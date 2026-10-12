// Verifies: security-malicious-build-input-v1 (T5: code-loading compiler arguments are removed
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

// clang-cl passes the value of each `/clang:` or `-clang:` argument to the
// driver as a separate argument list (LLVM 20.1.2 clang/lib/Driver/Driver.cpp,
// "In CL mode, look for any pass-through arguments"; ADR-0021).
TEST(ArgumentFilter, RemovesClangClPassThroughPluginOptions) {
    const auto result = remove_code_loading_arguments(
        {"clang-cl", "/c", "/clang:-fplugin=/p/a.so", "/DX", "-clang:-fpass-plugin=/p/b.so",
         "/clang:-fplugin-arg-a-verbose", "/clang:@flags.rsp", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/c", "/DX", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:-fplugin=/p/a.so", "-clang:-fpass-plugin=/p/b.so",
                                    "/clang:-fplugin-arg-a-verbose", "/clang:@flags.rsp"}));
}

TEST(ArgumentFilter, RemovesXclangLoadPassedThroughClangCl) {
    // The pass-through list is -Xclang -load -Xclang /p/a.so; /DX between
    // its parts is not in it.
    const auto result =
        remove_code_loading_arguments({"clang-cl", "/clang:-Xclang", "/clang:-load", "/DX",
                                       "/clang:-Xclang", "/clang:/p/a.so", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/DX", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:-Xclang /clang:-load /clang:-Xclang /clang:/p/a.so"}));
}

TEST(ArgumentFilter, KeepsHarmlessClangClPassThroughOptions) {
    const Args args{"clang-cl", "/clang:-fno-delayed-template-parsing", "/clang:-Xclang",
                    "/clang:-fno-validate-pch", "a.cpp"};
    EXPECT_EQ(remove_code_loading_arguments(args).arguments, args);
}

TEST(ArgumentFilter, RemovesTheXclangWhoseValueIsRemoved) {
    // -Xclang forwards the pass-through argument as it stands; left alone
    // once that is removed, it would forward the next argument (or whatever
    // an adjuster appends).
    for (const Args& tail : {Args{}, Args{"-DFOO", "a.cpp"}}) {
        Args args{"clang-cl", "-Xclang", "/clang:-fplugin=/p/a.so"};
        args.insert(args.end(), tail.begin(), tail.end());
        Args kept{"clang-cl"};
        kept.insert(kept.end(), tail.begin(), tail.end());
        const auto result = remove_code_loading_arguments(args);
        EXPECT_EQ(result.arguments, kept);
        EXPECT_EQ(result.removed, (Args{"-Xclang /clang:-fplugin=/p/a.so"}));
    }
}

TEST(ArgumentFilter, RemovesTheForwarderOfARemovedPassThroughValue) {
    const auto result = remove_code_loading_arguments(
        {"clang-cl", "/clang:-Xclang", "/clang:-fplugin=/p/a.so", "/clang:-DFOO", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/clang:-DFOO", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:-Xclang /clang:-fplugin=/p/a.so"}));
}

// The driver copies the values of -Wp, and -Xpreprocessor into the cc1
// command unchanged (LLVM 20.1.2 clang/lib/Driver/ToolChains/Clang.cpp line
// 1236): `clang-20 -### -c a.c -Wp,-load,/p/a.so` emits "-load" "/p/a.so".
TEST(ArgumentFilter, RemovesWpArgumentsWithCc1PluginOptions) {
    const auto result = remove_code_loading_arguments(
        {"clang", "-Wp,-load,/p/a.so", "-Wp,-DX,-UY", "-Wp,-MD,deps.d", "-Wp,-DX,-plugin,p",
         "-Wp,-fpass-plugin=/p/b.so", "-Wp,-plugin-arg-p,v", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-Wp,-DX,-UY", "-Wp,-MD,deps.d", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"-Wp,-load,/p/a.so", "-Wp,-DX,-plugin,p",
                                    "-Wp,-fpass-plugin=/p/b.so", "-Wp,-plugin-arg-p,v"}));
}

TEST(ArgumentFilter, RemovesWpPluginOptionsPassedThroughClangCl) {
    const auto result =
        remove_code_loading_arguments({"clang-cl", "/clang:-Wp,-load,/p/a.so", "/DX", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/DX", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:-Wp,-load,/p/a.so"}));
}

