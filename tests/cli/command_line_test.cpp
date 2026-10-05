// Verifies: fr006-query-and-export-facts-v1 (the `lcart query` commands and
// their arguments), fr001-ingest-compilation-database-v1 (`lcart analyze`
// options) and fr007-module-map-without-llm-v1 (`lcart map` options),
// command-line surface of ADR-0016 and ADR-0017.
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

TEST(CommandLine, AnOptionDoesNotTakeTheNextOptionAsItsValue) {
    expect_usage_error({"analyze", "--db", "--force"}, "--db needs a value");
    expect_usage_error({"query", "export", "--output", "--db", "x"}, "--output needs a value");
    // A value that starts with `-` is given after `=`.
    EXPECT_EQ(parsed_as<AnalyzeCommand>({"analyze", "--db=-odd.db"}).database, "-odd.db");
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

// ADR-0017: `-` alone is a value, standard output for `--output`.
TEST(CommandLine, OutputDashIsStandardOutput) {
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "export", "--output", "-"}).output, std::nullopt);
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "export", "--output=-"}).output, std::nullopt);
    EXPECT_EQ(parsed_as<MapCommand>({"map", "--output", "-"}).output, std::nullopt);
    EXPECT_EQ(parsed_as<MapCommand>({"map", "--output=-"}).output, std::nullopt);
    // Still no value: an option, or nothing.
    expect_usage_error({"map", "--output", "--db", "x"}, "--output needs a value");
    expect_usage_error({"map", "--output"}, "--output needs a value");
}

// ADR-0017: only `--output` takes a lone `-` as its next value; for every
// other option it stays ADR-0016's missing value.
TEST(CommandLine, OnlyOutputTakesALoneDash) {
    expect_usage_error({"analyze", "--db", "-"}, "--db needs a value");
    expect_usage_error({"analyze", "--config", "-"}, "--config needs a value");
    expect_usage_error({"analyze", "--root", "-"}, "--root needs a value");
    expect_usage_error({"analyze", "--compilation-database", "-"},
                       "--compilation-database needs a value");
    expect_usage_error({"query", "export", "--db", "-"}, "--db needs a value");
    expect_usage_error({"map", "--db", "-"}, "--db needs a value");
}

TEST(CommandLine, MapDefaults) {
    const auto map = parsed_as<MapCommand>({"map"});
    EXPECT_EQ(map.database, "lcart-out/facts.lcart.db");
    EXPECT_EQ(map.output, "lcart-out/map.md");
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(*parse({"map", "--help"})));
}

TEST(CommandLine, MapOptionsInBothSpellings) {
    const auto map = parsed_as<MapCommand>({"map", "--db", "x.lcart.db", "--output=docs/map.md"});
    EXPECT_EQ(map.database, "x.lcart.db");
    EXPECT_EQ(map.output, "docs/map.md");
}

TEST(CommandLine, MapRejectsBadArguments) {
    expect_usage_error({"map", "--frobnicate"}, "--frobnicate");
    expect_usage_error({"map", "extra"}, "extra");
    expect_usage_error({"map", "--force"}, "--force");
    expect_usage_error({"map", "--db", "a", "--db", "b"}, "twice");
    expect_usage_error({"map", "--include-source"}, "--include-source");
}

TEST(CommandLine, DoubleDashEndsOptions) {
    EXPECT_EQ(parsed_as<QueryCommand>({"query", "callers", "--", "--odd"}).subject, "--odd");
}

TEST(CommandLine, UnknownCommandsAndQueries) {
    expect_usage_error({"frobnicate"}, "frobnicate");
    expect_usage_error({"query"}, "query");
    expect_usage_error({"query", "everything"}, "everything");
    // Planned commands are named as such.
    expect_usage_error({"handoff"}, "not available yet");
}

// Verifies: b16-resumable-analysis-v2 (the command line of ADR-0019:
// `--jobs` from 1 to 256, command line only; `--resume`; exit status 5).
TEST(CommandLine, AnalyzeJobsAndResume) {
    const auto defaults = parsed_as<AnalyzeCommand>({"analyze"});
    EXPECT_EQ(defaults.jobs, std::nullopt);
    EXPECT_FALSE(defaults.resume);
    EXPECT_FALSE(defaults.replace_database);

    const auto resumed = parsed_as<AnalyzeCommand>({"analyze", "--jobs", "4", "--resume"});
    EXPECT_EQ(resumed.jobs, 4U);
    EXPECT_TRUE(resumed.resume);
    EXPECT_FALSE(resumed.replace_database);
    EXPECT_EQ(parsed_as<AnalyzeCommand>({"analyze", "--jobs=1"}).jobs, 1U);
    EXPECT_EQ(parsed_as<AnalyzeCommand>({"analyze", "--jobs=256", "--force"}).jobs, 256U);
    EXPECT_TRUE(parsed_as<AnalyzeCommand>({"analyze", "--jobs=256", "--force"}).replace_database);
}

// Verifies: b16-resumable-analysis-v2 (ADR-0019: `--jobs` takes a whole
// number from 1 to 256, anything else is a usage error).
TEST(CommandLine, AnalyzeRejectsBadJobsAndResume) {
    for (const std::string_view bad :
         {"--jobs=0", "--jobs=257", "--jobs=-1", "--jobs=x", "--jobs=4x", "--jobs=+4", "--jobs= 4",
          "--jobs=99999999999999999999"}) {
        expect_usage_error({"analyze", bad}, "--jobs");
    }
    expect_usage_error({"analyze", "--jobs"}, "--jobs needs a value");
    expect_usage_error({"analyze", "--resume=yes"}, "--resume");
    expect_usage_error({"analyze", "--resume", "--resume"}, "twice");
    // Analysis options belong to analyze only.
    expect_usage_error({"map", "--jobs", "2"}, "--jobs");
    expect_usage_error({"query", "export", "--resume"}, "--resume");
}

// Verifies: b16-resumable-analysis-v2 (the help names `--jobs`, `--resume`
// and the exit status for an interrupted analysis).
TEST(CommandLine, HelpNamesJobsResumeAndInterrupted) {
    EXPECT_NE(usage().find("--jobs N"), std::string_view::npos) << usage();
    EXPECT_NE(usage().find("--resume"), std::string_view::npos) << usage();
    EXPECT_NE(usage().find("5 interrupted"), std::string_view::npos) << usage();
    // SA_RESETHAND resets only the signal delivered (ADR-0019).
    EXPECT_NE(usage().find("a second signal of the same kind"), std::string_view::npos) << usage();
    EXPECT_EQ(static_cast<int>(ExitStatus::interrupted), 5);
}

} // namespace
