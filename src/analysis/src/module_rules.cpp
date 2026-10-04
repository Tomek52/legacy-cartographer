#include "analysis/module_rules.hpp"

namespace lcart::analysis {

std::string default_module(std::string_view relative_path) {
    const std::size_t slash = relative_path.find('/');
    if (slash == std::string_view::npos) {
        return "(root)";
    }
    return std::string{relative_path.substr(0, slash)};
}

} // namespace lcart::analysis
