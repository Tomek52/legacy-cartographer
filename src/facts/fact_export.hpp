#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace lcart::facts {

/// Version of the JSON Lines facts export (ADR-0016). Any change to record
/// kinds, keys, their meaning or the sort order increments it.
inline constexpr int export_format_version = 1;

/// Writes every fact in `facts` to `out` as JSON Lines, in the format of
/// ADR-0016: a header line, then modules, files, functions, includes,
/// calls, module dependencies and diagnostics, each kind in a fixed order.
///
/// The output contains no row ids, timestamps, absolute paths or tool
/// version, so the same analysis input gives the same bytes on every run
/// and machine (NFR-004). Files outside the analysis root are named by the
/// spelling of the first directive that includes them (ADR-0016), and have
/// no content hash. Fails if the database cannot be read or `out` fails.
[[nodiscard]] common::Status write_jsonl_export(const FactQuery& facts, std::ostream& out);

/// `text` as a JSON string literal, quotes included. Valid UTF-8 is copied;
/// each byte that is not part of a valid UTF-8 sequence becomes U+FFFD, so
/// any input gives valid JSON (names from analysed code are untrusted).
[[nodiscard]] std::string json_string(std::string_view text);

} // namespace lcart::facts
