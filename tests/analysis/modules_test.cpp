// Verifies: fr005-map-files-to-modules-v1 (criteria 1-3 on the two_modules
// fixture, through the SQLite store) and b11-module-rules-v2 (criterion 1:
// the rules come from lcart.toml).
#include "analysis/clang_analyzer.hpp"
#include "config/project_config.hpp"
#include "facts/sqlite_fact_store.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using lcart::analysis::ClangAnalyzer;
using lcart::analysis::ModuleRules;
using namespace lcart::facts;

/// The files of the two_modules fixture (its README).
std::vector<std::string> fixture_files() {
    return {"app/app.h", "app/legacy.c", "app/main.c", "core/core.c", "core/core.h"};
}

class ModulesTest : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir_ = fs::temp_directory_path() /
                    ("lcart-modules-" +
                     std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(work_dir_);
        fs::create_directories(work_dir_);
        auto store = SqliteFactStore::open(":memory:");
        ASSERT_TRUE(store.has_value()) << store.error().message;
        store_ = std::move(*store);
    }
    void TearDown() override { fs::remove_all(work_dir_); }

    /// Analyses the two_modules fixture with the module rules and exclusions
    /// of `toml`; `analysed` translation units are expected to be parsed.
    void analyze(std::string_view toml, std::uint32_t analysed = 3) {
        auto config = lcart::config::parse_project_config(toml, "lcart.toml");
        ASSERT_TRUE(config.has_value()) << config.error().message;

        const fs::path root = fs::path{LCART_FIXTURES_DIR} / "two_modules";
        const fs::path database = work_dir_ / "compile_commands.json";
        {
            std::ofstream out{database};
            out << "[\n";
            const std::vector<std::string> units{"app/legacy.c", "app/main.c", "core/core.c"};
            for (std::size_t i = 0; i < units.size(); ++i) {
                out << R"(  {"directory": ")" << root.string() << R"(", "command": "clang -c -I. )"
                    << units[i] << R"(", "file": ")" << units[i] << "\"}"
                    << (i + 1 < units.size() ? ",\n" : "\n");
            }
            out << "]\n";
        }
        ClangAnalyzer analyzer;
        auto summary = analyzer.analyze({.compilation_database = database,
                                         .root = root,
                                         .exclude_globs = std::move(config->exclude),
                                         .module_rules = std::move(config->modules)},
                                        *store_);
        ASSERT_TRUE(summary.has_value()) << summary.error().message;
        ASSERT_EQ(summary->translation_units_total, analysed);
        ASSERT_EQ(summary->translation_units_parsed, analysed);
        for (const std::string& path : fixture_files()) {
            if (const std::optional<FileFact> file =
                    store_->find_file(path).value_or(std::nullopt)) {
                paths_.emplace(file->id, path);
            }
        }
    }

    /// Fixture file -> the name of its module.
    [[nodiscard]] std::map<std::string, std::string> modules_of_files() const {
        std::map<std::string, std::string> out;
        for (const std::string& path : fixture_files()) {
            const std::optional<FileFact> file = store_->find_file(path).value_or(std::nullopt);
            out.emplace(path, file && file->module ? module_name(*file->module) : "<none>");
        }
        return out;
    }

    /// Every module dependency of the fixture with its edge, written as
    /// `from -> to: via at file:line[:column]`.
    [[nodiscard]] std::vector<std::string> dependency_edges() const {
        std::vector<std::string> out;
        for (const std::string& module : module_names()) {
            const std::optional<ModuleFact> from =
                store_->find_module(module).value_or(std::nullopt);
            if (!from) {
                continue;
            }
            for (const ModuleDependencyFact& edge :
                 store_->module_dependency_edges(from->id).value_or(
                     std::vector<ModuleDependencyFact>{})) {
                std::string text = module + " -> " + module_name(edge.to) + ": " +
                                   (edge.via == DependencyVia::include ? "include" : "call") +
                                   " at " + paths_.at(edge.site.file) + ':' +
                                   std::to_string(edge.site.line);
                if (edge.via == DependencyVia::call) {
                    text += ':' + std::to_string(edge.site.column);
                }
                out.push_back(std::move(text));
            }
        }
        return out;
    }

    /// Names of the modules `module` depends on, as module_dependencies() returns them.
    [[nodiscard]] std::vector<std::string> dependencies_of(const std::string& module) const {
        std::vector<std::string> out;
        const std::optional<ModuleFact> from = store_->find_module(module).value_or(std::nullopt);
        EXPECT_TRUE(from.has_value()) << module;
        if (from) {
            for (const ModuleFact& to :
                 store_->module_dependencies(from->id).value_or(std::vector<ModuleFact>{})) {
                out.push_back(to.name);
            }
        }
        return out;
    }

    [[nodiscard]] std::size_t functions_named(std::string_view name) const {
        return store_->find_functions(name).value_or(std::vector<FunctionFact>{}).size();
    }

