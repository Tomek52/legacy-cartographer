// Verifies: fr005-map-files-to-modules-v1 (criterion 2: the first matching
// rule wins, overlapping globs; the default for a file matching no rule).
#include "analysis/module_rules.hpp"
#include "config/path_glob.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>

namespace {

using lcart::analysis::default_module;
using lcart::analysis::module_of;
using lcart::analysis::ModuleRules;
using lcart::config::ModuleRule;
using lcart::config::PathGlob;

[[nodiscard]] ModuleRule rule(std::string name, std::initializer_list<std::string_view> globs) {
    ModuleRule out{.name = std::move(name), .paths = {}};
    for (const std::string_view glob : globs) {
        out.paths.push_back(PathGlob::parse(glob).value());
    }
    return out;
}

TEST(DefaultModule, IsTheTopLevelDirectory) {
    EXPECT_EQ(default_module("core/core.c"), "core");
    EXPECT_EQ(default_module("src/rtu/modbus-rtu.c"), "src");
}

TEST(DefaultModule, FilesDirectlyInTheRootBelongToRoot) {
    EXPECT_EQ(default_module("main.c"), "(root)");
}

TEST(ModuleOf, WithoutRulesIsTheDefault) {
    EXPECT_EQ(module_of("src/modbus.c", {}), "src");
    EXPECT_EQ(module_of("configure.ac", {}), "(root)");
}

TEST(ModuleOf, TheFirstRuleWithAMatchingGlobWins) {
    // The libmodbus rules of ADR-0008: `src/modbus*.h` would also match
    // `src/modbus-rtu.h`, but `rtu` comes first.
    const ModuleRules rules{rule("rtu", {"src/modbus-rtu*"}), rule("tcp", {"src/modbus-tcp*"}),
                            rule("core", {"src/modbus.c", "src/modbus-data.c", "src/modbus*.h"})};
    EXPECT_EQ(module_of("src/modbus-rtu.h", rules), "rtu");
    EXPECT_EQ(module_of("src/modbus-tcp-private.h", rules), "tcp");
    EXPECT_EQ(module_of("src/modbus.h", rules), "core");
    EXPECT_EQ(module_of("src/modbus-data.c", rules), "core");
    EXPECT_EQ(module_of("src/win32/modbus.dll.manifest.in", rules), "src");
    EXPECT_EQ(module_of("tests/unit-test-server.c", rules), "tests");

    // The same globs in the other order: the broad rule now takes the headers.
    const ModuleRules reversed{rule("core", {"src/modbus*.h"}), rule("rtu", {"src/modbus-rtu*"})};
    EXPECT_EQ(module_of("src/modbus-rtu.h", reversed), "core");
    EXPECT_EQ(module_of("src/modbus-rtu.c", reversed), "rtu");
}

TEST(ModuleOf, AnyGlobOfARuleMatches) {
    const ModuleRules rules{rule("headers", {"include/**", "**/*.h"})};
    EXPECT_EQ(module_of("include/a/b.inc", rules), "headers");
    EXPECT_EQ(module_of("src/x.h", rules), "headers");
    EXPECT_EQ(module_of("src/x.c", rules), "src");
}

} // namespace
