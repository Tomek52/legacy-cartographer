#include "analysis/module_rules.hpp"

#include <algorithm>

namespace lcart::analysis {

std::string default_module(std::string_view relative_path) {
    const std::size_t slash = relative_path.find('/');
    if (slash == std::string_view::npos) {
        return "(root)";
    }
    return std::string{relative_path.substr(0, slash)};
}

std::string module_of(std::string_view relative_path, std::span<const config::ModuleRule> rules) {
    for (const config::ModuleRule& rule : rules) {
        if (std::ranges::any_of(rule.paths, [relative_path](const config::PathGlob& glob) {
                return glob.matches(relative_path);
            })) {
            return rule.name;
        }
    }
    return default_module(relative_path);
}

} // namespace lcart::analysis
