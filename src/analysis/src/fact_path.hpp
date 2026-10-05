#pragma once

#include <filesystem>
#include <string>

namespace lcart::analysis {

/// How a file is named in the facts (docs/architecture/data-model.md).
struct FactPath {
    std::string path; ///< Root-relative with `/`, or absolute if external.
    bool is_external{};
};

/// How the file at `absolute_name` (as Clang names it, made absolute) is
/// named in the facts: its canonical path (symlinks and `..` resolved)
/// relative to `root`, which must be absolute and canonical, or the
/// canonical path itself for a file outside the root. Depends only on its
/// arguments and the file system, so every worker names a file alike.
[[nodiscard]] FactPath fact_path_of(const std::string& absolute_name,
                                    const std::filesystem::path& root);

} // namespace lcart::analysis
