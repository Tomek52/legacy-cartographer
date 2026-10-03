#pragma once

#include <string>
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

} // namespace lcart::analysis
