#pragma once

#include "config/project_config.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::analysis {

/// The `[[modules]]` rules of lcart.toml, in file order.
using ModuleRules = std::vector<config::ModuleRule>;

/// The module of a file that matches no rule: the first component of
/// `relative_path` (root-relative, `/` separators), or `(root)` for a file
/// directly in the analysis root (FR-005).
[[nodiscard]] std::string default_module(std::string_view relative_path);

/// The module of the file at `relative_path`: the name of the first rule
/// with a glob matching the whole path, else default_module() (FR-005).
[[nodiscard]] std::string module_of(std::string_view relative_path,
                                    std::span<const config::ModuleRule> rules);

} // namespace lcart::analysis
