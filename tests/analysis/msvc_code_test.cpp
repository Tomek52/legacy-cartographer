// Verifies: nfr013-linux-and-wsl2-v1 (criterion 1) and
// b17-msvc-code-spike-v2 (criterion 1): code written for MSVC parses with
// the MSVC target, from a `clang` driver command or a clang-cl command, and
// yields its functions and calls (msvc_constructs fixture, ADR-0021).
#include "analysis/clang_analyzer.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::AnalysisSummary;
using lcart::analysis::ClangAnalyzer;
using namespace lcart::facts;

/// A direct call as (callee name, line, column), so expectations read like
/// the fixture's README.
using Call = std::tuple<std::string, std::uint32_t, std::uint32_t>;

fs::path fixture_dir() {
    return fs::path{LCART_FIXTURES_DIR} / "msvc_constructs";
}

/// The flags ADR-0021 names for a `clang` driver command; the target alone
/// implies the three others (LLVM 20.1.2 clang/lib/Driver/ToolChains/Clang.cpp).
constexpr std::string_view msvc_driver_flags =
    "--target=x86_64-pc-windows-msvc -fms-extensions "
    "-fms-compatibility -fms-compatibility-version=19.33";

class MsvcCodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-msvc-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    [[nodiscard]] const fs::path& work_dir() const { return work_dir_; }

    /// Analyses `file` under `root` into a new store, with one compilation
    /// database entry whose "command" is `command` followed by the file name.
    [[nodiscard]] AnalysisSummary analyze(const std::string& command,
                                          const std::string& file = "api.cc",
                                          const fs::path& root = fixture_dir()) {
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << R"([{"directory": ")" << root.string() << R"(", "command": ")" << command << ' '
                << file << R"(", "file": ")" << file << "\"}]\n";
        }
        return analyze_database(database, root);
    }

    /// Analyses the translation units of an existing compilation database.
    [[nodiscard]] AnalysisSummary analyze_database(const fs::path& database, const fs::path& root) {
        auto store = SqliteFactStore::open(":memory:");
        if (!store) {
            ADD_FAILURE() << store.error().message;
            return {};
        }
        store_ = std::move(*store);
        ClangAnalyzer analyzer;
        auto summary = analyzer.analyze({.compilation_database = database,
                                         .root = root,
                                         .exclude_globs = {},
                                         .module_rules = {}},
                                        *store_);
        if (!summary) {
            ADD_FAILURE() << summary.error().message;
            return {};
        }
        return std::move(*summary);
    }

    /// The direct calls from the only function named `caller`.
    [[nodiscard]] std::vector<Call> calls(const std::string& caller) const {
        const auto found = store_->find_functions(caller);
        if (!found || found->size() != 1) {
            ADD_FAILURE() << "expected one function named " << caller;
            return {};
        }
        const auto edges = store_->calls_from(found->front().id);
        if (!edges) {
            ADD_FAILURE() << edges.error().message;
            return {};
        }
        std::vector<Call> result;
        for (const CallFact& edge : *edges) {
            std::string callee = "?";
            if (edge.callee) {
                if (const auto function = store_->function(*edge.callee); function && *function) {
                    callee = (*function)->name;
                }
            }
            result.emplace_back(callee, edge.site.line, edge.site.column);
        }
        return result;
    }

    /// Fails with each stored diagnostic, so a failure shows what Clang said.
    void expect_no_stored_diagnostics() const {
        const auto diagnostics = store_->diagnostics();
        ASSERT_TRUE(diagnostics.has_value());
        for (const DiagnosticFact& diagnostic : *diagnostics) {
            ADD_FAILURE() << diagnostic.file << ':' << diagnostic.line << ": "
                          << diagnostic.message;
        }
    }

    /// Facts of api.cc as the fixture's README describes them.
    void expect_api_facts(const AnalysisSummary& summary) const {
        EXPECT_EQ(summary.translation_units_parsed, 1U);
        EXPECT_EQ(summary.diagnostics, 0U);
        expect_no_stored_diagnostics();
        EXPECT_EQ(calls("checksum"), (std::vector<Call>{{"mix", 15, 15}, {"finish", 17, 12}}));
        EXPECT_EQ(calls("report"), (std::vector<Call>{{"finish", 21, 86}}));
        EXPECT_EQ(calls("mix"), std::vector<Call>{});
        EXPECT_EQ(calls("finish"), std::vector<Call>{});
    }

    [[nodiscard]] const SqliteFactStore& store() const { return *store_; }

