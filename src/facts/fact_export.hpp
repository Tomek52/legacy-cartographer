#pragma once

#include "common/result.hpp"
#include "facts/fact_store.hpp"

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::facts {

/// Version of the JSON Lines facts export (ADR-0016). Any change to record
/// kinds, keys, their meaning or the sort order increments it.
/// 2: `may_call` records (ADR-0026).
inline constexpr int export_format_version = 2;

/// Writes every fact in `facts` to `out` as JSON Lines, in the format of
/// ADR-0016 as amended by ADR-0026: a header line, then modules, files,
/// functions, includes, calls, may_call edges, module dependencies and
/// diagnostics, each kind in a fixed order.
///
/// The output contains no row ids, timestamps, absolute paths or tool
/// version, so the same analysis input gives the same bytes on every run
/// and machine (NFR-004). Files outside the analysis root are named by the
/// spelling of the first directive that includes them (ADR-0016), and have
/// no content hash. Fails if the database cannot be read or `out` fails.
[[nodiscard]] common::Status write_jsonl_export(const FactQuery& facts, std::ostream& out);

/// How the facts export names a file (ADR-0016).
struct ExportedFile {
    std::string name;
    bool is_external{}; ///< Outside the analysis root.
};

/// The names the facts export gives `files` (ADR-0016): the stored path for
/// a file under the root; for a file outside it, the spelling of the first
/// directive (by includer path, then line, then spelling) that includes it,
/// with `#2`, `#3`, ... appended if an earlier external file has that name
/// already, and its file name if no directive names it. Each directive's
/// spelling is first passed through `spelling`: the export keeps it as it
/// is, handoff bundles hide absolute spellings (ADR-0028). `includes` are
/// the directives of `files`; every includer must be one of `files`.
[[nodiscard]] std::map<FileId, ExportedFile>
exported_file_names(const std::vector<FileFact>& files, const std::vector<IncludeFact>& includes,
                    const std::function<std::string(std::string_view)>& spelling);

/// Length of the valid UTF-8 sequence at the start of `text`, or 0 if its
/// first byte does not start one (RFC 3629: no overlong forms, surrogates
/// or code points above U+10FFFF). Precondition: `text` is not empty.
[[nodiscard]] std::size_t utf8_sequence_length(std::string_view text);

/// `text` as a JSON string literal, quotes included. Valid UTF-8 is copied;
/// each byte that is not part of a valid UTF-8 sequence becomes U+FFFD, so
/// any input gives valid JSON (names from analysed code are untrusted).
[[nodiscard]] std::string json_string(std::string_view text);

} // namespace lcart::facts
