#pragma once

#include <string>
#include <string_view>

// Text from analysed code, lcart.toml and agent output in the Markdown that
// report writes; internal to the report module.
namespace lcart::report::detail {

/// Names and paths come from analysed code and lcart.toml, claims from an
/// agent (untrusted): `text` with each byte that is not part of valid
/// UTF-8, and each control character, replaced by U+FFFD, so no name can
/// end a line (ADR-0017).
[[nodiscard]] std::string printable(std::string_view text);

/// `text` as a Markdown code span (ADR-0017): nothing inside a code span is
/// Markdown or HTML, and a list item has no `|` cell separator to break. The
/// fence is one backtick longer than the longest run of backticks in the
/// text; a space pads a text that starts or ends with a backtick, which
/// CommonMark and Python-Markdown both strip again.
[[nodiscard]] std::string code_span(std::string_view text);

} // namespace lcart::report::detail
