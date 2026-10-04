// Verifies: b11-module-rules-v2 (criterion 1: rules from lcart.toml, unknown
// keys and consent switches are errors), adr0008-toml-v1,
// adr0008-no-consent-in-config-v1.
#include "config/project_config.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using lcart::common::ErrorCode;
using lcart::config::load_project_config;
using lcart::config::parse_project_config;

constexpr std::string_view libmodbus_config = R"toml(
[project]
name = "libmodbus"
root = "targets/libmodbus"
compilation_database = "build/targets/libmodbus/compile_commands.json"

# First matching rule wins; unmatched files fall back to their directory.
[[modules]]
name = "rtu"
paths = ["src/modbus-rtu*"]

[[modules]]
name = "tcp"
paths = ["src/modbus-tcp*"]

[[modules]]
name = "core"
paths = ["src/modbus.c", "src/modbus-data.c", "src/modbus*.h"]
)toml";

[[nodiscard]] std::vector<std::string> patterns(const lcart::config::ModuleRule& rule) {
    std::vector<std::string> out;
    out.reserve(rule.paths.size());
    for (const auto& glob : rule.paths) {
        out.push_back(glob.pattern());
    }
    return out;
}

/// The error message of parsing `text`, which must fail with `code`.
[[nodiscard]] std::string error_of(std::string_view text, ErrorCode code) {
    auto config = parse_project_config(text, "lcart.toml");
    EXPECT_FALSE(config.has_value()) << text;
    if (config.has_value()) {
        return {};
    }
    EXPECT_EQ(config.error().code, code) << config.error().message;
    return config.error().message;
}

TEST(ProjectConfig, ReadsTheExampleOfAdr0008) {
    auto config = parse_project_config(libmodbus_config, "lcart.toml");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_EQ(config->name, "libmodbus");
    EXPECT_EQ(config->root, "targets/libmodbus");
    EXPECT_EQ(config->compilation_database, "build/targets/libmodbus/compile_commands.json");
    ASSERT_EQ(config->modules.size(), 3U);
    EXPECT_EQ(config->modules[0].name, "rtu");
    EXPECT_EQ(patterns(config->modules[0]), std::vector<std::string>{"src/modbus-rtu*"});
    EXPECT_EQ(config->modules[1].name, "tcp");
    EXPECT_EQ(config->modules[2].name, "core");
    EXPECT_EQ(patterns(config->modules[2]),
              (std::vector<std::string>{"src/modbus.c", "src/modbus-data.c", "src/modbus*.h"}));
}

TEST(ProjectConfig, ReadsExclusionsAsGlobs) {
    auto config = parse_project_config("[project]\nexclude = [\"tests/**\", \"src/win32/*\"]\n",
                                       "lcart.toml");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    ASSERT_EQ(config->exclude.size(), 2U);
    EXPECT_EQ(config->exclude[0].pattern(), "tests/**");
    EXPECT_EQ(config->exclude[1].pattern(), "src/win32/*");
    EXPECT_EQ(error_of("[project]\nexclude = [\"../x\"]\n", ErrorCode::invalid_argument),
              "lcart.toml:2:12: path glob '../x' has a '.' or '..' segment, which a "
              "root-relative path never has");
    EXPECT_EQ(error_of("[project]\nexclude = \"tests/**\"\n", ErrorCode::invalid_argument),
              "lcart.toml:2:11: 'project.exclude' must be an array of strings");
}

TEST(ProjectConfig, EverySectionIsOptional) {
    auto config = parse_project_config("# nothing yet\n", "lcart.toml");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_TRUE(config->name.empty());
    EXPECT_TRUE(config->modules.empty());
}

TEST(ProjectConfig, NotTomlIsAParseErrorWithItsPosition) {
    const std::string message = error_of("[project\nname = 1\n", ErrorCode::parse_error);
    EXPECT_EQ(message.rfind("lcart.toml:1:", 0), 0U) << message;
}

