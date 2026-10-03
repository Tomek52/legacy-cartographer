#pragma once

#include "claims/claim.hpp"
#include "facts/fact_store.hpp"
#include "verify/verdict.hpp"

namespace lcart::verify {

/// Checks one claim against the fact database using the rules in
/// docs/specs/claim-schema.md. Deterministic and side-effect free: the same
/// claim and facts always give the same verdict.
// TODO(M2): implement the per-kind rules.
[[nodiscard]] Verdict verify_claim(const claims::Claim& claim, const facts::FactQuery& facts);

} // namespace lcart::verify
