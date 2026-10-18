// Parallel, interruptible and resumable analysis (ADR-0019), at the level of
// the analyzer and the fact store.
// Verifies: b16-resumable-analysis-v2 (criteria 2-4), nfr005-performance-budget-v3
// (criteria 3 and 4) and nfr004-deterministic-output-v1 (criterion 2).
#include "analysis/clang_analyzer.hpp"
#include "config/path_glob.hpp"
#include "facts/fact_export.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using lcart::analysis::AnalysisProgress;
using lcart::analysis::AnalysisRequest;
using lcart::analysis::AnalysisSummary;
using lcart::analysis::ClangAnalyzer;
using lcart::common::ErrorCode;
using lcart::common::Result;
using namespace lcart::facts;

constexpr std::size_t project_units = 12;

template <typename T>
T value_of(Result<T> result) {
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : T{};
}

std::unique_ptr<SqliteFactStore> open_store(const std::string& path) {
    auto store = SqliteFactStore::open(path);
    EXPECT_TRUE(store.has_value()) << (store ? "" : store.error().message);
    return store ? std::move(*store) : nullptr;
}

std::string export_of(const FactQuery& facts) {
    std::ostringstream out;
    const auto written = write_jsonl_export(facts, out);
    EXPECT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    return std::move(out).str();
}

/// How many translation units are no longer pending; they must come first.
std::size_t done_prefix(const std::vector<TranslationUnitFact>& units) {
    const auto first_pending =
        std::ranges::find(units, TranslationUnitStatus::pending, &TranslationUnitFact::status);
    EXPECT_TRUE(std::all_of(first_pending, units.end(), [](const TranslationUnitFact& unit) {
        return unit.status == TranslationUnitStatus::pending;
    })) << "a unit after the first pending one is done";
    return static_cast<std::size_t>(first_pending - units.begin());
}

class ParallelAnalysisTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-parallel-" + std::to_string(getpid()) + "-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
        work_dir_ = fs::canonical(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// A project of project_units translation units with the decisions that
    /// depend on earlier units (ADR-0012, ADR-0013): a `static` helper with
    /// one name in every unit, a `static inline` function in a header, a
    /// function declared in the header and defined in the first unit, three
    /// programs with their own `main`, and one unit that does not parse.
    /// Every unit also assigns its own `static` `op` to the field of a table
    /// that the first unit calls through, so its may_call edges need the
    /// field assignments of every unit (B-26).
    [[nodiscard]] fs::path write_project() const {
        const fs::path root = work_dir_ / "project";
        fs::create_directories(root / "a");
        fs::create_directories(root / "b");
        std::ofstream{root / "shared.h"} << "#ifndef SHARED_H\n#define SHARED_H\n"
                                            "int common(int value);\n"
                                            "struct ops {\n    int (*run)(int value);\n};\n"
                                            "int dispatch(const struct ops *ops);\n"
                                            "static inline int twice(int value) {\n"
                                            "    return 2 * value;\n}\n#endif\n";
        for (std::size_t unit = 0; unit < project_units; ++unit) {
            std::ofstream out{root / unit_file(unit)};
            out << "#include \"shared.h\"\n\n"
                << "static int helper(void) {\n    return twice(" << unit << ");\n}\n\n"
                << "int u" << unit << "(void) {\n    return helper() + common(" << unit
                << ");\n}\n";
            out << "\nstatic int op(int value) {\n    return value + " << unit << ";\n}\n"
                << "\nconst struct ops ops" << unit << " = {op};\n";
            if (unit == 0) {
                out << "\nint common(int value) {\n    return value;\n}\n";
                out << "\nint dispatch(const struct ops *ops) {\n    return ops->run(0);\n}\n";
            }
            if (unit % 4 == 1) {
                out << "\nint main(void) {\n    return u" << unit << "();\n}\n";
            }
            if (unit == 6) {
                out << "\nint broken(void) { return 1 +; }\n";
            }
        }
        return root;
    }

    [[nodiscard]] static std::string unit_file(std::size_t unit) {
        return std::string{unit % 2 == 0 ? "a" : "b"} + "/u" + (unit < 10 ? "0" : "") +
               std::to_string(unit) + ".c";
    }

    [[nodiscard]] static std::vector<std::string> project_files() {
        std::vector<std::string> files;
        files.reserve(project_units);
        for (std::size_t unit = 0; unit < project_units; ++unit) {
            files.push_back(unit_file(unit));
        }
        return files;
    }

    /// Writes compile_commands.json for `files` of `root`, compiled with
    /// `-I. <extra>`.
    [[nodiscard]] fs::path write_compilation_database(const fs::path& root,
                                                      const std::vector<std::string>& files,
                                                      const std::string& extra = "") const {
        const fs::path path = work_dir_ / "compile_commands.json";
        std::ofstream out{path};
        out << "[\n";
        for (std::size_t i = 0; i < files.size(); ++i) {
            out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c -I. )"
                << extra << ' ' << files[i] << R"(", "file": ")" << files[i] << "\"}"
                << (i + 1 < files.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return path;
    }

    [[nodiscard]] static AnalysisRequest
    request(const fs::path& root, const fs::path& compilation_database, unsigned jobs) {
        AnalysisRequest request{.compilation_database = compilation_database,
                                .root = root,
                                .exclude_globs = {},
                                .module_rules = {}};
        request.jobs = jobs;
        return request;
    }

    /// The project analysed by an uninterrupted run with one worker.
    struct Reference {
        std::string exported;
        AnalysisSummary summary;
    };
    [[nodiscard]] static Reference reference(const AnalysisRequest& base) {
        const auto store = open_store(":memory:");
        AnalysisRequest one_worker = base;
        one_worker.jobs = 1;
        ClangAnalyzer analyzer;
        Reference result;
        result.summary = value_of(analyzer.analyze(one_worker, *store));
        result.exported = export_of(*store);
        return result;
    }

    /// Resumes the analysis in the database at `path` with `jobs` workers;
    /// its progress reports go to `reports`.
    [[nodiscard]] static AnalysisSummary resumed_run(const AnalysisRequest& base,
                                                     const std::string& path, unsigned jobs,
                                                     std::vector<AnalysisProgress>& reports) {
        // A new store, as in a new process: the writer's state comes from
        // the database.
        const auto store = open_store(path);
        AnalysisRequest resumed = base;
        resumed.jobs = jobs;
        resumed.resume = true;
        resumed.on_progress = [&reports](const AnalysisProgress& progress) {
            reports.push_back(progress);
        };
        ClangAnalyzer analyzer;
        return value_of(analyzer.analyze(resumed, *store));
    }

    /// Runs `base` into the database at `path` and stops it once
    /// `stop_after` translation units of the run are done. Returns the summary.
    [[nodiscard]] static AnalysisSummary interrupted_run(const AnalysisRequest& base,
                                                         const std::string& path,
                                                         std::uint32_t stop_after) {
        const auto store = open_store(path);
        std::atomic<bool> stop{false};
        AnalysisRequest stopping = base;
        stopping.stop = &stop;
        stopping.progress_interval = 0ms;
        stopping.on_progress = [&stop, stop_after](const AnalysisProgress& progress) {
            if (progress.parsed + progress.failed >= stop_after) {
                stop.store(true);
            }
        };
        ClangAnalyzer analyzer;
        return value_of(analyzer.analyze(stopping, *store));
    }

private:
    fs::path work_dir_;
};

/// Expects the analysis of `project` with 4 workers to write the facts and
/// report the summary of the analysis with 1 worker.
void expect_same_with_four_workers(const AnalysisRequest& project) {
    const auto one = open_store(":memory:");
    const auto four = open_store(":memory:");
    AnalysisRequest one_worker = project;
    one_worker.jobs = 1;
    AnalysisRequest four_workers = project;
    four_workers.jobs = 4;
    ClangAnalyzer analyzer;
    const AnalysisSummary by_one = value_of(analyzer.analyze(one_worker, *one));
    const AnalysisSummary by_four = value_of(analyzer.analyze(four_workers, *four));
    EXPECT_EQ(export_of(*four), export_of(*one)) << project.root;
    EXPECT_EQ(by_four.warnings, by_one.warnings) << project.root;
    EXPECT_EQ(by_four.translation_units_parsed, by_one.translation_units_parsed) << project.root;
    EXPECT_EQ(by_four.translation_units_failed, by_one.translation_units_failed) << project.root;
    EXPECT_EQ(by_four.diagnostics, by_one.diagnostics) << project.root;
}

/// Expects `report`, number `index` of `count`, to hold consistent counts
/// for `total` units, the estimate of ADR-0019 (elapsed / done * left), and
/// to be the final one only if it is the last.
void expect_consistent_report(const AnalysisProgress& report, std::size_t index, std::size_t count,
                              std::uint32_t total) {
    const std::uint32_t done = report.parsed + report.failed;
    EXPECT_EQ(report.total, total);
    EXPECT_GE(report.elapsed, 0ms);
    const std::uint32_t left = total - report.already_done - done;
    const auto expected =
        done == 0 ? std::nullopt
                  : std::optional{std::chrono::milliseconds{report.elapsed.count() * left / done}};
    EXPECT_EQ(report.remaining, expected) << "report " << index;
    EXPECT_EQ(report.finished, index + 1 == count) << "report " << index;
}

/// Expects every report to be consistent (above), and counts and elapsed
/// time never to go down from one report to the next.
void expect_consistent(const std::vector<AnalysisProgress>& reports, std::uint32_t total) {
    for (std::size_t i = 0; i < reports.size(); ++i) {
        expect_consistent_report(reports[i], i, reports.size(), total);
    }
    for (std::size_t i = 1; i < reports.size(); ++i) {
        EXPECT_GE(reports[i].parsed + reports[i].failed,
                  reports[i - 1].parsed + reports[i - 1].failed);
        EXPECT_GE(reports[i].elapsed, reports[i - 1].elapsed);
    }
}

/// The `.c` files under `root`, root-relative and sorted.
std::vector<std::string> sources_of(const fs::path& root) {
    std::vector<std::string> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator{root}) {
        if (entry.path().extension() == ".c") {
            files.push_back(entry.path().lexically_relative(root).generic_string());
        }
    }
    std::ranges::sort(files);
    return files;
}

