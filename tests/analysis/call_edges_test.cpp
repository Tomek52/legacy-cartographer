// Verifies: fr004-record-call-edges-v1 (criteria 1-3, on the fn_pointers,
// macros and two_programs fixtures), through the SQLite store.
#include "analysis/clang_analyzer.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::AnalysisSummary;
using lcart::analysis::ClangAnalyzer;
using namespace lcart::facts;

/// One call edge, with names and paths instead of ids, so expectations read
/// like the fixture's README. `callee` is empty for an indirect call.
struct CallSummary {
    std::string callee;
    CallKind kind{CallKind::direct};
    std::string via;
    std::string file;
    std::uint32_t line{};
    std::uint32_t column{};

    bool operator==(const CallSummary&) const = default;
};

std::ostream& operator<<(std::ostream& out, const CallSummary& call) {
    return out << (call.kind == CallKind::direct ? "direct " : "indirect ") << '\'' << call.callee
               << "' via '" << call.via << "' at " << call.file << ':' << call.line << ':'
               << call.column;
}

CallSummary direct(std::string callee, std::string file, std::uint32_t line, std::uint32_t column) {
    return {.callee = std::move(callee),
            .kind = CallKind::direct,
            .via = {},
            .file = std::move(file),
            .line = line,
            .column = column};
}

CallSummary indirect(std::string via, std::string file, std::uint32_t line, std::uint32_t column) {
    return {.callee = {},
            .kind = CallKind::indirect,
            .via = std::move(via),
            .file = std::move(file),
            .line = line,
            .column = column};
}

class CallEdgesTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-calls-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
        auto store = SqliteFactStore::open(":memory:");
        ASSERT_TRUE(store.has_value()) << store.error().message;
        store_ = std::move(*store);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    /// Analyses `files` of the fixture `name`, each compiled with the fixture
    /// root on the include path; `functions` are the names of the fixture's
    /// functions, `paths` its files.
    void analyze(const std::string& name, const std::vector<std::string>& files,
                 const std::vector<std::string>& functions, const std::vector<std::string>& paths) {
        const fs::path root = fs::path{LCART_FIXTURES_DIR} / name;
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << "[\n";
            for (std::size_t i = 0; i < files.size(); ++i) {
                out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c -I. )"
                    << files[i] << R"(", "file": ")" << files[i] << "\"}"
                    << (i + 1 < files.size() ? ",\n" : "\n");
            }
            out << "]\n";
        }
        ClangAnalyzer analyzer;
        auto summary = analyzer.analyze(
            {.compilation_database = database, .root = root, .exclude_globs = {}}, *store_);
        ASSERT_TRUE(summary.has_value()) << summary.error().message;
        ASSERT_EQ(summary->translation_units_parsed, files.size());
        summary_ = std::move(*summary);
        name_functions(functions);
        name_files(paths);
    }

    void name_functions(const std::vector<std::string>& functions) {
        for (const std::string& function : functions) {
            for (const FunctionFact& fact : this->functions(function)) {
                names_.emplace(fact.id, fact.name);
            }
        }
    }

    void name_files(const std::vector<std::string>& paths) {
        for (const std::string& path : paths) {
            const auto file = store_->find_file(path);
            if (!file) {
                ADD_FAILURE() << "find_file(" << path << "): " << file.error().message;
            } else if (const std::optional<FileFact>& fact = *file) {
                paths_.emplace(fact->id, path);
            } else {
                ADD_FAILURE() << "no file " << path;
            }
        }
    }

    /// A query error fails the test, so an empty result means no function.
    [[nodiscard]] std::vector<FunctionFact> functions(const std::string& name) const {
        auto found = store_->find_functions(name);
        if (!found) {
            ADD_FAILURE() << "find_functions(" << name << "): " << found.error().message;
            return {};
        }
        return std::move(*found);
    }

    /// The calls from the only function named `caller`.
    [[nodiscard]] std::vector<CallSummary> calls(const std::string& caller) const {
        const std::vector<FunctionFact> found = functions(caller);
        if (found.size() != 1) {
            ADD_FAILURE() << found.size() << " functions named " << caller;
            return {};
        }
        return calls(found[0].id);
    }

    [[nodiscard]] std::vector<CallSummary> calls(FunctionId caller) const {
        auto found = store_->calls_from(caller);
        if (!found) {
            ADD_FAILURE() << "calls_from: " << found.error().message;
            return {};
        }
        std::vector<CallSummary> result;
        for (const CallFact& call : *found) {
            const auto callee = call.callee ? names_.find(*call.callee) : names_.end();
            const auto file = paths_.find(call.site.file);
            result.push_back({.callee = callee == names_.end() ? std::string{} : callee->second,
                              .kind = call.kind,
                              .via = call.via,
                              .file = file == paths_.end() ? "?" : file->second,
                              .line = call.site.line,
                              .column = call.site.column});
        }
        return result;
    }

    [[nodiscard]] std::vector<std::optional<FunctionId>> callees(FunctionId caller) const {
        auto found = store_->calls_from(caller);
        if (!found) {
            ADD_FAILURE() << "calls_from: " << found.error().message;
            return {};
        }
        std::vector<std::optional<FunctionId>> result;
        for (const CallFact& call : *found) {
            result.push_back(call.callee);
        }
        return result;
    }

    [[nodiscard]] const AnalysisSummary& summary() const { return summary_; }

    void analyze_fn_pointers() {
        analyze("fn_pointers", {"core.c", "rtu.c"},
                {"checksum", "transfer", "apply", "run", "size_only", "rtu_send", "rtu_recv"},
                {"core.c", "rtu.c"});
    }
    void analyze_macros() {
        analyze("macros", {"main.c"}, {"log_value", "fail", "step", "zero", "main"}, {"main.c"});
    }
    void analyze_two_programs() {
        analyze("two_programs",
                {"one/main.c", "one/report.c", "three/use.c", "two/extra.c", "two/main.c"},
                {"work", "main", "twice", "use", "report", "extra"},
                {"lib.h", "one/main.c", "three/use.c", "two/extra.c", "two/main.c"});
    }

    void analyze_gnu_inline() {
        analyze("gnu_inline", {"pick.c"}, {"pick", "first", "second"}, {"pick.c"});
    }

