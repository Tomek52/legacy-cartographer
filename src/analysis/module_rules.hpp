#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lcart::analysis {

/// Maps files to logical modules (FR-005). Rules are evaluated in order and
/// the first matching glob wins; files matching no rule fall back to their
/// top-level directory under the analysis root.
// TODO(B-11): load rules from the project config file and apply them.
struct ModuleRule {
    std::string module;
    std::string path_glob;
};

using ModuleRules = std::vector<ModuleRule>;

/// The module of a file that matches no rule: the first component of
/// `relative_path` (root-relative, `/` separators), or `(root)` for a file
/// directly in the analysis root (FR-005).
[[nodiscard]] std::string default_module(std::string_view relative_path);

} // namespace lcart::analysis
