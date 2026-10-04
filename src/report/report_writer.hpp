#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"
#include "verify/verdict.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

namespace lcart::report {

enum class ReportFormat : std::uint8_t { markdown, json };

struct VerificationReport {
    std::vector<claims::Claim> claims;
    std::vector<verify::Verdict> verdicts; ///< Same order as `claims`.
};

/// Writes the module map (FR-007) to `out`: Markdown with a coverage line,
/// a Mermaid graph of module dependencies (each edge labelled with its
/// include and call sites) and the modules with their files and function
/// definition counts, in the format of ADR-0017. Files outside the analysis
/// root are left out; files under it without a module are listed under
/// `(unassigned)`.
///
/// Built from facts only, sorted by name and path, with no timestamp,
/// absolute path or tool version: the same facts give the same bytes
/// (NFR-004). Names are escaped, so no module name or path can break the
/// Markdown or the Mermaid graph. Fails if the database cannot be read or
/// `out` fails.
[[nodiscard]] common::Status write_module_map(const facts::FactQuery& facts, std::ostream& out);

/// Writes the verification report (FR-011).
// TODO(M2): implement both formats.
common::Status write_verification_report(const VerificationReport& report, ReportFormat format,
                                         std::ostream& out);

} // namespace lcart::report