private:
    fs::path work_dir_;
    std::unique_ptr<SqliteFactStore> store_;
    AnalysisSummary summary_;
    std::map<FunctionId, std::string> names_;
    std::map<FileId, std::string> paths_;
};

// Verifies: fr004-record-call-edges-v1 (criterion 1; a function named
// without being called, `apply(checksum, ...)`, is not a call)
TEST_F(CallEdgesTest, DirectCallsAreExactlyTheCallsWrittenInTheBody) {
    analyze_fn_pointers();

    EXPECT_EQ(calls("rtu_send"), (std::vector{direct("checksum", "rtu.c", 5, 12)}));
    EXPECT_EQ(calls("run"),
              (std::vector{direct("apply", "core.c", 20, 12), direct("transfer", "core.c", 20, 28),
                           indirect("local.recv", "core.c", 20, 48)}));
    EXPECT_TRUE(calls("checksum").empty());
    EXPECT_TRUE(calls("rtu_recv").empty());
}

// Verifies: fr004-record-call-edges-v1 (criterion 1: a call in an
// unevaluated operand, `sizeof(checksum(1))`, never happens)
TEST_F(CallEdgesTest, CallInSizeofIsNotACall) {
    analyze_fn_pointers();

    EXPECT_TRUE(calls("size_only").empty());
}

