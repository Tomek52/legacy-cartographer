#pragma once

#include <string>
#include <vector>

namespace lcart::analysis {

struct FilteredArguments {
    std::vector<std::string> arguments; ///< Input order, minus removed items.
    std::vector<std::string> removed;   ///< Each removed option with its value, joined by a space.
};

/// Removes compiler arguments that would load code into the analysing
/// process: plugins and pass plugins, in driver form (`-fplugin=`), as cc1
/// options passed with `-Xclang` / `-Xclang=`, or bare in a `-cc1` command.
/// Also removes `@file` arguments: response files must be expanded before
/// filtering, so one left over cannot be checked (docs/security.md, T5).
///
/// The option list was taken from `clang-20 --help-hidden` and
/// `clang-20 -cc1 --help` (2026-10-03). Arguments are otherwise unchanged.
[[nodiscard]] FilteredArguments remove_code_loading_arguments(std::vector<std::string> arguments);

} // namespace lcart::analysis
