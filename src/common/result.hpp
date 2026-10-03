#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace lcart::common {

/// Error categories shared by all modules. Switches over this enum list every
/// enumerator and have no `default:`, so -Wswitch-enum flags a new one.
enum class ErrorCode : std::uint8_t {
    invalid_argument,
    io_error,
    parse_error,
    not_found,
    /// A request would send source code to a remote provider without explicit
    /// consent (NFR-002; providers arrive in M6, ADR-0006).
    egress_denied,
    provider_error,
    /// Agent or LLM output does not conform to the claim JSON Schema (FR-009).
    schema_violation,
};

struct Error {
    ErrorCode code;
    std::string message;
};

/// Value-or-error return type used across module boundaries (ADR-0007).
template <typename T>
using Result = std::expected<T, Error>;

/// Result of an operation that produces no value.
using Status = std::expected<void, Error>;

} // namespace lcart::common