// Verifies: nfr004-deterministic-output-v1 (criterion 2: 1 and N threads,
// identical exports) and b16-resumable-analysis-v2 (criterion 4), on every
// fixture and on a project whose facts depend on the order of its units.
TEST_F(ParallelAnalysisTest, OneAndFourWorkersExportTheSameFacts) {
    struct Project {
        fs::path root;
        std::string extra;
    };
    std::vector<Project> projects{{.root = write_project(), .extra = ""}};
    for (const char* name : {"two_modules", "two_programs", "static_same_name", "fn_pointers",
                             "macros", "gnu_inline", "unresolved_include"}) {
        projects.push_back({.root = fs::path{LCART_FIXTURES_DIR} / name, .extra = ""});
    }
    projects.push_back({.root = fs::path{LCART_FIXTURES_DIR} / "broken_tu", .extra = "-Iinclude"});

    for (const Project& project : projects) {
        expect_same_with_four_workers(request(
            project.root,
            write_compilation_database(project.root, sources_of(project.root), project.extra), 4));
    }
}

// Verifies: b16-resumable-analysis-v2 (criterion 3: completed units are
// committed; ADR-0019: committed in order, so they are a prefix, and the
// rest stays pending).
TEST_F(ParallelAnalysisTest, InterruptedRunCommitsTheUnitsDoneInOrder) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 2);
    const std::string database = (work_dir() / "facts.lcart.db").string();

    const AnalysisSummary summary = interrupted_run(base, database, 2);
    EXPECT_TRUE(summary.interrupted);
    EXPECT_EQ(summary.translation_units_total, project_units);

    const auto store = open_store(database);
    const std::vector<TranslationUnitFact> units = value_of(store->translation_units());
    ASSERT_EQ(units.size(), project_units);
    const std::size_t done = done_prefix(units);
    EXPECT_GE(done, 2U);
    EXPECT_LT(done, project_units);
    EXPECT_EQ(summary.translation_units_parsed + summary.translation_units_failed, done);
    // Derived once, after the last unit: not yet.
    EXPECT_EQ(value_of(store->coverage()), AnalysisCoverage{});
}

