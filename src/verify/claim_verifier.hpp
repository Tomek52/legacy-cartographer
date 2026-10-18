#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"
#include "facts/fact_store.hpp"
#include "verify/verdict.hpp"

namespace lcart::verify {

/// Checks one claim against the fact database with the rules of
/// docs/specs/claim-schema.md: entity resolution, relation rules, negation.
/// Deterministic and side-effect free: the same claim and facts always give
/// the same verdict (NFR-004).
///
/// Errors: `invalid_argument` if an entity's type does not fit the claim's
/// kind (the claim parser rejects such claims); otherwise only the errors
/// of `facts`' queries, which fail if the database cannot be read.
[[nodiscard]] common::Result<Verdict> verify_claim(const claims::Claim& claim,
                                                   const facts::FactQuery& facts);

} // namespace lcart::verify
