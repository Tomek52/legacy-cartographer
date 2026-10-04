#include "cli/git_exposure.hpp"

#include "config/path_glob.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lcart::cli {

namespace {

namespace fs = std::filesystem;

/// Larger ignore files are not read; the answer is then "not ignored".
constexpr std::uintmax_t max_ignore_file_size = 1024UL * 1024UL;

struct IgnorePattern {
    config::PathGlob glob;
    bool negated{};
    bool directory_only{};
};

/// The patterns of one ignore file, which apply to paths below `base`
/// (root-relative, empty for the work tree root).
struct IgnoreFile {
    std::string base;
    std::vector<IgnorePattern> patterns;
    /// It may re-include something this matcher cannot see: a negation it
    /// does not support, or the file could not be read.
    bool uncertain{};
};

/// Parses one line of an ignore file (gitignore(5)) into a path glob of
/// ADR-0014, or nullopt for a blank line, a comment and a pattern this
/// matcher does not support (`uncertain` is set if that pattern negates).
std::optional<IgnorePattern> parse_line(std::string_view line, bool& uncertain) {
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    while (line.ends_with(' ')) {
        line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
        return std::nullopt;
    }
    IgnorePattern pattern{.glob = {}, .negated = line.front() == '!', .directory_only = false};
    if (pattern.negated) {
        line.remove_prefix(1);
    }
    if (line.ends_with('/')) {
        pattern.directory_only = true;
        line.remove_suffix(1);
    }
    // Escapes, `[^...]` and POSIX classes (`[[:alpha:]]`) mean something
    // else in git than in PathGlob; non-ASCII patterns are left out too, so
    // no encoding question can make a negation invisible.
    const bool supported =
        !line.empty() && line.find('\\') == std::string_view::npos &&
        line.find("[^") == std::string_view::npos && line.find("[:") == std::string_view::npos &&
        std::ranges::none_of(line, [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
    // A pattern with a slash other than at its end is anchored at the
    // directory of its file; one without matches at any depth.
    const bool anchored = line.find('/') != std::string_view::npos;
    if (line.starts_with('/')) {
        line.remove_prefix(1);
    }
    std::string text{line};
    // In git a trailing `/**` matches everything inside, not the directory
    // itself; ADR-0014's `**` also matches zero segments, so require one.
    if (text.ends_with("/**")) {
        text.insert(text.size() - 2, "*/");
    }
    auto glob = config::PathGlob::parse(anchored ? text : "**/" + text);
    if (!supported || !glob) {
        uncertain = uncertain || pattern.negated;
        return std::nullopt;
    }
    pattern.glob = std::move(*glob);
    return pattern;
}

/// Reads the ignore file `file`, if it exists, for paths below `base`.
std::optional<IgnoreFile> read_ignore_file(const fs::path& file, std::string base) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(file, ec);
    if (!fs::exists(status)) {
        return std::nullopt;
    }
    IgnoreFile ignore{.base = std::move(base), .patterns = {}, .uncertain = false};
    // Only a regular file is opened: git does not follow a symbolic link
    // here either, and a FIFO in an untrusted checkout would block.
    if (!fs::is_regular_file(status)) {
        ignore.uncertain = true;
        return ignore;
    }
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec || size > max_ignore_file_size) {
        ignore.uncertain = true;
        return ignore;
    }
    // A sized read, not istreambuf_iterator: GCC 14 at -O3 reports
    // -Wnull-dereference inside the inlined iterator (as in config).
    std::string text(static_cast<std::size_t>(size), '\0');
    try {
        std::ifstream in{file, std::ios::binary};
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!in.is_open() || in.bad()) {
            ignore.uncertain = true;
            return ignore;
        }
        text.resize(static_cast<std::size_t>(in.gcount()));
    } catch (const std::ios_base::failure&) {
        // libstdc++'s filebuf throws on a failed read() even without
        // exceptions() set; an unreadable file only makes the check uncertain.
        ignore.uncertain = true;
        return ignore;
    }
    std::string_view rest = text;
    // git skips a UTF-8 byte order mark at the start of an ignore file.
    if (rest.starts_with("\xEF\xBB\xBF")) {
        rest.remove_prefix(3);
    }
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        if (auto pattern = parse_line(rest.substr(0, end), ignore.uncertain)) {
            ignore.patterns.push_back(std::move(*pattern));
        }
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
    }
    return ignore;
}

