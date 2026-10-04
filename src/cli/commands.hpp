#pragma once

#include "cli/command_line.hpp"

#include <iosfwd>

namespace lcart::cli {

/// Runs `command`: results go to `out`, messages and warnings to `err`.
/// Returns the exit status for the process (ADR-0016).
[[nodiscard]] ExitStatus run(const Command& command, std::ostream& out, std::ostream& err);

} // namespace lcart::cli
