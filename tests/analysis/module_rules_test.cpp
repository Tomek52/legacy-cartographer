// Verifies: fr005-map-files-to-modules-v1 (the default for a file matching
// no rule; rules themselves arrive with B-11).
#include "analysis/module_rules.hpp"

#include <gtest/gtest.h>

namespace {

using lcart::analysis::default_module;

TEST(DefaultModule, IsTheTopLevelDirectory) {
    EXPECT_EQ(default_module("core/core.c"), "core");
    EXPECT_EQ(default_module("src/rtu/modbus-rtu.c"), "src");
}

TEST(DefaultModule, FilesDirectlyInTheRootBelongToRoot) {
    EXPECT_EQ(default_module("main.c"), "(root)");
}

} // namespace