private:
    fs::path work_dir_;
    std::unique_ptr<SqliteFactStore> store_;
};

TEST_F(MsvcCodeTest, LinuxTargetRejectsTheFixture) {
    // The control: without the MSVC target the fixture does not parse, so
    // the tests below show what the MSVC settings do.
    const AnalysisSummary summary = analyze("clang -c -DAPI_EXPORTS -I.");
    EXPECT_EQ(summary.translation_units_parsed, 0U);
    EXPECT_GT(summary.diagnostics, 0U);
}

TEST_F(MsvcCodeTest, ParsesWithTheClangDriverAndTheMsvcTarget) {
    expect_api_facts(analyze("clang -c " + std::string{msvc_driver_flags} + " -DAPI_EXPORTS -I."));
}

TEST_F(MsvcCodeTest, ParsesAClangClCommand) {
    expect_api_facts(
        analyze("clang-cl /c /nologo /W4 /DAPI_EXPORTS /I. /std:c++17 /EHsc /Foapi.obj"));
}

TEST_F(MsvcCodeTest, ParsesAClExeCommand) {
    expect_api_facts(analyze("cl.exe /c /nologo /DAPI_EXPORTS /I."));
}

TEST_F(MsvcCodeTest, DashSpelledClOptionsReachTheDriver) {
    // LibTooling's dependency-file adjuster drops every -M... argument
    // unless the command names --driver-mode=cl, which a command run as
    // cl.exe or clang-cl does not (ArgumentsAdjusters.cpp, LLVM 20.1.2);
    // -MD is cl's /MD and defines _DLL (ADR-0021).
    for (const char* compiler : {"cl.exe", "clang-cl"}) {
        const AnalysisSummary summary =
            analyze(std::string{compiler} + " /c -MD /DAPI_EXPORTS /I.", "runtime.cc");
        EXPECT_EQ(summary.translation_units_parsed, 1U) << compiler;
        EXPECT_EQ(summary.diagnostics, 0U) << compiler;
        EXPECT_EQ(calls("report_runtime"),
                  (std::vector<Call>{{"report", 8, 43}, {"checksum", 8, 50}}))
            << compiler;
    }
}

// Verifies: security-malicious-build-input-v1 (T5: clang-cl's /clang: form)
TEST_F(MsvcCodeTest, RemovesPluginOptionsPassedThroughClangCl) {
    const AnalysisSummary summary =
        analyze("clang-cl /c /clang:-fplugin=/nonexistent/evil.so /DAPI_EXPORTS /I.");
    EXPECT_EQ(summary.translation_units_parsed, 1U);
    ASSERT_EQ(summary.warnings.size(), 1U);
    EXPECT_NE(summary.warnings[0].find("/clang:-fplugin=/nonexistent/evil.so"), std::string::npos);
}

// Verifies: nfr013-linux-and-wsl2-v1 (criterion 2, path relativity only: a
// root whose path looks like a WSL2 drive mount, on the Linux file system;
// a root on a real drvfs mount was tried by hand on 2026-10-11, ADR-0022,
// and the facts were the same)
TEST_F(MsvcCodeTest, StoresPathsRelativeToARootUnderMnt) {
    const fs::path root = work_dir() / "mnt" / "c" / "src" / "project";
    fs::create_directories(root);
    for (const char* name : {"api.h", "api.cc"}) {
        fs::copy_file(fixture_dir() / name, root / name);
    }
    expect_api_facts(analyze("clang-cl /c /DAPI_EXPORTS /I.", "api.cc", root));
    const auto files = store().files();
    ASSERT_TRUE(files.has_value());
    std::vector<std::string> paths;
    for (const FileFact& file : *files) {
        if (!file.is_external) {
            paths.push_back(file.path);
        }
    }
    EXPECT_EQ(paths, (std::vector<std::string>{"api.cc", "api.h"}));
}

// The database that tools/msbuild-compdb/CompDbLogger.cs wrote for the
// msbuild_solution fixture on Windows (tests/fixtures/msbuild_solution/
// compile_commands.golden.json), with the paths of that machine replaced
// by placeholders. The Windows SDK and the MSVC library are not available
// here: the test puts small stand-ins at the directories the database names
// with /imsvc, so it checks how lcart reads the database, not the SDK.
class MsbuildDatabaseTest : public MsvcCodeTest {
protected:
    static fs::path solution_dir() { return fixture_dir().parent_path() / "msbuild_solution"; }