// Verifies: nfr005-performance-budget-v3 (criterion 4: the export of an
// interrupted and resumed analysis equals an uninterrupted one, and the
// units done before are not parsed again, counted in the summary) and
// b16-resumable-analysis-v2 (ADR-0019: warnings are not repeated).
TEST_F(ParallelAnalysisTest, ResumedRunExportsWhatAnUninterruptedRunExports) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 3);
    const Reference uninterrupted = reference(base);
    ASSERT_FALSE(uninterrupted.summary.warnings.empty()); // the programs' `main`s
    const std::string database = (work_dir() / "facts.lcart.db").string();

    const AnalysisSummary first = interrupted_run(base, database, 3);
    ASSERT_TRUE(first.interrupted);
    const std::uint32_t done_before =
        first.translation_units_parsed + first.translation_units_failed;

    std::vector<AnalysisProgress> reports;
    const AnalysisSummary second = resumed_run(base, database, 2, reports);
    EXPECT_FALSE(second.interrupted);
    EXPECT_EQ(second.translation_units_already_done, done_before);
    EXPECT_EQ(second.translation_units_total, project_units);
    EXPECT_EQ(second.translation_units_parsed, uninterrupted.summary.translation_units_parsed);
    EXPECT_EQ(second.translation_units_failed, uninterrupted.summary.translation_units_failed);
    std::vector<std::string> warnings = first.warnings;
    warnings.insert(warnings.end(), second.warnings.begin(), second.warnings.end());
    EXPECT_EQ(warnings, uninterrupted.summary.warnings);

    const auto store = open_store(database);
    EXPECT_EQ(export_of(*store), uninterrupted.exported);
    EXPECT_EQ(done_prefix(value_of(store->translation_units())), project_units);
}

// Verifies: b26-function-pointer-tables-v1 (criterion 6: the field
// assignments of the units committed before the interruption are stored,
// so the resumed run derives the may_call edges of an uninterrupted one:
// `dispatch` may call the `op` of every unit).
TEST_F(ParallelAnalysisTest, ResumedRunDerivesTheMayCallEdgesOfEveryUnit) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 2);
    const Reference uninterrupted = reference(base);
    const std::string database = (work_dir() / "facts.lcart.db").string();
    const AnalysisSummary first = interrupted_run(base, database, 3);
    ASSERT_TRUE(first.interrupted);

    std::vector<AnalysisProgress> reports;
    static_cast<void>(resumed_run(base, database, 2, reports));
    const auto store = open_store(database);
    const std::vector<CallFact> may_calls = value_of(store->may_calls());
    EXPECT_EQ(may_calls.size(), project_units);
    for (const CallFact& call : may_calls) {
        EXPECT_EQ(call.via, "ops.run");
    }
    EXPECT_EQ(export_of(*store), uninterrupted.exported);
}

// Verifies: nfr005-performance-budget-v3 (criteria 3 and 4: the progress of
// a resumed run counts the units done before as already done, and parses
// only the others).
TEST_F(ParallelAnalysisTest, ResumedRunParsesOnlyTheUnitsLeft) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 2);
    const std::string database = (work_dir() / "facts.lcart.db").string();
    const AnalysisSummary first = interrupted_run(base, database, 4);
    ASSERT_TRUE(first.interrupted);
    const std::uint32_t done_before =
        first.translation_units_parsed + first.translation_units_failed;

    std::vector<AnalysisProgress> reports;
    static_cast<void>(resumed_run(base, database, 3, reports));
    ASSERT_FALSE(reports.empty());
    expect_consistent(reports, project_units);
    EXPECT_EQ(reports.back().already_done, done_before);
    EXPECT_EQ(reports.back().parsed + reports.back().failed, project_units - done_before);
}

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume continues an
// unchanged analysis) and nfr005-performance-budget-v3 (criteria 3 and 4)
// when two compilation database entries name one file, through a symbolic
// link: the file is one translation unit, the first in sorted order, so
// progress, summary and database agree on the total, and resume works.
TEST_F(ParallelAnalysisTest, EntriesNamingOneFileThroughALinkAreOneUnit) {
    const fs::path root = write_project();
    fs::create_directory_symlink(root / "a", root / "link");
    std::vector<std::string> files = project_files();
    const AnalysisRequest unique = request(root, write_compilation_database(root, files), 2);
    const Reference expected = reference(unique);
    files.emplace_back("link/u00.c");
    files.emplace_back("link/u02.c");
    const AnalysisRequest base = request(root, write_compilation_database(root, files), 2);

    const std::string database = (work_dir() / "facts.lcart.db").string();
    const AnalysisSummary first = interrupted_run(base, database, 2);
    ASSERT_TRUE(first.interrupted);
    EXPECT_EQ(first.translation_units_total, project_units);
    EXPECT_EQ(value_of(open_store(database)->translation_units()).size(), project_units);

    std::vector<AnalysisProgress> reports;
    const AnalysisSummary resumed = resumed_run(base, database, 3, reports);
    EXPECT_FALSE(resumed.interrupted);
    EXPECT_EQ(resumed.translation_units_total, project_units);
    EXPECT_EQ(resumed.translation_units_parsed + resumed.translation_units_failed, project_units);
    ASSERT_FALSE(reports.empty());
    expect_consistent(reports, project_units);
    EXPECT_EQ(export_of(*open_store(database)), expected.exported);
}

