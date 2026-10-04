// Verifies: adr0014-path-glob-syntax-v1 (the glob syntax of module rules,
// FR-005).
#include "config/path_glob.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using lcart::common::ErrorCode;
using lcart::config::PathGlob;

[[nodiscard]] bool matches(std::string_view pattern, std::string_view path) {
    auto glob = PathGlob::parse(pattern);
    EXPECT_TRUE(glob.has_value()) << pattern << ": " << glob.error().message;
    return glob.has_value() && glob->matches(path);
}

TEST(PathGlob, LiteralMatchesOnlyTheWholePath) {
    EXPECT_TRUE(matches("src/modbus.c", "src/modbus.c"));
    EXPECT_FALSE(matches("src/modbus.c", "src/modbus.c.orig"));
    EXPECT_FALSE(matches("modbus.c", "src/modbus.c")); // anchored at the root
    EXPECT_FALSE(matches("src/Modbus.c", "src/modbus.c"));
}

TEST(PathGlob, StarStaysWithinOneSegment) {
    EXPECT_TRUE(matches("src/modbus-rtu*", "src/modbus-rtu.c"));
    EXPECT_TRUE(matches("src/modbus-rtu*", "src/modbus-rtu-private.h"));
    EXPECT_TRUE(matches("src/modbus*.h", "src/modbus.h"));
    EXPECT_TRUE(matches("src/*", "src/.hidden"));
    EXPECT_FALSE(matches("src/*", "src/rtu/modbus-rtu.c"));
    EXPECT_FALSE(matches("*.c", "src/modbus.c"));
    EXPECT_TRUE(matches("src/*a*b*", "src/xaybz"));
    EXPECT_FALSE(matches("src/*a*b", "src/xaybz"));
}

TEST(PathGlob, QuestionMarkIsOneCharacterOfASegment) {
    EXPECT_TRUE(matches("src/unit-test?.c", "src/unit-test1.c"));
    EXPECT_FALSE(matches("src/unit-test?.c", "src/unit-test.c"));
    EXPECT_FALSE(matches("src?x.c", "src/x.c"));
}

TEST(PathGlob, DoubleStarMatchesZeroOrMoreSegments) {
    EXPECT_TRUE(matches("**/*.h", "modbus.h"));
    EXPECT_TRUE(matches("**/*.h", "src/win32/config.h"));
    EXPECT_TRUE(matches("src/**/test.c", "src/test.c"));
    EXPECT_TRUE(matches("src/**/test.c", "src/a/b/test.c"));
    EXPECT_TRUE(matches("tests/**", "tests/unit-test-server.c"));
    EXPECT_FALSE(matches("tests/**", "src/tests/x.c"));
    EXPECT_TRUE(matches("**", "any/depth/file.c"));
}

TEST(PathGlob, BracketsMatchOneCharacterOfASet) {
    EXPECT_TRUE(matches("src/modbus-[rt]*.c", "src/modbus-rtu.c"));
    EXPECT_TRUE(matches("src/modbus-[rt]*.c", "src/modbus-tcp.c"));
    EXPECT_FALSE(matches("src/modbus-[rt]*.c", "src/modbus-data.c"));
    EXPECT_TRUE(matches("v[0-9].c", "v7.c"));
    EXPECT_FALSE(matches("v[!0-9].c", "v7.c"));
    EXPECT_TRUE(matches("v[!0-9].c", "vx.c"));
    EXPECT_TRUE(matches("[]]x", "]x")); // `]` first is a member
    EXPECT_TRUE(matches("a[*]", "a*")); // the way to write a literal `*`
    EXPECT_FALSE(matches("a[*]", "ab"));
}

TEST(PathGlob, ManyStarsDoNotBacktrackExponentially) {
    const std::string pattern = "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b";
    const std::string path(4096, 'a');
    EXPECT_FALSE(matches(pattern, path)); // returns at once, or the test times out
}

TEST(PathGlob, RejectsPatternsThatCannotMatchARootRelativePath) {
    for (const std::string_view pattern :
         {"", "/src/x.c", "src//x.c", "src/", "./src/x.c", "src/../x.c", "src\\x.c", "src/a**",
          "**b/x.c", "src/[abc", "src/[!"}) {
        auto glob = PathGlob::parse(pattern);
        ASSERT_FALSE(glob.has_value()) << pattern;
        EXPECT_EQ(glob.error().code, ErrorCode::invalid_argument);
        EXPECT_NE(glob.error().message.find("path glob"), std::string::npos);
    }
}

TEST(PathGlob, KeepsThePatternAsWritten) {
    auto glob = PathGlob::parse("src/modbus-rtu*");
    ASSERT_TRUE(glob.has_value());
    EXPECT_EQ(glob->pattern(), "src/modbus-rtu*");
}

} // namespace
