#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace lcart::common {

/// Error categories shared by all modules. Switches over this enum list every
/// enumerator and have no `default:`, so -Wswitch-enum flags a new one.
enum class ErrorCode : std::uint8_t {
    invalid_argument,
    io_error,
    parse_error,
    not_found,
    /// A request would send source code to a remote provider without explicit
    /// consent (NFR-002, ADR-0004).
    egress_denied,
    provider_error,
    /// LLM output does not conform to the claim JSON Schema (FR-009).
    schema_violation,
};

struct Error {
    ErrorCode code;
    std::string message;
};

/// Value-or-error return type. Stand-in for C++23 std::expected while the
/// project targets C++20 (see docs/open-questions.md). Accessing the wrong
/// alternative throws std::bad_variant_access: check has_value() first.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }

private:
    std::variant<T, Error> storage_;
};

/// Result of an operation that produces no value.
using Status = Result<std::monostate>;

} // namespace lcart::common