/// A progress callback that, once `units` units are done, makes `other`
/// take the database for writing (an open transaction with a write), so
/// the next commit of the analysis fails; sets `blocked` then.
std::function<void(const AnalysisProgress&)> block_after(std::uint32_t units,
                                                         SqliteFactStore& other, bool& blocked) {
    return [units, &other, &blocked](const AnalysisProgress& progress) {
        if (progress.parsed + progress.failed != units || blocked) {
            return;
        }
        blocked = true;
        EXPECT_TRUE(other.begin_transaction().has_value());
        EXPECT_TRUE(other.set_meta("blocking", "yes").has_value());
    };
}

/// Expects every warning to name `text`.
void expect_each_names(const std::vector<std::string>& warnings, std::string_view text) {
    for (const std::string& warning : warnings) {
        EXPECT_NE(warning.find(text), std::string::npos) << warning;
    }
}

// Verifies: adr0019-resume-v1 (warnings of the units done are printed by the
// run that did them): each unit's warnings are handed out when its
// transaction commits, so a later failure does not lose them, and those of
// a unit that was not committed are not handed out.
TEST_F(ParallelAnalysisTest, WarningsOfCommittedUnitsSurviveALaterFailure) {
    const fs::path root = write_project();
    AnalysisRequest failing = request(
        root, write_compilation_database(root, project_files(), "-fplugin=/nonexistent/evil.so"),
        1);
    const std::string database = (work_dir() / "facts.lcart.db").string();
    const auto store = open_store(database);
    // Another writer takes the database after the second unit, so the
    // third unit's transaction fails.
    const auto other = open_store(database);
    std::vector<std::string> warnings;
    failing.on_warning = [&warnings](const std::string& warning) {
        warnings.push_back(warning);
    };
    failing.progress_interval = 0ms;
    bool blocked = false;
    failing.on_progress = block_after(2, *other, blocked);
    ClangAnalyzer analyzer;
    const auto result = analyzer.analyze(failing, *store);
    other->rollback_transaction();
    EXPECT_TRUE(blocked);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(warnings.size(), 2U);
    expect_each_names(warnings, "-fplugin=/nonexistent/evil.so");
    EXPECT_EQ(done_prefix(value_of(store->translation_units())), 2U);
}

/// Expects `failing` to end with an error naming `file` and `message`
/// after committing exactly its first `unit` units, with their warnings
/// (one per unit).
void expect_ends_at_unit(AnalysisRequest failing, std::size_t unit, std::string_view file,
                         std::string_view message) {
    const auto store = open_store(":memory:");
    std::vector<std::string> warnings;
    failing.on_warning = [&warnings](const std::string& warning) {
        warnings.push_back(warning);
    };
    ClangAnalyzer analyzer;
    const auto result = analyzer.analyze(failing, *store);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find(file), std::string::npos) << result.error().message;
    EXPECT_NE(result.error().message.find(message), std::string::npos) << result.error().message;
    EXPECT_EQ(done_prefix(value_of(store->translation_units())), unit);
    EXPECT_EQ(warnings.size(), unit);
}

