#pragma once

#include "common/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace lcart::cli {

/// Exit statuses of `lcart` (ADR-0016). Scripts rely on them: never
/// renumber one.
enum class ExitStatus : std::uint8_t {
    success = 0,
    /// The command could not do its work: no translation unit parsed, or a
    /// file or the fact database could not be read or written.
    failure = 1,
    /// Invalid command line, or an lcart.toml whose content is invalid (one
    /// that cannot be read is a failure).
    usage = 2,
    /// The function, file or module named on the command line is not in
    /// the fact database.
    not_found = 3,
    /// A function name matches several functions; the candidates are
    /// printed (FR-006 criterion 2).
    ambiguous = 4,
};

/// Where outputs go by default (docs/security.md T2): one directory that a
/// repository can ignore as a whole.
inline constexpr std::string_view default_output_directory = "lcart-out";
inline constexpr std::string_view default_database = "lcart-out/facts.lcart.db";
/// Read from the working directory when `--config` is not given and the
/// file exists.
inline constexpr std::string_view default_config = "lcart.toml";

struct HelpCommand {};
struct VersionCommand {};

/// `lcart analyze`. Paths given on the command line are relative to the
/// working directory; those in lcart.toml to the directory of lcart.toml.
struct AnalyzeCommand {
    std::optional<std::filesystem::path> config;               ///< `--config`
    std::optional<std::filesystem::path> compilation_database; ///< `--compilation-database`
    std::optional<std::filesystem::path> root;                 ///< `--root`
    std::filesystem::path database{default_database};          ///< `--db`
    bool replace_database{};                                   ///< `--force`
};

enum class QueryKind : std::uint8_t {
    callers,
    callees,
    includes,
    included_by,
    module_deps,
    export_facts,
};

/// `lcart query <kind> [<subject>]`.
struct QueryCommand {
    QueryKind kind{QueryKind::callers};
    /// A function name or stored USR (callers, callees), a stored file path
    /// (includes, included-by) or a module name (module-deps, optional).
    /// Empty when not given.
    std::string subject;
    std::filesystem::path database{default_database}; ///< `--db`
    /// `query export --output`; standard output when empty.
    std::optional<std::filesystem::path> output;
};

using Command = std::variant<HelpCommand, VersionCommand, AnalyzeCommand, QueryCommand>;

/// Parses the arguments after the program name. Options take their value
/// as the next argument or after `=` (`--db x`, `--db=x`); a value that
/// starts with `-` needs `=`. `--` ends the options. Errors: ErrorCode::invalid_argument, with a
/// message for the user, for an unknown command or option, a missing or extra argument, or an
/// option given twice.
[[nodiscard]] common::Result<Command>
parse_command_line(std::span<const std::string_view> arguments);

/// The text of `lcart --help`.
[[nodiscard]] std::string_view usage();

} // namespace lcart::cli