TEST(ArgumentFilter, RemovesXpreprocessorLoadWithItsValue) {
    const auto result =
        remove_code_loading_arguments({"clang", "-Xpreprocessor", "-load", "-Xpreprocessor",
                                       "/p/a.so", "-Xpreprocessor", "-DX", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-Xpreprocessor", "-DX", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"-Xpreprocessor -load -Xpreprocessor /p/a.so"}));
}

TEST(ArgumentFilter, RemovesXpreprocessorPluginOptionsPassedThroughClangCl) {
    const auto result =
        remove_code_loading_arguments({"clang-cl", "/clang:-Xpreprocessor", "/clang:-add-plugin",
                                       "/clang:-Xpreprocessor", "/clang:p", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "a.cpp"}));
    EXPECT_EQ(result.removed.size(), 1U);
}

// --- Clang configuration files (--config), docs/security.md T5 ---
// The driver reads them and their arguments bypass this filter unless they
// are removed here too; a default clang.cfg is stopped by --no-default-config
// in the recorder (ADR-0021, ADR-0023).

TEST(ArgumentFilter, RemovesConfigEqualsForm) {
    const auto result =
        remove_code_loading_arguments({"clang", "-c", "--config=/etc/evil.cfg", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-c", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"--config=/etc/evil.cfg"}));
    ASSERT_EQ(result.reasons.size(), 1U);
    EXPECT_EQ(result.reasons[0], "reads a configuration file");
}

TEST(ArgumentFilter, RemovesConfigSeparateFormWithItsValue) {
    const auto result =
        remove_code_loading_arguments({"clang", "-c", "--config", "/etc/evil.cfg", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-c", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"--config /etc/evil.cfg"}));
    ASSERT_EQ(result.reasons.size(), 1U);
    EXPECT_EQ(result.reasons[0], "reads a configuration file");
}

TEST(ArgumentFilter, KeepsSurroundingArgumentsInOrderAroundConfig) {
    const auto result =
        remove_code_loading_arguments({"clang", "-Iinc", "--config", "/etc/evil.cfg", "-DX=1",
                                       "--config=/etc/other.cfg", "-O2", "a.c"});
    EXPECT_EQ(result.arguments, (Args{"clang", "-Iinc", "-DX=1", "-O2", "a.c"}));
    EXPECT_EQ(result.removed, (Args{"--config /etc/evil.cfg", "--config=/etc/other.cfg"}));
}

TEST(ArgumentFilter, RemovesConfigPassedThroughClangCl) {
    const auto result = remove_code_loading_arguments(
        {"clang-cl", "/c", "/clang:--config=/etc/evil.cfg", "/DX", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/c", "/DX", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:--config=/etc/evil.cfg"}));
    ASSERT_EQ(result.reasons.size(), 1U);
    EXPECT_EQ(result.reasons[0], "reads a configuration file");
}

TEST(ArgumentFilter, RemovesConfigSeparateFormPassedThroughClangCl) {
    const auto result = remove_code_loading_arguments(
        {"clang-cl", "/c", "/clang:--config", "/clang:/etc/evil.cfg", "a.cpp"});
    EXPECT_EQ(result.arguments, (Args{"clang-cl", "/c", "a.cpp"}));
    EXPECT_EQ(result.removed, (Args{"/clang:--config /clang:/etc/evil.cfg"}));
}

TEST(ArgumentFilter, KeepsOptionsThatMerelyLookLikeConfig) {
    // -config (one dash) is not an option; --config-foo, the config-dir
    // options and a plain file named "config" are not --config and must be
    // kept.
    const Args args{"clang",
                    "-c",
                    "-config",
                    "ignored.cfg",
                    "--config-foo",
                    "--config-system-dir=/d",
                    "--config-user-dir=/u",
                    "-multi-lib-config=/m",
                    "config",
                    "a.c"};
    const auto result = remove_code_loading_arguments(args);
    EXPECT_EQ(result.arguments, args);
    EXPECT_TRUE(result.removed.empty());
}

TEST(ArgumentFilter, PluginRemovalsKeepTheCodeLoadingReason) {
    const auto result = remove_code_loading_arguments({"clang", "-fplugin=/p/a.so", "a.c"});
    ASSERT_EQ(result.reasons.size(), 1U);
    EXPECT_EQ(result.reasons[0], "loads code into the analyser");
}

} // namespace
