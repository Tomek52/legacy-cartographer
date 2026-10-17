// libFuzzer target for the claim parser: LLM output is untrusted input
// (B-25, FR-009, NFR-010, docs/security.md). Built only with
// -DLCART_BUILD_FUZZERS=ON and Clang. Seed corpus: tests/claims/data
// (scripts/fuzz-smoke.sh).
#include "claims/claim_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {
constexpr std::size_t schema_max_items = 500; // docs/specs/claim-schema.md
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(data, data + size);
    const auto claims = lcart::claims::parse_claims(text);
    // Beyond "no crash": a value never breaks the schema's maxItems, and an
    // error is always a schema_violation that names at least one problem.
    if (claims ? claims->size() > schema_max_items
               : claims.error().code != lcart::common::ErrorCode::schema_violation ||
                     claims.error().message.empty()) {
        std::abort();
    }
    return 0;
}
