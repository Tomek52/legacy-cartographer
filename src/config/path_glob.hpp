#pragma once

#include "common/result.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::config {

/// A path pattern from `lcart.toml`, matched against a whole root-relative
/// path with `/` separators (ADR-0014): `*` and `?` stay within one path
/// segment, `**` as a whole segment matches zero or more segments, and
/// `[a-z]` / `[!a-z]` match one character of a set. Case-sensitive; there is
/// no escaping (a literal `*` is `[*]`).
///
/// Matching takes time linear in the product of pattern and path lengths,
/// so a hostile pattern cannot make it backtrack exponentially.
class PathGlob {
public:
    /// Fails with ErrorCode::invalid_argument for a pattern that could never
    /// match a root-relative path (empty, absolute, an empty, `.` or `..`
    /// segment, a backslash) or that is malformed (an unclosed `[`, `**`
    /// within a segment).
    [[nodiscard]] static common::Result<PathGlob> parse(std::string_view pattern);

    /// Whether all of `path` (relative, `/` separators) matches.
    [[nodiscard]] bool matches(std::string_view path) const;

    /// The pattern as written.
    [[nodiscard]] const std::string& pattern() const { return pattern_; }

private:
    static constexpr std::size_t byte_values = 256;

    struct Token {
        enum class Kind : std::uint8_t { one_of, any_run };
        Kind kind{Kind::one_of};
        std::bitset<byte_values> accepted; ///< For one_of: the bytes it matches.
    };
    struct Segment {
        bool any_depth{}; ///< `**`: zero or more whole segments.
        std::vector<Token> tokens;
    };

    /// The tokens of one segment other than `**`, or why it is invalid.
    [[nodiscard]] static std::expected<std::vector<Token>, std::string_view>
    parse_tokens(std::string_view text);
    [[nodiscard]] static bool segment_matches(const Segment& segment, std::string_view text);

    std::string pattern_;
    std::vector<Segment> segments_;
};

} // namespace lcart::config
