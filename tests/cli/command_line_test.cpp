// Verifies: fr006-query-and-export-facts-v1 (the `lcart query` commands and
// their arguments) and fr001-ingest-compilation-database-v1 (`lcart analyze`
// options), command-line surface of ADR-0016.
#include "cli/command_line.hpp"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace lcart::cli;
using lcart::common::ErrorCode;

lcart::common::Result<Command> parse(std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> list{arguments};
    return parse_command_line(list);
}

template <typename T>
T parsed_as(std::initializer_list<std::string_view> arguments) {
    const auto command = parse(arguments);
    EXPECT_TRUE(command.has_value()) << (command ? "" : command.error().message);
    if (!command || !std::holds_alternative<T>(*command)) {
        ADD_FAILURE() << "parsed as another command";
        return T{};
    }
    return std::get<T>(*command);
}

void expect_usage_error(std::initializer_list<std::string_view> arguments,
                        std::string_view message_part) {
    const auto command = parse(arguments);
    ASSERT_FALSE(command.has_value());
    EXPECT_EQ(command.error().code, ErrorCode::invalid_argument);
    EXPECT_NE(command.error().message.find(message_part), std::string::npos)
        << command.error().message;
}

TEST(CommandLine, HelpAndVersion) {
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(*parse({"--help"})));
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(*parse({"-h"})));
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(*parse({"query", "--help"})));
    EXPECT_TRUE(std::holds_alternative<VersionCommand>(*parse({"--version"})));
    EXPECT_NE(usage().find("lcart query"), std::string_view::npos);
}

TEST(CommandLine, NoCommandIsAnError) {
    expect_usage_error({}, "command");
}

TEST(CommandLine, AnalyzeDefaults) {
    const auto analyze = parsed_as<AnalyzeCommand>({"analyze"});
    EXPECT_EQ(analyze.config, std::nullopt);
    EXPECT_EQ(analyze.compilation_database, std::nullopt);
    EXPECT_EQ(analyze.root, std::nullopt);
    EXPECT_EQ(analyze.database, "lcart-out/facts.lcart.db");
    EXPECT_FALSE(analyze.replace_database);
}

TEST(CommandLine, AnalyzeOptionsInBothSpellings) {
    const auto analyze =
        parsed_as<AnalyzeCommand>({"analyze", "--config", "proj/lcart.toml",
                                   "--compilation-database=build/compile_commands.json", "--root",
                                   "src", "--db=out/x.lcart.db", "--force"});
    EXPECT_EQ(analyze.config, "proj/lcart.toml");
    EXPECT_EQ(analyze.compilation_database, "build/compile_commands.json");
    EXPECT_EQ(analyze.root, "src");
    EXPECT_EQ(analyze.database, "out/x.lcart.db");
    EXPECT_TRUE(analyze.replace_database);
}

TEST(CommandLine, AnalyzeRejectsBadOptions) {
    expect_usage_error({"analyze", "--frobnicate"}, "--frobnicate");
    expect_usage_error({"analyze", "--db"}, "--db");
    expect_usage_error({"analyze", "--db", "a", "--db", "b"}, "twice");
    expect_usage_error({"analyze", "extra"}, "extra");
    expect_usage_error({"analyze", "--force=yes"}, "--force");
    expect_usage_error({"analyze", "--db="}, "--db");
}

// Consent switches are never read from anywhere but the command line, and
// analyze has none (ADR-0008); the parser must not accept unknown ones.
TEST(CommandLine, AnalyzeHasNoIncludeSourceSwitch) {
    expect_usage_error({"analyze", "--include-source"}, "--include-source");
}

TEST(CommandLine, QueryKindsWithSubjects) {
    struct Kind {
        std::string_view word;
        QueryKind kind;
    };
    const std::array kinds{Kind{.word = "callers", .kind = QueryKind::callers},
                           Kind{.word = "callees", .kind = QueryKind::callees},
                           Kind{.word = "includes", .kind = QueryKind::includes},
                           Kind{.word = "included-by", .kind = QueryKind::included_by}};
    for (const auto& [word, kind] : kinds) {
        const auto query = parsed_as<QueryCommand>({"query", word, "subject"});
        EXPECT_EQ(query.kind, kind) << word;
        EXPECT_EQ(query.subject, "subject") << word;
        EXPECT_EQ(query.database, "lcart-out/facts.lcart.db") << word;
        expect_usage_error({"query", word}, word);
        expect_usage_error({"query", word, "a", "b"}, "b");
    }
}

TEST(CommandLine, QueryModuleDepsSubjectIsOptional) {
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "module-deps"}).subject, "");
    const auto query = parsed_as<QueryCommand>({"query", "module-deps", "core", "--db", "f.db"});
    EXPECT_EQ(query.kind, QueryKind::module_deps);
    EXPECT_EQ(query.subject, "core");
    EXPECT_EQ(query.database, "f.db");
}

TEST(CommandLine, QueryExport) {
    const auto to_stdout = parsed_as<QueryCommand>({"query", "export"});
    EXPECT_EQ(to_stdout.kind, QueryKind::export_facts);
    EXPECT_EQ(to_stdout.output, std::nullopt);
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "export", "--output", "x.jsonl"}).output,
              "x.jsonl");
    expect_usage_error({"query", "export", "subject"}, "subject");
    expect_usage_error({"query", "callers", "f", "--output", "x"}, "--output");
}

TEST(CommandLine, DoubleDashEndsOptions) {
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "callers", "--", "--odd"}).subject, "--odd");
}

TEST(CommandLine, UnknownCommandsAndQueries) {
    expect_usage_error({"frobnicate"}, "frobnicate");
    expect_usage_error({"query"}, "query");
    expect_usage_error({"query", "everything"}, "everything");
    // Planned commands are named as such.
    expect_usage_error({"map"}, "not available yet");
}

} // namespace
