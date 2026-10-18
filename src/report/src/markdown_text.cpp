#include "markdown_text.hpp"

#include "facts/fact_export.hpp"

#include <algorithm>
#include <cstddef>

namespace lcart::report::detail {

namespace {

constexpr std::string_view replacement_character = "\xEF\xBF\xBD";

} // namespace

std::string printable(std::string_view text) {
    constexpr unsigned char first_printable = 0x20;
    constexpr unsigned char delete_character = 0x7F;
    std::string out;
    out.reserve(text.size());
    while (!text.empty()) {
        const std::size_t length = facts::utf8_sequence_length(text);
        const auto lead = static_cast<unsigned char>(text.front());
        if (length == 0 || (length == 1 && (lead < first_printable || lead == delete_character))) {
            out += replacement_character;
            text.remove_prefix(1);
        } else {
            out += text.substr(0, length);
            text.remove_prefix(length);
        }
    }
    return out;
}

std::string code_span(std::string_view text) {
    const std::string content = printable(text);
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char character : content) {
        run = character == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const std::string fence(longest + 1, '`');
    const bool pad = content.empty() || content.front() == '`' || content.back() == '`';
    const std::string_view space = pad ? " " : "";
    return fence + std::string{space} + content + std::string{space} + fence;
}

} // namespace lcart::report::detail
