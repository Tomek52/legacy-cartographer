#pragma once

namespace lcart::common {

/// C++20 replacement for C++23 std::unreachable(). Placed after exhaustive
/// switches over enums so that -Wswitch-enum, not a `default:`, catches a new
/// enumerator.
[[noreturn]] inline void unreachable() {
    __builtin_unreachable();
}

} // namespace lcart::common
