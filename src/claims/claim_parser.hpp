#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace lcart::claims {

/// Largest input parse_claims reads. 500 claims at their maximum lengths
/// fit with room to spare; anything larger is rejected before parsing
/// (docs/specs/claim-schema.md#claim-json-schema-v2).
inline constexpr std::size_t max_claims_input_size = std::size_t{4} * 1024 * 1024;

/// Deepest nesting of objects and arrays the claim schema allows: document,
/// `claims` array, claim, entity or source. Deeper input is rejected before
/// parsing.
inline constexpr std::size_t max_claims_nesting_depth = 4;

/// Parses and schema-validates agent or LLM output into claims (FR-009),
/// following docs/specs/claim-schema.md: the JSON Schema v1, then the
/// entity types per kind and unique claim IDs.
///
/// The input is untrusted: it comes from a model that has read the analysed
/// code, including its comments (docs/security.md). Input larger than
/// max_claims_input_size or nested deeper than max_claims_nesting_depth is
/// rejected before a document is built, so memory and recursion are bounded
/// by these constants, not by the input.
///
/// Errors: ErrorCode::schema_violation, never an exception or a partial
/// result. Its message lists every problem found, one per line, in the
/// order the document is checked, each as `<JSON pointer>: <description>`
/// (`/claims/3/kind: not one of ...`); the pointer of the whole document is
/// written `""`. A missing property is reported at the pointer it would
/// have. Property names from the input are shown with control characters
/// escaped and cut to a bounded length.
[[nodiscard]] common::Result<std::vector<Claim>> parse_claims(std::string_view json);

} // namespace lcart::claims