// Verifies: fr004-record-call-edges-v1 (criterion 2: a struct member
// function pointer, as libmodbus calls its backend table)
TEST_F(CallEdgesTest, CallThroughStructMemberIsIndirectWithItsMemberName) {
    analyze_fn_pointers();

    EXPECT_EQ(calls("transfer"), (std::vector{indirect("backend.send", "core.c", 10, 16),
                                              indirect("backend.recv", "core.c", 11, 19)}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 2: a plain function
// pointer, called as `handler(...)` and as `(*handler)(...)`)
TEST_F(CallEdgesTest, CallThroughFunctionPointerIsIndirectWithoutMemberName) {
    analyze_fn_pointers();

    EXPECT_EQ(calls("apply"),
              (std::vector{indirect("", "core.c", 15, 12), indirect("", "core.c", 15, 29)}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 3; calls to functions
// outside the root, `strlen`, are not recorded)
TEST_F(CallEdgesTest, CallsInMacrosBelongToTheFunctionWhereTheMacroExpands) {
    analyze_macros();

    EXPECT_EQ(calls("step"), (std::vector{direct("log_value", "main.c", 10, 32)}));
    EXPECT_EQ(calls("main"), (std::vector{
                                 direct("step", "main.c", 14, 5),
                                 direct("step", "main.c", 14, 5),
                                 direct("log_value", "main.c", 15, 12),
                                 direct("fail", "main.c", 15, 12),
                                 direct("log_value", "main.c", 15, 12),
                                 direct("zero", "main.c", 15, 45),
                             }));
}

// Verifies: fr004-record-call-edges-v1 (criterion 3, for a function whose
// whole definition is written by a macro)
TEST_F(CallEdgesTest, CallsInFunctionWrittenByMacroBelongToThatFunction) {
    analyze_macros();

    EXPECT_EQ(calls("zero"), (std::vector{direct("log_value", "main.c", 11, 1)}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 1, with two programs in
// one compilation database: each `main` calls its own `work`; ADR-0013)
TEST_F(CallEdgesTest, EachDefinitionOfAnExternalFunctionKeepsItsOwnCalls) {
    analyze_two_programs();

    const std::vector<FunctionFact> mains = functions("main");
    const std::vector<FunctionFact> works = functions("work");
    ASSERT_EQ(mains.size(), 2U);
    ASSERT_EQ(works.size(), 2U);
    EXPECT_EQ(mains[0].usr, "c:@F@main");
    EXPECT_EQ(mains[1].usr, "two/main.c#c:@F@main");
    EXPECT_EQ(works[0].usr, "c:@F@work");
    EXPECT_EQ(works[1].usr, "two/main.c#c:@F@work");
    EXPECT_EQ(mains[0].linkage, Linkage::external);
    EXPECT_EQ(mains[1].linkage, Linkage::external);

    const auto one_calls = calls(mains[0].id);
    const auto two_calls = calls(mains[1].id);
    EXPECT_EQ(one_calls, (std::vector{direct("work", "one/main.c", 8, 12)}));
    EXPECT_EQ(two_calls, (std::vector{direct("work", "two/main.c", 8, 12)}));
    // Same names; the ids tell the two `work`s apart.
    EXPECT_EQ(callees(mains[0].id), (std::vector<std::optional<FunctionId>>{works[0].id}));
    EXPECT_EQ(callees(mains[1].id), (std::vector<std::optional<FunctionId>>{works[1].id}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 1 with ADR-0013: one
// definition in a header reached from three translation units is one
// function whose calls are recorded once, naming the first `work`)
TEST_F(CallEdgesTest, DefinitionInHeaderIsOneFunctionWithItsCallsOnce) {
    analyze_two_programs();

    const std::vector<FunctionFact> twice = functions("twice");
    const std::vector<FunctionFact> works = functions("work");
    ASSERT_EQ(twice.size(), 1U);
    ASSERT_EQ(works.size(), 2U);
    EXPECT_EQ(twice[0].usr, "c:@F@twice");
    EXPECT_EQ(twice[0].linkage, Linkage::external);
    EXPECT_EQ(calls(twice[0].id),
              (std::vector{direct("work", "lib.h", 7, 12), direct("work", "lib.h", 7, 26)}));
    EXPECT_EQ(callees(twice[0].id),
              (std::vector<std::optional<FunctionId>>{works[0].id, works[0].id}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 1 with ADR-0013: a
// translation unit that only declares `work` calls the first definition)
TEST_F(CallEdgesTest, CallToFunctionOnlyDeclaredNamesItsFirstDefinition) {
    analyze_two_programs();

    const std::vector<FunctionFact> works = functions("work");
    ASSERT_EQ(works.size(), 2U);
    EXPECT_EQ(calls("use"), (std::vector{direct("work", "three/use.c", 4, 12)}));
    const std::vector<FunctionFact> uses = functions("use");
    ASSERT_EQ(uses.size(), 1U);
    EXPECT_EQ(callees(uses[0].id), (std::vector<std::optional<FunctionId>>{works[0].id}));
}

// Verifies: fr004-record-call-edges-v1 (criterion 1 with ADR-0013: two
// definitions of one name in one translation unit keep their own calls)
TEST_F(CallEdgesTest, TwoDefinitionsInOneTranslationUnitKeepTheirOwnCalls) {
    analyze_gnu_inline();

    const std::vector<FunctionFact> picks = functions("pick");
    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0].usr, "c:@F@pick");
    EXPECT_EQ(picks[1].usr, "pick.c#c:@F@pick");
    EXPECT_EQ(calls(picks[0].id), (std::vector{direct("first", "pick.c", 4, 67)}));
    EXPECT_EQ(calls(picks[1].id), (std::vector{direct("second", "pick.c", 5, 25)}));
}

// Verifies: fr004-record-call-edges-v1 (ADR-0013: a second definition of
// an external function is reported once, and a definition reached from
// several translation units is not)
TEST_F(CallEdgesTest, SecondDefinitionOfAnExternalFunctionIsAWarning) {
    analyze_two_programs();

    EXPECT_EQ(summary().warnings,
              (std::vector<std::string>{
                  "two/report.h:4: function 'report' with external linkage is also defined at "
                  "one/report.c:1; recorded as a separate function (ADR-0013)",
                  "two/main.c:3: function 'work' with external linkage is also defined at "
                  "one/main.c:3; recorded as a separate function (ADR-0013)",
                  "two/main.c:7: function 'main' with external linkage is also defined at "
                  "one/main.c:7; recorded as a separate function (ADR-0013)",
              }));
}

} // namespace
