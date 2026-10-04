#include "cli/command_line.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lcart::cli {

namespace {

using common::Error;
using common::ErrorCode;
using common::Result;

constexpr std::string_view usage_text = R"(usage: lcart <command> [options]

Commands:
  analyze    build the fact database from compile_commands.json
  query      print facts from the fact database, or export them all
  map        write the module map (Markdown + Mermaid), no LLM
  handoff    write a bundle for an external coding agent          (planned, M2)
  ingest     validate agent output, verify claims, write report   (planned, M2)
  verify     verify a claims file against the fact database       (planned, M2)
  testgen    generate a characterization test skeleton           (planned, M3)

lcart analyze [--config FILE] [--compilation-database FILE|DIR] [--root DIR]
              [--db FILE] [--force]
  Reads lcart.toml (--config, else ./lcart.toml if it exists). Paths in it
  are relative to its directory. Defaults: root = the directory of lcart.toml
  (else the working directory), compilation database = <root>/
  compile_commands.json, database = lcart-out/facts.lcart.db. An existing
  database is replaced only with --force.

lcart query <query> [--db FILE]
  callers <function>      direct calls of the function, by file and line
  callees <function>      calls made by the function
  includes <file>         include directives in the file
  included-by <file>      include directives naming the file
  module-deps [<module>]  module dependencies
  export [--output FILE]  all facts as JSON Lines (default: standard output)
  <function> is a name or a stored USR; <file> a path relative to the
  analysis root.

lcart map [--db FILE] [--output FILE]
  Writes the module list and a Mermaid graph of module dependencies as
  Markdown. Default output: lcart-out/map.md. Replaces an earlier map,
  never another file or the database; --output - writes to standard
  output.

--output - (or --output=-) means standard output.

Exit status: 0 success, 1 failure (including an unreadable file), 2 invalid
command line or lcart.toml content, 3 not found in the fact database,
4 ambiguous function name.

Options:
  -h, --help   show this message
  --version    show the tool version
)";

constexpr std::array planned_commands{"handoff", "ingest", "verify", "testgen"};

/// `--output -`: standard output (ADR-0017).
constexpr std::string_view standard_output = "-";

Error usage_error(std::string message) {
    return Error{.code = ErrorCode::invalid_argument, .message = std::move(message)};
}

/// Options and positional arguments of one command, before interpretation.
struct ParsedArguments {
    bool help{};
    std::map<std::string, std::string, std::less<>> values; ///< option -> value
    std::vector<std::string> flags;
    std::vector<std::string> positional;
};

/// Records the value of the option at `arguments[index]`, given after `=` or
/// as the next argument unless that starts with `-` (then `index` moves
/// past it).
common::Status add_value(ParsedArguments& parsed, std::span<const std::string_view> arguments,
                         std::size_t& index) {
    const std::string_view argument = arguments[index];
    const std::size_t equals = argument.find('=');
    const std::string name{argument.substr(0, equals)};
    std::string_view value;
    if (equals != std::string_view::npos) {
        value = argument.substr(equals + 1);
    } else if (index + 1 < arguments.size() && (!arguments[index + 1].starts_with('-') ||
                                                arguments[index + 1] == standard_output)) {
        // `--db --force` is a forgotten value, not a database named
        // "--force"; a value starting with `-` is given as `--db=-x`. A lone
        // `-` is never an option, so `--output -` needs no `=` (ADR-0017).
        value = arguments[++index];
    }
    if (value.empty()) {
        return std::unexpected(usage_error(name + " needs a value"));
    }
    if (!parsed.values.emplace(name, std::string{value}).second) {
        return std::unexpected(usage_error(name + " given twice"));
    }
    return {};
}

/// Splits `arguments` into options and positional arguments. `value_options`
/// take a value, `flag_options` none.
Result<ParsedArguments> split_arguments(std::span<const std::string_view> arguments,
                                        std::span<const std::string_view> value_options,
                                        std::span<const std::string_view> flag_options) {
    ParsedArguments parsed;
    bool options_ended = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        if (options_ended || argument.size() < 2 || argument.front() != '-') {
            parsed.positional.emplace_back(argument);
            continue;
        }
        if (argument == "--") {
            options_ended = true;
            continue;
        }
        if (argument == "-h" || argument == "--help") {
            parsed.help = true;
            continue;
        }
        const std::size_t equals = argument.find('=');
        const std::string_view name = argument.substr(0, equals);
        if (std::ranges::find(flag_options, name) != flag_options.end()) {
            if (equals != std::string_view::npos) {
                return std::unexpected(usage_error(std::string{name} + " takes no value"));
            }
            if (std::ranges::find(parsed.flags, name) != parsed.flags.end()) {
                return std::unexpected(usage_error(std::string{name} + " given twice"));
            }
            parsed.flags.emplace_back(name);
            continue;
        }
        if (std::ranges::find(value_options, name) == value_options.end()) {
            return std::unexpected(usage_error("unknown option '" + std::string{name} + "'"));
        }
        if (auto added = add_value(parsed, arguments, i); !added) {
            return std::unexpected(added.error());
        }
    }
    return parsed;
}

std::optional<std::filesystem::path> path_option(const ParsedArguments& parsed,
                                                 std::string_view name) {
    const auto found = parsed.values.find(name);
    if (found == parsed.values.end()) {
        return std::nullopt;
    }
    return std::filesystem::path{found->second};
}