// Verifies: nfr004-deterministic-output-v1 (no thread scheduling affects
// the facts) for a parse that throws: with 4 workers, exactly the units
// before it are committed (with their warnings), and its error is the one
// reported, on every run.
TEST_F(ParallelAnalysisTest, ParseThatThrowsEndsTheRunAtItsUnit) {
    constexpr std::size_t throwing_unit = 5;
    const fs::path root = write_project();
    AnalysisRequest failing = request(
        root, write_compilation_database(root, project_files(), "-fplugin=/nonexistent/evil.so"),
        4);
    std::vector<std::string> sorted_files = project_files();
    std::ranges::sort(sorted_files); // the order of the analysis
    failing.before_parse = [](std::size_t index) {
        // Units after it throw too, and may finish first.
        if (index >= throwing_unit) {
            throw std::runtime_error{"injected failure of unit " + std::to_string(index)};
        }
    };
    for (int run = 0; run < 5; ++run) {
        expect_ends_at_unit(failing, throwing_unit, sorted_files[throwing_unit],
                            "injected failure of unit 5");
    }
}

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume refuses a changed
// compilation database and says why).
TEST_F(ParallelAnalysisTest, ResumeRefusesAChangedCompilationDatabase) {
    const fs::path root = write_project();
    const fs::path compilation_database = write_compilation_database(root, project_files());
    const std::string database = (work_dir() / "facts.lcart.db").string();
    static_cast<void>(interrupted_run(request(root, compilation_database, 1), database, 1));

    std::ofstream{compilation_database, std::ios::app} << "\n"; // same commands, other bytes
    AnalysisRequest resumed = request(root, compilation_database, 1);
    resumed.resume = true;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(resumed, *open_store(database));
    ASSERT_FALSE(summary.has_value());
    EXPECT_NE(summary.error().message.find("compilation database"), std::string::npos)
        << summary.error().message;
    EXPECT_EQ(summary.error().message.find("configuration"), std::string::npos)
        << summary.error().message;
}

// Verifies: b16-resumable-analysis-v2 (criterion 2: resume refuses another
// tool version and says why).
TEST_F(ParallelAnalysisTest, ResumeRefusesAnotherLcartVersion) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 1);
    const std::string database = (work_dir() / "facts.lcart.db").string();
    static_cast<void>(interrupted_run(base, database, 1));
    ASSERT_TRUE(open_store(database)->set_meta("tool_version", "0.0.0-other").has_value());

    AnalysisRequest resumed = base;
    resumed.resume = true;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(resumed, *open_store(database));
    ASSERT_FALSE(summary.has_value());
    EXPECT_NE(summary.error().message.find("lcart version"), std::string::npos)
        << summary.error().message;
    EXPECT_NE(summary.error().message.find("0.0.0-other"), std::string::npos)
        << summary.error().message;
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: the configuration hash
// covers the module rules, the exclusions and the translation units).
TEST_F(ParallelAnalysisTest, ResumeRefusesAChangedConfiguration) {
    const fs::path root = write_project();
    const AnalysisRequest base =
        request(root, write_compilation_database(root, project_files()), 1);
    const std::string database = (work_dir() / "facts.lcart.db").string();
    static_cast<void>(interrupted_run(base, database, 1));

    AnalysisRequest excluded = base;
    excluded.exclude_globs.push_back(value_of(lcart::config::PathGlob::parse("b/u11.c")));
    AnalysisRequest other_rules = base;
    other_rules.module_rules.push_back(
        {.name = "everything", .paths = {value_of(lcart::config::PathGlob::parse("**"))}});
    AnalysisRequest other_root = base;
    other_root.root = root / "a";

    for (AnalysisRequest& changed :
         {std::ref(excluded), std::ref(other_rules), std::ref(other_root)}) {
        changed.resume = true;
        ClangAnalyzer analyzer;
        const auto summary = analyzer.analyze(changed, *open_store(database));
        ASSERT_FALSE(summary.has_value());
        EXPECT_NE(summary.error().message.find("configuration"), std::string::npos)
            << summary.error().message;
    }
}

// Verifies: b16-resumable-analysis-v2 (resume needs an analysis to resume).
TEST_F(ParallelAnalysisTest, ResumeNeedsAnInterruptedAnalysis) {
    const fs::path root = write_project();
    AnalysisRequest resumed = request(root, write_compilation_database(root, project_files()), 1);
    resumed.resume = true;
    ClangAnalyzer analyzer;
    const auto store = open_store(":memory:");
    const auto empty = analyzer.analyze(resumed, *store);
    ASSERT_FALSE(empty.has_value());
    EXPECT_NE(empty.error().message.find("no analysis to resume"), std::string::npos)
        << empty.error().message;

    // A writer without per-unit transactions cannot be resumed.
    const auto plain = analyzer.analyze(resumed, static_cast<FactWriter&>(*store));
    ASSERT_FALSE(plain.has_value());
    EXPECT_EQ(plain.error().code, ErrorCode::invalid_argument);
}

