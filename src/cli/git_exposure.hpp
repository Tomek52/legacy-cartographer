#pragma once

#include <filesystem>
#include <optional>

namespace lcart::cli {

/// Whether git could pick up `path` (an output file or directory, which
/// need not exist yet): returns the root of the git work tree it is in,
/// unless the ignore files of that work tree exclude it; nullopt otherwise
/// (docs/security.md T2, ADR-0016).
///
/// Runs no git command: the work tree may be the analysed repository,
/// whose configuration can make git run programs. Reads `.gitignore` files
/// from the work tree root down to `path`, and `.git/info/exclude`; not the
/// user's global excludes file. Any doubt (a pattern it does not support, a
/// file it cannot read) counts as "not ignored", so the answer errs towards
/// a warning.
[[nodiscard]] std::optional<std::filesystem::path>
unignored_git_work_tree(const std::filesystem::path& path, bool is_directory);

} // namespace lcart::cli
