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

/// Writes the module map (FR-007): Markdown with a Mermaid module graph,
/// built from facts only.
// TODO(B-13): implement.
common::Status write_module_map(const facts::FactQuery& facts, std::ostream& out);

/// Writes the verification report (FR-011).
// TODO(M2): implement both formats.
common::Status write_verification_report(const VerificationReport& report, ReportFormat format,
                                         std::ostream& out);

} // namespace lcart::report