    /// Writes the golden database with `drop_system_includes` as asked, the
    /// placeholders replaced, and the stand-in headers in place.
    [[nodiscard]] fs::path write_database(bool drop_system_includes) const {
        const fs::path stand_ins = work_dir() / "windows";
        const fs::path msvc_include = stand_ins / "msvc" / "include";
        const fs::path sdk_um = stand_ins / "sdk" / "um";
        fs::create_directories(msvc_include);
        fs::create_directories(sdk_um);
        write_file(msvc_include / "cstdio", "namespace std { int printf(const char*, ...); }\n");
        write_file(sdk_um / "windows.h", "void __stdcall OutputDebugStringA(const char*);\n"
                                         "unsigned __int64 __stdcall GetTickCount64(void);\n");

        std::string text = read_file(solution_dir() / "compile_commands.golden.json");
        if (drop_system_includes) {
            // Remove each pair `"/imsvc", "<dir>", `.
            for (std::size_t at = text.find(R"("/imsvc", ")"); at != std::string::npos;
                 at = text.find(R"("/imsvc", ")")) {
                const std::size_t closing_quote = text.find("\", ", at + 11);
                text.erase(at, closing_quote + 3 - at);
            }
        }
        replace_all(text, "@ROOT@", solution_dir().string());
        replace_all(text, "@MSVC@", (stand_ins / "msvc").string());
        replace_all(text, "@SDK@", (stand_ins / "sdk").string());
        replace_all(text, "@VS@", (stand_ins / "vs").string());
        const fs::path database = work_dir() / "compile_commands.json";
        write_file(database, text);
        return database;
    }

    /// The stored files, split into those under the root (root-relative
    /// paths) and external ones (file names: the store keeps their absolute
    /// path, the export shows the spelling of the #include).
    struct StoredFiles {
        std::vector<std::string> own;
        std::vector<std::string> external;
    };
    [[nodiscard]] StoredFiles stored_files() const {
        StoredFiles result;
        const auto files = store().files();
        if (!files) {
            ADD_FAILURE() << files.error().message;
            return result;
        }
        for (const FileFact& file : *files) {
            if (file.is_external) {
                result.external.push_back(fs::path{file.path}.filename().string());
            } else {
                result.own.push_back(file.path);
            }
        }
        return result;
    }

private:
    static void write_file(const fs::path& path, const std::string& text) {
        std::ofstream{path} << text;
    }
    static std::string read_file(const fs::path& path) {
        std::ifstream in{path};
        return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }
    static void replace_all(std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t at = text.find(from); at != std::string::npos;
             at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    }
};

// Verifies: nfr013-linux-and-wsl2-v1 (criterion 3, the part that runs
// without Windows: lcart reads what the MSBuild logger writes; the logger
// itself is checked on Windows by tools/msbuild-compdb/verify.ps1, ADR-0022)
TEST_F(MsbuildDatabaseTest, AnalysesTheDatabaseOfAnMsbuildSolution) {
    const AnalysisSummary summary = analyze_database(write_database(false), solution_dir());
    // Both translation units parse: app/main.cc only with the project's
    // APP_GREETING definition (a quoted value with spaces, written by the
    // logger as "/D", "APP_GREETING=\"hello from app\""), core/core.cc with
    // <windows.h> from the directory named by /imsvc.
    EXPECT_EQ(summary.translation_units_parsed, 2U);
    EXPECT_EQ(summary.diagnostics, 0U);
    expect_no_stored_diagnostics();
    EXPECT_EQ(calls("main"), (std::vector<Call>{{"core_stamp", 11, 10}}));
    EXPECT_EQ(calls("core_stamp"), (std::vector<Call>{{"core_log", 14, 3}, {"core_mix", 15, 10}}));

    const StoredFiles files = stored_files();
    // No drive letter and no absolute path in the facts.
    EXPECT_EQ(files.own, (std::vector<std::string>{"app/main.cc", "core/core.cc", "core/core.h"}));
    EXPECT_EQ(files.external, (std::vector<std::string>{"cstdio", "windows.h"}));
}

// The control: without the /imsvc entries the system headers are found only
// through the environment of the shell that runs lcart, so the database
// must name them (ADR-0021, ADR-0022).
TEST_F(MsbuildDatabaseTest, SystemIncludeDirectoriesMustBeInTheDatabase) {
    const AnalysisSummary summary = analyze_database(write_database(true), solution_dir());
    EXPECT_EQ(summary.translation_units_parsed, 0U);
    EXPECT_EQ(summary.diagnostics, 2U);
}

} // namespace
