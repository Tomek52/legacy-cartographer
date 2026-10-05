#include "fact_path.hpp"

#include <system_error>

namespace lcart::analysis {

FactPath fact_path_of(const std::string& absolute_name, const std::filesystem::path& root) {
    // Canonical, so a file reached through a symlink or `..` compares
    // equal to the canonical root.
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute_name, ec);
    if (ec) {
        canonical = std::filesystem::path{absolute_name}.lexically_normal();
    }
    const std::filesystem::path relative = canonical.lexically_relative(root);
    const bool is_external =
        relative.empty() || relative == "." || *relative.begin() == std::filesystem::path{".."};
    return {.path = (is_external ? canonical : relative).generic_string(),
            .is_external = is_external};
}

} // namespace lcart::analysis
