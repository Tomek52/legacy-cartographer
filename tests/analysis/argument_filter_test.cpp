// Verifies: docs/security.md T5 (code-loading compiler arguments are removed
// before Clang sees a command). Option names from `clang-20 --help-hidden`
// and `clang-20 -cc1 --help`.
#include "analysis/argument_filter.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using lcart::analysis::remove_code_loading_arguments;
using Args = std::vector<std::string>;

TEST(ArgumentFilter, KeepsOrdinaryArgumentsUnchanged) {
    const Args args{"clang", "-c", "a.c", "-Iinclude", "-DX=1", "-O2", "-o", "a.o"};
    const auto result = remove_code_loading_arguments(args);
    EXPECT_EQ(result.arguments, args);
    EXPECT_TRUE(result.removed.empty());
}

TEST(ArgumentFilter, RemovesDriverPluginOptions) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-fplugin=/p/a.so", "-c", "-fpass-plugin=/p/b.so", "a.c",
         "--hipspv-pass-plugin=/p/c.so", "-fplugin-arg-a-verbose"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-c", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"-fplugin=/p/a.so", "-fpass-plugin=/p/b.so",
                                    "--hipspv-pass-plugin=/p/c.so", "-fplugin-arg-a-verbose"}));
}

TEST(ArgumentFilter, RemovesXclangLoadWithItsValue) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-Xclang", "-load", "-Xclang", "/p/a.so", "-c", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-c", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"-Xclang -load -Xclang /p/a.so"}));
}

TEST(ArgumentFilter, RemovesXclangEqualsForm) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-Xclang=-load", "-Xclang=/p/a.so", "-Xclang=-plugin", "-Xclang=name", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "a.c"}));
    EXPECT_EQ(result.removed.size(), 2U);
}

TEST(ArgumentFilter, RemovesXclangPluginActionsAndArgs) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-Xclang", "-add-plugin", "-Xclang", "p", "-Xclang", "-plugin-arg-p", "-Xclang",
         "v", "-Xclang", "-fpass-plugin=/p/b.so", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "a.c"}));
    EXPECT_EQ(result.removed.size(), 3U);
}

TEST(ArgumentFilter, KeepsHarmlessXclangOptions) {
    const Args args{"clang", "-Xclang", "-fno-validate-pch", "a.c"};
    EXPECT_EQ(remove_code_loading_arguments(args).arguments, args);
}

TEST(ArgumentFilter, RemovesBareCc1PluginOptions) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-cc1", "-load", "/p/a.so", "-plugin", "p", "-plugin-arg-p", "v", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-cc1", "a.c"}));
    EXPECT_EQ(result.removed.size(), 3U);
}

TEST(ArgumentFilter, RemovesUnexpandedResponseFiles) {
    const auto result = remove_code_loading_arguments({"clang", "@flags.rsp", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"@flags.rsp"}));
}

TEST(ArgumentFilter, DanglingXclangAtEndIsRemoved) {
    // A truncated pair could pair with whatever an adjuster appends later.
    const auto result = remove_code_loading_arguments({"clang", "a.c", "-Xclang", "-load"});
    EXPECT_EQ(result.arguments, (Args{"clang", "a.c"}));
    EXPECT_EQ(result.removed.size(), 1U);
}

} // namespace
