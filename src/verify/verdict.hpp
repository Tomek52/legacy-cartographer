#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
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

/// A table of the fact database (docs/architecture/data-model.md) whose
/// rows a verdict can cite.
enum class FactTable : std::uint8_t { module, file, function, include_edge, call_edge };

/// One fact a verdict rests on: a row of a fact table. Row ids are per
/// table, so a reference names both (ADR-0029).
struct FactRef {
    FactTable table{FactTable::file};
    std::int64_t id{};

    auto operator<=>(const FactRef&) const = default;
};

struct Verdict {
    std::string claim_id;
    VerificationStatus status{VerificationStatus::unverified};
    VerdictReason reason{VerdictReason::coverage_incomplete};
    /// The facts the verdict rests on, in the order of the queries they come
    /// from (docs/specs/claim-schema.md, relation rules):
    /// - relation_found: the relation's facts (direct calls, include
    ///   directives, the edges behind a module dependency, the defining
    ///   function), or for `belongs_to` the subject (and a function's file).
    /// - relation_absent: the subject (a function stands for its
    ///   definition), and for `belongs_to` a function's file too.
    /// - indirect_calls_present: the subject's indirect calls, then its
    ///   may_call edges to the object.
    /// - entity_ambiguous: the candidates.
    /// - entity_not_found: empty. There is no fact to cite; the evidence is
    ///   the analysis coverage (every translation unit parsed), which the
    ///   report shows once for all verdicts.
    /// - coverage_incomplete: empty.
    std::vector<FactRef> evidence;

    bool operator==(const Verdict&) const = default;
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
    std::unreachable();
}

} // namespace lcart::verify