/// `--output`: `fallback` when not given, nullopt (standard output) for `-`.
std::optional<std::filesystem::path> output_option(const ParsedArguments& parsed,
                                                   std::optional<std::filesystem::path> fallback) {
    auto output = path_option(parsed, "--output");
    if (!output) {
        return fallback;
    }
    if (*output == standard_output) {
        return std::nullopt;
    }
    return output;
}

Result<Command> parse_analyze(std::span<const std::string_view> arguments) {
    static constexpr std::array<std::string_view, 4> value_options{
        "--config", "--compilation-database", "--root", "--db"};
    static constexpr std::array<std::string_view, 1> flag_options{"--force"};
    auto parsed = split_arguments(arguments, value_options, flag_options);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->help) {
        return HelpCommand{};
    }
    if (!parsed->positional.empty()) {
        return std::unexpected(
            usage_error("unexpected argument '" + parsed->positional.front() + "' for analyze"));
    }
    AnalyzeCommand analyze;
    analyze.config = path_option(*parsed, "--config");
    analyze.compilation_database = path_option(*parsed, "--compilation-database");
    analyze.root = path_option(*parsed, "--root");
    analyze.database = path_option(*parsed, "--db").value_or(analyze.database);
    analyze.replace_database = !parsed->flags.empty();
    return analyze;
}

struct QueryShape {
    std::string_view word;
    QueryKind kind;
    std::string_view subject; ///< What the subject names; empty: none.
    bool subject_required;
};

constexpr std::array<QueryShape, 6> query_shapes{{
    {.word = "callers",
     .kind = QueryKind::callers,
     .subject = "a function",
     .subject_required = true},
    {.word = "callees",
     .kind = QueryKind::callees,
     .subject = "a function",
     .subject_required = true},
    {.word = "includes",
     .kind = QueryKind::includes,
     .subject = "a file",
     .subject_required = true},
    {.word = "included-by",
     .kind = QueryKind::included_by,
     .subject = "a file",
     .subject_required = true},
    {.word = "module-deps",
     .kind = QueryKind::module_deps,
     .subject = "a module",
     .subject_required = false},
    {.word = "export", .kind = QueryKind::export_facts, .subject = "", .subject_required = false},
}};

std::string query_words() {
    std::string words;
    for (const QueryShape& shape : query_shapes) {
        words += words.empty() ? "" : ", ";
        words += shape.word;
    }
    return words;
}

Result<Command> parse_query(std::span<const std::string_view> arguments) {
    if (arguments.empty()) {
        return std::unexpected(usage_error("query needs one of: " + query_words()));
    }
    if (arguments.front() == "-h" || arguments.front() == "--help") {
        return HelpCommand{};
    }
    const auto* shape = std::ranges::find(query_shapes, arguments.front(), &QueryShape::word);
    if (shape == query_shapes.end()) {
        return std::unexpected(usage_error("unknown query '" + std::string{arguments.front()} +
                                           "'; one of: " + query_words()));
    }
    static constexpr std::array<std::string_view, 2> value_options{"--db", "--output"};
    auto parsed = split_arguments(arguments.subspan(1), value_options, {});
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->help) {
        return HelpCommand{};
    }
    const std::string query_name = "query " + std::string{shape->word};
    if (shape->kind != QueryKind::export_facts && parsed->values.contains("--output")) {
        return std::unexpected(usage_error("--output applies to query export only"));
    }
    const std::size_t allowed = shape->subject.empty() ? 0 : 1;
    if (parsed->positional.size() > allowed) {
        return std::unexpected(usage_error("unexpected argument '" + parsed->positional[allowed] +
                                           "' for " + query_name));
    }
    if (shape->subject_required && parsed->positional.empty()) {
        return std::unexpected(usage_error(query_name + " needs " + std::string{shape->subject}));
    }
    QueryCommand query;
    query.kind = shape->kind;
    query.subject = parsed->positional.empty() ? "" : parsed->positional.front();
    query.database = path_option(*parsed, "--db").value_or(query.database);
    query.output = output_option(*parsed, std::nullopt);
    return query;
}

Result<Command> parse_map(std::span<const std::string_view> arguments) {
    static constexpr std::array<std::string_view, 2> value_options{"--db", "--output"};
    auto parsed = split_arguments(arguments, value_options, {});
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->help) {
        return HelpCommand{};
    }
    if (!parsed->positional.empty()) {
        return std::unexpected(
            usage_error("unexpected argument '" + parsed->positional.front() + "' for map"));
    }
    MapCommand map;
    map.database = path_option(*parsed, "--db").value_or(map.database);
    map.output = output_option(*parsed, map.output);
    return map;
}

} // namespace

Result<Command> parse_command_line(std::span<const std::string_view> arguments) {
    if (arguments.empty()) {
        return std::unexpected(usage_error("no command given"));
    }
    const std::string_view command = arguments.front();
    if (command == "-h" || command == "--help" || command == "help") {
        return HelpCommand{};
    }
    if (command == "--version") {
        return VersionCommand{};
    }
    if (command == "analyze") {
        return parse_analyze(arguments.subspan(1));
    }
    if (command == "query") {
        return parse_query(arguments.subspan(1));
    }
    if (command == "map") {
        return parse_map(arguments.subspan(1));
    }
    if (std::ranges::find(planned_commands, command) != planned_commands.end()) {
        return std::unexpected(usage_error("'" + std::string{command} +
                                           "' is not available yet (see docs/plan/roadmap.md)"));
    }
    return std::unexpected(usage_error("unknown command '" + std::string{command} + "'"));
}

std::string_view usage() {
    return usage_text;
}

} // namespace lcart::cli
