#pragma once

#include "common/unreachable.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lcart::verify {

/// Outcome of checking one claim against the fact database (FR-010).
enum class VerificationStatus : std::uint8_t {
    confirmed,    ///< The facts support the claim.
    unverified,   ///< The facts can neither support nor refute it.
    contradicted, ///< The facts refute it.
};

/// Why a status was assigned; reported next to each verdict so a reader can
/// judge it (docs/specs/claim-schema.md).
enum class VerdictReason : std::uint8_t {
    relation_found,
    relation_absent,
    entity_not_found,
    entity_ambiguous,
    coverage_incomplete,
    indirect_calls_present,
};

struct Verdict {
    std::string claim_id;
    VerificationStatus status{VerificationStatus::unverified};
    VerdictReason reason{VerdictReason::coverage_incomplete};
    /// Fact database row ids supporting the verdict, for the report.
    std::vector<std::int64_t> evidence;
};

/// Stable lowercase name, used in JSON reports and the claim schema.
[[nodiscard]] constexpr std::string_view to_string(VerificationStatus status) noexcept {
    switch (status) {
    case VerificationStatus::confirmed:
        return "confirmed";
    case VerificationStatus::unverified:
        return "unverified";
    case VerificationStatus::contradicted:
        return "contradicted";
    }
    common::unreachable();
}

} // namespace lcart::verify
