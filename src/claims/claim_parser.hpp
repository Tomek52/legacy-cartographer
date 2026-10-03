#pragma once

#include "claims/claim.hpp"
#include "common/result.hpp"

#include <string_view>
#include <vector>

namespace lcart::claims {

/// Parses and schema-validates agent or LLM output into claims (FR-009).
///
/// The input is untrusted: it comes from a model that has read the analysed
/// code, including its comments (docs/security.md). Invalid input yields
/// ErrorCode::schema_violation, never an exception or a partial result.
// TODO(M2): implement with nlohmann_json; fuzzed by fuzz/claim_json_fuzzer.cpp.
[[nodiscard]] common::Result<std::vector<Claim>> parse_claims(std::string_view json);

} // namespace lcart::claims