// Verifies: b16-resumable-analysis-v2 (criterion 3: a stop before the first
// unit commits nothing and reports the run as interrupted).
TEST_F(ParallelAnalysisTest, StopBeforeTheFirstUnitCommitsNothing) {
    const fs::path root = write_project();
    AnalysisRequest stopped = request(root, write_compilation_database(root, project_files()), 4);
    const std::atomic<bool> stop{true};
    stopped.stop = &stop;
    const auto store = open_store(":memory:");
    ClangAnalyzer analyzer;
    const AnalysisSummary summary = value_of(analyzer.analyze(stopped, *store));
    EXPECT_TRUE(summary.interrupted);
    EXPECT_EQ(summary.translation_units_parsed + summary.translation_units_failed, 0U);
    EXPECT_EQ(done_prefix(value_of(store->translation_units())), 0U);
}

// Verifies: nfr005-performance-budget-v3 (criterion 3: parsed, failed and
// total units and an estimate of the time left, with the formula of
// ADR-0019; the clock is injected and reaches no fact).
TEST_F(ParallelAnalysisTest, ProgressReportsCountsAndAnEstimate) {
    const fs::path root = fs::path{LCART_FIXTURES_DIR} / "broken_tu";
    AnalysisRequest observed = request(
        root, write_compilation_database(root, {"ok.c", "broken.c", "also_ok.c"}, "-Iinclude"), 1);
    std::chrono::steady_clock::time_point now{};
    observed.clock = [&now] {
        now += 10s;
        return now;
    };
    observed.progress_interval = 0ms;
    std::vector<AnalysisProgress> reports;
    observed.on_progress = [&reports](const AnalysisProgress& progress) {
        reports.push_back(progress);
    };
    const auto store = open_store(":memory:");
    ClangAnalyzer analyzer;
    static_cast<void>(value_of(analyzer.analyze(observed, *store)));

    ASSERT_GE(reports.size(), 4U); // one after each unit, and the last
    expect_consistent(reports, 3);
    EXPECT_GT(reports.front().elapsed, 0ms); // the injected clock, not the real one
    EXPECT_EQ(reports.back().already_done, 0U);
    EXPECT_EQ(reports.back().parsed, 2U);
    EXPECT_EQ(reports.back().failed, 1U);
    EXPECT_EQ(reports.back().remaining, 0ms);
}

// Verifies: nfr005-performance-budget-v3 (criterion 3: reports come no more
// often than the interval asks, except the last).
TEST_F(ParallelAnalysisTest, ProgressWaitsForTheInterval) {
    const fs::path root = write_project();
    AnalysisRequest observed = request(root, write_compilation_database(root, project_files()), 2);
    std::chrono::steady_clock::time_point now{};
    observed.clock = [&now] {
        now += 1s;
        return now;
    };
    observed.progress_interval = 5s;
    std::vector<AnalysisProgress> reports;
    observed.on_progress = [&reports](const AnalysisProgress& progress) {
        reports.push_back(progress);
    };
    const auto store = open_store(":memory:");
    ClangAnalyzer analyzer;
    static_cast<void>(value_of(analyzer.analyze(observed, *store)));

    ASSERT_FALSE(reports.empty());
    EXPECT_TRUE(reports.back().finished);
    for (std::size_t i = 1; i + 1 < reports.size(); ++i) {
        EXPECT_GE(reports[i].elapsed - reports[i - 1].elapsed, 5s) << "report " << i;
    }
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: `--jobs` from 1 to 256).
TEST_F(ParallelAnalysisTest, JobsOutOfRangeIsAnError) {
    const fs::path root = write_project();
    const fs::path compilation_database = write_compilation_database(root, project_files());
    for (const unsigned jobs : {0U, lcart::analysis::max_jobs + 1}) {
        const auto store = open_store(":memory:");
        ClangAnalyzer analyzer;
        const auto summary = analyzer.analyze(request(root, compilation_database, jobs), *store);
        ASSERT_FALSE(summary.has_value()) << jobs;
        EXPECT_EQ(summary.error().code, ErrorCode::invalid_argument);
        EXPECT_NE(summary.error().message.find("jobs"), std::string::npos)
            << summary.error().message;
    }
}

} // namespace
