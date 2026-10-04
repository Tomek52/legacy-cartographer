// Verifies: fr001-ingest-compilation-database-v1 (criterion 2: a failed TU
// is a diagnostic and analysis continues; the coverage part of criterion 1,
// on the broken_tu fixture instead of two_modules, which arrives with B-08)
// and docs/security.md T5 (plugin arguments, also inside response files,
// never reach Clang).
#include "analysis/clang_analyzer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using lcart::common::Status;
using namespace lcart::facts;

fs::path fixture_dir() {
    return fs::path{LCART_FIXTURES_DIR} / "broken_tu";
}

/// Records what the analyzer writes; ids are assigned in call order.
class RecordingFactWriter final : public FactWriter {
public:
    lcart::common::Result<ModuleId> add_module(const ModuleFact& /*module*/) override {
        return ModuleId{++next_id_};
    }
    lcart::common::Result<FileId> add_file(const FileFact& /*file*/) override {
        return FileId{++next_id_};
    }
    Status add_include(const IncludeFact& /*include*/) override { return {}; }
    lcart::common::Result<FunctionId> add_function(const FunctionFact& /*function*/) override {
        return FunctionId{++next_id_};
    }
    Status add_call(const CallFact& /*call*/) override { return {}; }
    Status add_diagnostic(const DiagnosticFact& diagnostic) override {
        diagnostics_.push_back(diagnostic);
        return {};
    }
    Status set_coverage(const AnalysisCoverage& value) override {
        coverage_ = value;
        return {};
    }

    [[nodiscard]] const std::vector<DiagnosticFact>& diagnostics() const { return diagnostics_; }
    [[nodiscard]] const AnalysisCoverage& coverage() const { return coverage_; }

private:
    std::int64_t next_id_{0};
    std::vector<DiagnosticFact> diagnostics_;
    AnalysisCoverage coverage_;
};

class ClangAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-analyzer-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// Writes compile_commands.json with one entry per file, each compiled
    /// with `extra` arguments added.
    [[nodiscard]] fs::path write_compilation_database(const std::vector<std::string>& files,
                                                      const std::string& extra = "") const {
        const fs::path path = work_dir_ / "compile_commands.json";
        std::ofstream out{path};
        out << "[\n";
        for (std::size_t i = 0; i < files.size(); ++i) {
            out << R"(  {"directory": ")" << fixture_dir().string() << R"(", "command": "clang -c )"
                << extra << " -Iinclude " << files[i] << R"(", "file": ")" << files[i] << "\"}"
                << (i + 1 < files.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return path;
    }

private:
    fs::path work_dir_;
};

TEST_F(ClangAnalyzerTest, CountsParsedAndFailedTranslationUnits) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(
        {.compilation_database = write_compilation_database({"ok.c", "broken.c", "also_ok.c"}),
         .root = fixture_dir(),
         .exclude_globs = {}},
        facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_total, 3U);
    EXPECT_EQ(summary->translation_units_parsed, 2U);
    EXPECT_EQ(facts.coverage().translation_units_total, 3U);
    EXPECT_EQ(facts.coverage().translation_units_parsed, 2U);
}

TEST_F(ClangAnalyzerTest, RecordsSyntaxErrorAsDiagnosticWithLocation) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary =
        analyzer.analyze({.compilation_database = write_compilation_database({"broken.c", "ok.c"}),
                          .root = fixture_dir(),
                          .exclude_globs = {}},
                         facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    ASSERT_EQ(facts.diagnostics().size(), 1U);
    const DiagnosticFact& diagnostic = facts.diagnostics().front();
    EXPECT_EQ(fs::path{diagnostic.translation_unit}.filename(), "broken.c");
    EXPECT_EQ(fs::path{diagnostic.file}.filename(), "broken.c");
    EXPECT_EQ(diagnostic.line, 4U);
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::error);
    EXPECT_FALSE(diagnostic.message.empty());
    EXPECT_EQ(summary->diagnostics, 1U);
}

TEST_F(ClangAnalyzerTest, StripsPluginArgumentsAndWarns) {
    // Without the filter, Clang fails to load the (nonexistent) plugins and
    // the translation unit counts as failed.
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze(
        {.compilation_database = write_compilation_database(
             {"ok.c"}, "-fplugin=/nonexistent/evil.so -Xclang -load -Xclang /nonexistent/evil.so"),
         .root = fixture_dir(),
         .exclude_globs = {}},
        facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_parsed, 1U);
    ASSERT_EQ(summary->warnings.size(), 2U);
    EXPECT_NE(summary->warnings[0].find("-fplugin=/nonexistent/evil.so"), std::string::npos);
    EXPECT_NE(summary->warnings[1].find("-load"), std::string::npos);
}

TEST_F(ClangAnalyzerTest, StripsPluginArgumentsInsideResponseFiles) {
    {
        std::ofstream rsp{work_dir() / "flags.rsp"};
        rsp << "-fplugin=/nonexistent/evil.so\n";
    }
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = write_compilation_database(
                                               {"ok.c"}, "@" + (work_dir() / "flags.rsp").string()),
                                           .root = fixture_dir(),
                                           .exclude_globs = {}},
                                          facts);

    ASSERT_TRUE(summary.has_value()) << summary.error().message;
    EXPECT_EQ(summary->translation_units_parsed, 1U);
    ASSERT_EQ(summary->warnings.size(), 1U);
    EXPECT_NE(summary->warnings[0].find("-fplugin=/nonexistent/evil.so"), std::string::npos);
}

TEST_F(ClangAnalyzerTest, MissingCompilationDatabaseIsAnError) {
    RecordingFactWriter facts;
    ClangAnalyzer analyzer;
    const auto summary = analyzer.analyze({.compilation_database = work_dir() / "missing.json",
                                           .root = fixture_dir(),
                                           .exclude_globs = {}},
                                          facts);

    ASSERT_FALSE(summary.has_value());
    EXPECT_EQ(summary.error().code, lcart::common::ErrorCode::io_error);
}

} // namespace