// Found by project_config_fuzzer: toml++ 3.4.0 asserts (and, under NDEBUG,
// assumes) that a table header starts with a key character.
TEST(ProjectConfig, ATableHeaderWithoutAKeyIsAParseError) {
    for (const std::string_view text : {"[=]\n", "[[=]]\n", "[ \x01]\n"}) {
        const std::string message = error_of(text, ErrorCode::parse_error);
        EXPECT_EQ(message.rfind("lcart.toml:1:", 0), 0U) << message;
    }
}

// Verifies: adr0015-ascii-configuration-v1. Found by project_config_fuzzer:
// toml++ 3.4.0 reaches __builtin_unreachable() for U+030C where it expects
// whitespace.
TEST(ProjectConfig, NonAsciiTextIsAnErrorWithItsPosition) {
    EXPECT_EQ(error_of("\xCF\x8C", ErrorCode::parse_error),
              "lcart.toml:1:1: non-ASCII character; lcart.toml must be ASCII (ADR-0015)");
    EXPECT_EQ(error_of("[project]\nname = \"\xC5\xBC\"\n", ErrorCode::parse_error),
              "lcart.toml:2:9: non-ASCII character; lcart.toml must be ASCII (ADR-0015)");
    EXPECT_EQ(error_of("# ok\n# za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87\n", ErrorCode::parse_error),
              "lcart.toml:2:5: non-ASCII character; lcart.toml must be ASCII (ADR-0015)");
}

TEST(ProjectConfig, ALeadingByteOrderMarkIsAllowed) {
    auto config = parse_project_config("\xEF\xBB\xBF[project]\nname = \"x\"\n", "lcart.toml");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_EQ(config->name, "x");
}

// Verifies: adr0015-ascii-configuration-v1 (limits). Found in review:
// toml++ 3.4.0 recurses once per dotted key segment and table level, so a
// deep key overflows the stack.
TEST(ProjectConfig, DeeplyDottedKeysAreErrorsNotACrash) {
    std::string dotted;
    for (int i = 0; i < 100000; ++i) {
        dotted += "a.";
    }
    for (const std::string& text : {"[project]\nname = \"x\"\n" + dotted + "a = 1\n",
                                    "[" + dotted + "a]\n", "x = { " + dotted + "a = 1 }\n"}) {
        const std::string message = error_of(text, ErrorCode::parse_error);
        EXPECT_NE(message.find("more than 32 dotted parts"), std::string::npos) << message;
    }
    // Dots in strings, comments and numbers do not count.
    const std::string many_dots(100, '.');
    auto config = parse_project_config("# " + many_dots + "\n[project]\nname = \"" + many_dots +
                                           "\"\nroot = '" + many_dots + "'\n",
                                       "lcart.toml");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_EQ(config->name, many_dots);
    EXPECT_EQ(error_of("[project]\nname = 1.5\n", ErrorCode::invalid_argument),
              "lcart.toml:2:8: 'project.name' must be a string");
}

TEST(ProjectConfig, AFileOverOneMebibyteIsAnError) {
    const std::string text = "# " + std::string(std::size_t{1024} * 1024, 'x') + "\n";
    EXPECT_EQ(error_of(text, ErrorCode::parse_error),
              "lcart.toml: larger than 1 MiB; lcart.toml is a small file (ADR-0015)");
}

TEST(ProjectConfig, UnknownKeysAreErrorsWithTheirPosition) {
    EXPECT_EQ(error_of("[project]\nname = \"x\"\nroots = \"src\"\n", ErrorCode::invalid_argument),
              "lcart.toml:3:1: unknown key 'project.roots'");
    EXPECT_EQ(
        error_of("[[modules]]\nname = \"core\"\npath = [\"src/*\"]\n", ErrorCode::invalid_argument),
        "lcart.toml:3:1: unknown key 'modules.path'");
    EXPECT_EQ(error_of("[output]\nformat = \"json\"\n", ErrorCode::invalid_argument),
              "lcart.toml:1:2: unknown key 'output'");
}