/// The nearest directory at or above `start` that has a `.git` entry (a
/// directory, or a file in linked work trees and submodules).
std::optional<fs::path> find_work_tree(fs::path start) {
    std::error_code ec;
    while (true) {
        if (fs::exists(fs::symlink_status(start / ".git", ec))) {
            return start;
        }
        if (start == start.parent_path()) {
            return std::nullopt;
        }
        start = start.parent_path();
    }
}

enum class Decision : std::uint8_t { none, excluded, included };

/// What the ignore files say about `path` (root-relative) alone, without its
/// parent directories: the last matching pattern wins, files later in
/// `ignore_files` (deeper) over earlier ones. An uncertain file that applies
/// turns "excluded" into "none".
Decision decide(std::string_view path, bool is_directory,
                const std::vector<IgnoreFile>& ignore_files) {
    Decision decision = Decision::none;
    bool uncertain = false;
    for (const IgnoreFile& file : ignore_files) {
        std::string_view relative = path;
        if (!file.base.empty()) {
            const bool below_base = path.starts_with(file.base) && path.size() > file.base.size() &&
                                    path[file.base.size()] == '/';
            if (!below_base) {
                continue; // the file is not in an ancestor directory of path
            }
            relative.remove_prefix(file.base.size() + 1);
        }
        uncertain = uncertain || file.uncertain;
        for (const IgnorePattern& pattern : file.patterns) {
            if ((!pattern.directory_only || is_directory) && pattern.glob.matches(relative)) {
                decision = pattern.negated ? Decision::included : Decision::excluded;
            }
        }
    }
    return uncertain && decision == Decision::excluded ? Decision::none : decision;
}

} // namespace

std::optional<fs::path> unignored_git_work_tree(const fs::path& path, bool is_directory) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return std::nullopt;
    }
    fs::path target = fs::weakly_canonical(absolute, ec);
    if (ec) {
        target = absolute.lexically_normal();
    }
    std::optional<fs::path> work_tree =
        find_work_tree(is_directory ? target : target.parent_path());
    if (!work_tree) {
        return std::nullopt;
    }

    std::vector<std::string> segments;
    for (const fs::path& segment : target.lexically_relative(*work_tree)) {
        if (segment != ".") {
            segments.push_back(segment.generic_string());
        }
    }
    if (segments.empty()) {
        return work_tree; // the work tree itself
    }
    if (segments.front() == ".git") {
        return std::nullopt; // git never tracks its own directory
    }

    // Lowest precedence first: info/exclude, then .gitignore files from the
    // root down (gitignore(5)). The user's global excludes file is not read.
    std::vector<IgnoreFile> ignore_files;
    if (fs::is_directory(*work_tree / ".git", ec)) {
        if (auto exclude = read_ignore_file(*work_tree / ".git" / "info" / "exclude", "")) {
            ignore_files.push_back(std::move(*exclude));
        }
    }
    fs::path directory = *work_tree;
    std::string relative;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (auto ignore = read_ignore_file(directory / ".gitignore", relative)) {
            ignore_files.push_back(std::move(*ignore));
        }
        relative += (relative.empty() ? "" : "/") + segments[i];
        const bool segment_is_directory = i + 1 < segments.size() || is_directory;
        // A path below an excluded directory stays excluded: git does not
        // look into the directory, so a negation cannot re-include it.
        if (decide(relative, segment_is_directory, ignore_files) == Decision::excluded) {
            return std::nullopt;
        }
        directory /= segments[i];
    }
    return work_tree;
}

} // namespace lcart::cli