private:
    [[nodiscard]] std::string module_name(ModuleId id) const {
        for (const std::string& name : module_names()) {
            const std::optional<ModuleFact> module =
                store_->find_module(name).value_or(std::nullopt);
            if (module && module->id == id) {
                return name;
            }
        }
        return "<unknown>";
    }
    [[nodiscard]] static std::vector<std::string> module_names() {
        return {"app", "app_lib", "core", "entry", "headers"};
    }

    fs::path work_dir_;
    std::unique_ptr<SqliteFactStore> store_;
    std::map<FileId, std::string> paths_;
};

TEST_F(ModulesTest, WithoutRulesEachTopLevelDirectoryIsAModule) {
    analyze("");
    EXPECT_EQ(modules_of_files(), (std::map<std::string, std::string>{{"app/app.h", "app"},
                                                                      {"app/legacy.c", "app"},
                                                                      {"app/main.c", "app"},
                                                                      {"core/core.c", "core"},
                                                                      {"core/core.h", "core"}}));
    EXPECT_EQ(dependencies_of("app"), std::vector<std::string>{"core"});
    EXPECT_TRUE(dependencies_of("core").empty());
    EXPECT_EQ(dependency_edges(),
              (std::vector<std::string>{"app -> core: include at app/app.h:4",
                                        "app -> core: call at app/main.c:6:28"}));
}

TEST_F(ModulesTest, TheFirstMatchingRuleWinsAndUnmatchedFilesKeepTheDefault) {
    // `app/*` and `**/*.h` both match app/app.h; `app/main.c` matches the
    // first and the second rule. core/core.c matches none.
    analyze(R"toml(
[[modules]]
name = "entry"
paths = ["app/main.c"]

[[modules]]
name = "app_lib"
paths = ["app/*"]

[[modules]]
name = "headers"
paths = ["**/*.h"]
)toml");
    EXPECT_EQ(modules_of_files(), (std::map<std::string, std::string>{{"app/app.h", "app_lib"},
                                                                      {"app/legacy.c", "app_lib"},
                                                                      {"app/main.c", "entry"},
                                                                      {"core/core.c", "core"},
                                                                      {"core/core.h", "headers"}}));
    EXPECT_EQ(dependency_edges(),
              (std::vector<std::string>{
                  "app_lib -> entry: call at app/legacy.c:4:12",
                  "app_lib -> headers: include at app/app.h:4",
                  "core -> headers: include at core/core.c:1",
                  "entry -> app_lib: include at app/main.c:1",
                  // core_length is defined in core/core.c, so the call goes to core.
                  "entry -> core: call at app/main.c:6:28",
              }));
    EXPECT_EQ(dependencies_of("entry"), (std::vector<std::string>{"app_lib", "core"}));
}

// Verifies: b11-module-rules-v2 (exclude_globs share the glob syntax of the
// module rules, as the B-08 result asked).
TEST_F(ModulesTest, ExcludedTranslationUnitsAreNotAnalysed) {
    analyze("[project]\nexclude = [\"app/legacy.*\"]\n", 2);
    EXPECT_EQ(modules_of_files(), (std::map<std::string, std::string>{{"app/app.h", "app"},
                                                                      {"app/legacy.c", "<none>"},
                                                                      {"app/main.c", "app"},
                                                                      {"core/core.c", "core"},
                                                                      {"core/core.h", "core"}}));
    EXPECT_EQ(functions_named("legacy_run"), 0U);
}

// A header matching an exclusion is still recorded when an analysed
// translation unit includes it: exclusions select translation units.
TEST_F(ModulesTest, ExclusionsSelectTranslationUnitsNotHeaders) {
    analyze("[project]\nexclude = [\"core/**\"]\n", 2);
    EXPECT_EQ(modules_of_files().at("core/core.c"), "<none>");
    EXPECT_EQ(modules_of_files().at("core/core.h"), "core");
    EXPECT_EQ(functions_named("core_length"), 1U); // declared in core/core.h only
}

} // namespace
