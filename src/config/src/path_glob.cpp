#include "config/path_glob.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcart::config {

namespace {

/// Matches `items` against `units`, where a run unit matches any number of
/// items and every other unit exactly one. Backtracks only to the last run,
/// which is enough because every other unit consumes exactly one item:
/// O(units × items), never exponential.
template <typename Unit, typename Item, typename IsRun, typename Matches>
[[nodiscard]] bool match_with_runs(std::span<const Unit> units, std::span<const Item> items,
                                   IsRun is_run, Matches matches) {
    std::size_t unit = 0;
    std::size_t item = 0;
    std::optional<std::size_t> last_run;
    std::size_t resume_item = 0;
    while (item < items.size()) {
        if (unit < units.size() && is_run(units[unit])) {
            last_run = unit++;
            resume_item = item;
        } else if (unit < units.size() && matches(units[unit], items[item])) {
            ++unit;
            ++item;
        } else if (last_run) {
            unit = *last_run + 1;
            item = ++resume_item;
        } else {
            return false;
        }
    }
    while (unit < units.size() && is_run(units[unit])) {
        ++unit;
    }
    return unit == units.size();
}

[[nodiscard]] std::vector<std::string_view> split_segments(std::string_view path) {
    std::vector<std::string_view> segments;
    std::size_t begin = 0;
    for (std::size_t slash = path.find('/'); slash != std::string_view::npos;
         slash = path.find('/', begin)) {
        segments.push_back(path.substr(begin, slash - begin));
        begin = slash + 1;
    }
    segments.push_back(path.substr(begin));
    return segments;
}

[[nodiscard]] std::size_t byte_of(char c) {
    return static_cast<unsigned char>(c);
}

struct ParsedSet {
    std::bitset<256> accepted;
    std::size_t end{}; ///< Index just past the closing `]`.
};

/// Parses the set starting at `text[open] == '['`; empty if it is unclosed.
/// A `]` right after `[` or `[!` is a member, as in POSIX globs.
[[nodiscard]] std::optional<ParsedSet> parse_set(std::string_view text, std::size_t open) {
    std::size_t at = open + 1;
    const bool negated = at < text.size() && text[at] == '!';
    if (negated) {
        ++at;
    }
    const std::size_t first = at;
    ParsedSet set;
    while (at < text.size()) {
        const char c = text[at];
        if (c == ']' && at > first) {
            if (negated) {
                set.accepted.flip();
            }
            set.end = at + 1;
            return set;
        }
        if (at + 2 < text.size() && text[at + 1] == '-' && text[at + 2] != ']') {
            // A reversed range (`[z-a]`) is empty, as in POSIX.
            for (std::size_t byte = byte_of(c); byte <= byte_of(text[at + 2]); ++byte) {
                set.accepted.set(byte);
            }
            at += 3;
        } else {
            set.accepted.set(byte_of(c));
            ++at;
        }
    }
    return std::nullopt;
}

} // namespace

common::Result<PathGlob> PathGlob::parse(std::string_view pattern) {
    auto invalid = [pattern](std::string_view why) {
        return std::unexpected(common::Error{.code = common::ErrorCode::invalid_argument,
                                             .message = "path glob '" + std::string{pattern} +
                                                        "' " + std::string{why}});
    };
    if (pattern.empty()) {
        return invalid("is empty");
    }
    if (pattern.front() == '/') {
        return invalid("must be relative to the analysis root");
    }
    if (pattern.contains('\\')) {
        return invalid("must separate directories with '/'");
    }

    PathGlob glob;
    glob.pattern_ = pattern;
    for (const std::string_view text : split_segments(pattern)) {
        if (text.empty()) {
            return invalid("has an empty path segment");
        }
        if (text == "." || text == "..") {
            return invalid("has a '.' or '..' segment, which a root-relative path never has");
        }
        Segment segment;
        if (text == "**") {
            segment.any_depth = true;
        } else {
            auto tokens = parse_tokens(text);
            if (!tokens) {
                return invalid(tokens.error());
            }
            segment.tokens = std::move(*tokens);
        }
        glob.segments_.push_back(std::move(segment));
    }
    return glob;
}

std::expected<std::vector<PathGlob::Token>, std::string_view>
PathGlob::parse_tokens(std::string_view text) {
    std::vector<Token> tokens;
    for (std::size_t at = 0; at < text.size();) {
        Token token;
        if (text[at] == '*') {
            if (at + 1 < text.size() && text[at + 1] == '*') {
                return std::unexpected("has '**' that is not a whole path segment");
            }
            token.kind = Token::Kind::any_run;
            ++at;
        } else if (text[at] == '?') {
            token.accepted.set();
            ++at;
        } else if (text[at] == '[') {
            std::optional<ParsedSet> set = parse_set(text, at);
            if (!set) {
                return std::unexpected("has an unclosed '['");
            }
            token.accepted = set->accepted;
            at = set->end;
        } else {
            token.accepted.set(byte_of(text[at]));
            ++at;
        }
        tokens.push_back(token);
    }
    return tokens;
}

bool PathGlob::segment_matches(const Segment& segment, std::string_view text) {
    return match_with_runs(
        std::span<const Token>{segment.tokens}, std::span<const char>{text},
        [](const Token& token) { return token.kind == Token::Kind::any_run; },
        [](const Token& token, char c) { return token.accepted.test(byte_of(c)); });
}

bool PathGlob::matches(std::string_view path) const {
    const std::vector<std::string_view> parts = split_segments(path);
    return match_with_runs(
        std::span<const Segment>{segments_}, std::span<const std::string_view>{parts},
        [](const Segment& segment) { return segment.any_depth; },
        [](const Segment& segment, std::string_view part) {
            return !segment.any_depth && segment_matches(segment, part);
        });
}

} // namespace lcart::config
