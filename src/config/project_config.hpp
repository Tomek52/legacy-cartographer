#pragma once

#include "common/result.hpp"
#include "config/path_glob.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::config {

/// One `[[modules]]` table: files matching any of `paths` belong to `name`
/// (FR-005).
struct ModuleRule {
    std::string name;
    std::vector<PathGlob> paths;
};

/// The project configuration, `lcart.toml` (ADR-0008). Values are as
/// written; resolving paths and applying defaults is the caller's job.
struct ProjectConfig {
    std::string name;                 ///< `[project] name`; empty if absent.
    std::string root;                 ///< `[project] root`; empty if absent.
    std::string compilation_database; ///< `[project] compilation_database`; empty if absent.
    /// `[project] exclude`: translation units whose root-relative path
    /// matches one of these are not analysed.
    std::vector<PathGlob> exclude;
    /// In file order: the first rule with a matching glob wins (FR-005).
    std::vector<ModuleRule> modules;
};

/// Parses the text of `lcart.toml`. `source_name` names the file in error
/// messages (`<source_name>:<line>:<column>: ...`).
///
/// Errors: ErrorCode::parse_error for text that is not TOML, not ASCII (a
/// leading UTF-8 byte order mark is allowed), larger than 1 MiB or with more
/// than 32 dotted key parts on a line (ADR-0015);
/// ErrorCode::invalid_argument for an unknown key, a value of the wrong
/// type, a module without a name or paths, an invalid glob (PathGlob), and
/// a consent switch such as `include_source`, which only the command line
/// may give (ADR-0008). The file is untrusted input: any text yields a
/// value or an error (NFR-010).
[[nodiscard]] common::Result<ProjectConfig> parse_project_config(std::string_view text,
                                                                 std::string_view source_name);

/// Reads and parses `file`; ErrorCode::io_error if it cannot be read.
[[nodiscard]] common::Result<ProjectConfig> load_project_config(const std::filesystem::path& file);

} // namespace lcart::config
