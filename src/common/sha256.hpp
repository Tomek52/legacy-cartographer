#pragma once

#include <string>
#include <string_view>

namespace lcart::common {

/// SHA-256 of `data` (FIPS 180-4) as 64 lower-case hexadecimal digits.
/// Used outside `analysis`, which hashes with LLVM's implementation
/// (ADR-0028); both give the same digits for the same bytes.
[[nodiscard]] std::string sha256_hex(std::string_view data);

} // namespace lcart::common
