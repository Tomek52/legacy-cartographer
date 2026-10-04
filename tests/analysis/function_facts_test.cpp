// Verifies: fr003-record-functions-v1 (criteria 1-3, on the two_modules and
// static_same_name fixtures), through the SQLite store.
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
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using namespace lcart::facts;

/// The facts of one function, with its file as a path so expectations read
/// like the fixture's README.
struct FunctionSummary {
    std::string file;
    std::uint32_t line_begin{};
    std::uint32_t line_end{};
    bool is_definition{};
    Linkage linkage{Linkage::external};
    std::string signature;

    bool operator==(const FunctionSummary&) const = default;
};

std::ostream& operator<<(std::ostream& out, const FunctionSummary& function) {
    return out << function.file << ':' << function.line_begin << '-' << function.line_end
               << (function.is_definition ? " definition " : " declaration ")
               << (function.linkage == Linkage::internal ? "internal " : "external ")
               << function.signature;
}

class FunctionFactsTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-functions-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
        auto store = SqliteFactStore::open(":memory:");
        ASSERT_TRUE(store.has_value()) << store.error().message;
        store_ = std::move(*store);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    /// Analyses `files` of the fixture `name`, each compiled with the fixture
    /// root on the include path.
    void analyze(const std::string& name, const std::vector<std::string>& files) {
        root_ = fs::path{LCART_FIXTURES_DIR} / name;
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << "[\n";
            for (std::size_t i = 0; i < files.size(); ++i) {
                out << R"(  {"directory": ")" << root_.string() << R"(", "command": "clang -c -I. )"
                    << files[i] << R"(", "file": ")" << files[i] << "\"}"
                    << (i + 1 < files.size() ? ",\n" : "\n");
            }
            out << "]\n";
        }
        ClangAnalyzer analyzer;
        const auto summary = analyzer.analyze(
            {.compilation_database = database, .root = root_, .exclude_globs = {}}, *store_);
        ASSERT_TRUE(summary.has_value()) << summary.error().message;
        ASSERT_EQ(summary->translation_units_parsed, files.size());
    }

    [[nodiscard]] std::vector<FunctionFact> functions(const std::string& name) const {
        return store_->find_functions(name).value_or(std::vector<FunctionFact>{});
    }

    /// `functions(name)` with file paths; `known_files` are the paths the
    /// functions may be in.
    [[nodiscard]] std::vector<FunctionSummary>
    summaries(const std::string& name, const std::vector<std::string>& known_files) const {
        std::map<FileId, std::string> paths;
        for (const std::string& path : known_files) {
            const auto file = store_->find_file(path).value_or(std::nullopt);
            if (file) {
                paths.emplace(file->id, path);
            }
        }
        std::vector<FunctionSummary> result;
        for (const FunctionFact& function : functions(name)) {
            const auto path = paths.find(function.file);
            result.push_back({.file = path == paths.end() ? "?" : path->second,
                              .line_begin = function.line_begin,
                              .line_end = function.line_end,
                              .is_definition = function.is_definition,
                              .linkage = function.linkage,
                              .signature = function.signature});
        }
        return result;
    }

    void analyze_static_same_name() {
        analyze("static_same_name", {"left/util.c", "right/util.c"});
    }
    void analyze_two_modules() {
        analyze("two_modules", {"app/legacy.c", "app/main.c", "core/core.c"});
    }

private:
    fs::path work_dir_;
    fs::path root_;
    std::unique_ptr<SqliteFactStore> store_;
};

std::vector<std::string> static_same_name_files() {
    return {"shared.h", "left/util.c", "right/util.c"};
}

std::vector<std::string> two_modules_files() {
    return {"core/core.h", "core/core.c", "app/app.h", "app/main.c", "app/legacy.c"};
}

// Verifies: fr003-record-functions-v1 (criterion 1)
TEST_F(FunctionFactsTest, StaticFunctionsWithOneNameInFilesWithOneBaseNameAreTwoFunctions) {
    analyze_static_same_name();

    const std::vector<FunctionFact> helpers = functions("helper");
    ASSERT_EQ(helpers.size(), 2U);
    EXPECT_NE(helpers[0].usr, helpers[1].usr);
    EXPECT_EQ(summaries("helper", static_same_name_files()), (std::vector<FunctionSummary>{
                                                                 {.file = "left/util.c",
                                                                  .line_begin = 3,
                                                                  .line_end = 5,
                                                                  .is_definition = true,
                                                                  .linkage = Linkage::internal,
                                                                  .signature = "int (void)"},
                                                                 {.file = "right/util.c",
                                                                  .line_begin = 9,
                                                                  .line_end = 12,
                                                                  .is_definition = true,
                                                                  .linkage = Linkage::internal,
                                                                  .signature = "int (void)"},
                                                             }));
}

// Verifies: fr003-record-functions-v1 (criteria 2 and 3)
TEST_F(FunctionFactsTest, FunctionDeclaredInHeaderIsOneFunctionAtItsDefinition) {
    analyze_two_modules();

    const std::vector<FunctionFact> core_length = functions("core_length");
    ASSERT_EQ(core_length.size(), 1U);
    EXPECT_EQ(core_length[0].usr, "c:@F@core_length");
    EXPECT_EQ(core_length[0].qualified_name, "core_length");
    EXPECT_EQ(summaries("core_length", two_modules_files()),
              (std::vector<FunctionSummary>{{.file = "core/core.c",
                                             .line_begin = 5,
                                             .line_end = 7,
                                             .is_definition = true,
                                             .linkage = Linkage::external,
                                             .signature = "size_t (const char *)"}}));
    EXPECT_EQ(summaries("app_run", two_modules_files()),
              (std::vector<FunctionSummary>{{.file = "app/main.c",
                                             .line_begin = 5,
                                             .line_end = 7,
                                             .is_definition = true,
                                             .linkage = Linkage::external,
                                             .signature = "int (const char *)"}}));
}

// Verifies: fr003-record-functions-v1 (criterion 3)
TEST_F(FunctionFactsTest, LineRangesOfDefinitionsMatchTheFixture) {
    analyze_two_modules();

    EXPECT_EQ(summaries("main", two_modules_files()),
              (std::vector<FunctionSummary>{{.file = "app/main.c",
                                             .line_begin = 9,
                                             .line_end = 11,
                                             .is_definition = true,
                                             .linkage = Linkage::external,
                                             .signature = "int (void)"}}));
    EXPECT_EQ(summaries("legacy_run", two_modules_files()),
              (std::vector<FunctionSummary>{{.file = "app/legacy.c",
                                             .line_begin = 3,
                                             .line_end = 5,
                                             .is_definition = true,
                                             .linkage = Linkage::external,
                                             .signature = "int (void)"}}));
}

// Verifies: fr003-record-functions-v1 (a declaration without a definition
// is recorded at its declaration)
TEST_F(FunctionFactsTest, RecordsFunctionThatIsOnlyDeclared) {
    analyze_static_same_name();

    EXPECT_EQ(summaries("library_only", static_same_name_files()),
              (std::vector<FunctionSummary>{{.file = "shared.h",
                                             .line_begin = 6,
                                             .line_end = 6,
                                             .is_definition = false,
                                             .linkage = Linkage::external,
                                             .signature = "int (int, ...)"}}));
}

// Verifies: fr003-record-functions-v1 (only functions in files under the
// analysis root)
TEST_F(FunctionFactsTest, FunctionsDeclaredOutsideTheRootAreNotRecorded) {
    analyze_two_modules();

    EXPECT_TRUE(functions("printf").empty());
    EXPECT_TRUE(functions("strlen").empty());
}

} // namespace
