#pragma once

#include <string_view>

namespace lcart::common {

/// Tool version, from project(VERSION) in the top-level CMakeLists.txt.
/// Recorded in every fact database and report so outputs can be traced to
/// the tool build that produced them.
inline constexpr std::string_view version = LCART_VERSION;

} // namespace lcart::common