TEST(ProjectConfig, ConsentSwitchesAreRejectedAnywhere) {
    for (const std::string_view text :
         {"include_source = true\n", "include-source = true\n",
          "[project]\ninclude_source = true\n",
          "[[modules]]\nname = \"a\"\npaths = [\"a/*\"]\nallow-remote-source = true\n",
          "allow_remote_source = true\n"}) {
        const std::string message = error_of(text, ErrorCode::invalid_argument);
        EXPECT_NE(message.find("command line"), std::string::npos) << message;
        EXPECT_NE(message.find("ADR-0008"), std::string::npos) << message;
    }
}

TEST(ProjectConfig, ValuesOfTheWrongTypeAreErrors) {
    EXPECT_EQ(error_of("[project]\nname = 3\n", ErrorCode::invalid_argument),
              "lcart.toml:2:8: 'project.name' must be a string");
    EXPECT_EQ(error_of("project = \"libmodbus\"\n", ErrorCode::invalid_argument),
              "lcart.toml:1:11: 'project' must be a table");
    EXPECT_EQ(error_of("[modules]\nname = \"a\"\n", ErrorCode::invalid_argument),
              "lcart.toml:1:1: 'modules' must be an array of tables ([[modules]])");
    EXPECT_EQ(error_of("[[modules]]\nname = \"a\"\npaths = \"a/*\"\n", ErrorCode::invalid_argument),
              "lcart.toml:3:9: 'modules.paths' must be an array of strings");
    EXPECT_EQ(
        error_of("[[modules]]\nname = \"a\"\npaths = [\"a/*\", 1]\n", ErrorCode::invalid_argument),
        "lcart.toml:3:17: 'modules.paths' must be an array of strings");
}

TEST(ProjectConfig, AModuleNeedsANameAndAtLeastOnePath) {
    EXPECT_EQ(error_of("[[modules]]\npaths = [\"a/*\"]\n", ErrorCode::invalid_argument),
              "lcart.toml:1:1: module without 'name'");
    EXPECT_EQ(
        error_of("[[modules]]\nname = \"\"\npaths = [\"a/*\"]\n", ErrorCode::invalid_argument),
        "lcart.toml:2:8: module without 'name'");
    EXPECT_EQ(error_of("[[modules]]\nname = \"a\"\n", ErrorCode::invalid_argument),
              "lcart.toml:1:1: module 'a' without 'paths'");
    EXPECT_EQ(error_of("[[modules]]\nname = \"a\"\npaths = []\n", ErrorCode::invalid_argument),
              "lcart.toml:3:9: module 'a' without 'paths'");
}

TEST(ProjectConfig, AnInvalidGlobIsAnErrorAtItsPosition) {
    EXPECT_EQ(error_of("[[modules]]\nname = \"a\"\npaths = [\"a/*\", \"/abs/*\"]\n",
                       ErrorCode::invalid_argument),
              "lcart.toml:3:17: path glob '/abs/*' must be relative to the analysis root");
}

TEST(ProjectConfig, LoadsAFileAndNamesItInErrors) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lcart-project-config-test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "lcart.toml";
    {
        std::ofstream out{file};
        out << "[project]\nname = \"x\"\nnmae = \"y\"\n";
    }
    auto config = load_project_config(file);
    ASSERT_FALSE(config.has_value());
    EXPECT_EQ(config.error().message, file.string() + ":3:1: unknown key 'project.nmae'");

    auto missing = load_project_config(dir / "missing.toml");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::io_error);

    auto directory = load_project_config(dir); // reading a directory fails in the stream
    ASSERT_FALSE(directory.has_value());
    EXPECT_EQ(directory.error().code, ErrorCode::io_error);
    std::filesystem::remove_all(dir);
}

} // namespace
