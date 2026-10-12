#pragma once

#include <string>
#include <vector>

namespace lcart::analysis {

struct FilteredArguments {
    std::vector<std::string> arguments; ///< Input order, minus removed items.
    std::vector<std::string> removed;   ///< Each removed option with its value, joined by a space.
    std::vector<std::string> reasons; ///< Why each removed item was removed, parallel to `removed`.
};

/// Removes compiler arguments that would let a malicious compilation database
/// influence the analysing process, and says why each was removed
/// (`reasons`, parallel to `removed`): either it loads code into the
/// analyser, or it reads a Clang configuration file.
///
/// Code-loading arguments are plugins and pass plugins, in driver form
/// (`-fplugin=`), as cc1 options passed with `-Xclang` / `-Xclang=` /
/// `-Xpreprocessor`, in a `-Wp,` list (the whole `-Wp,` argument is
/// removed), or bare in a `-cc1` command, and the same inside clang-cl's
/// pass-through arguments (`/clang:<arg>`, `-clang:<arg>`, ADR-0021). Also
/// removes `@file` arguments: response files must be expanded before
/// filtering, so one left over cannot be checked (docs/security.md, T5).
///
/// Configuration-file arguments are `--config=<file>` (one argument) and the
/// separate form `--config <file>` (two arguments), plain or passed through
/// clang-cl, in every place the removals above look. The driver would read
/// the file and its arguments would bypass this filter (docs/security.md T5,
/// ADR-0023). Default configuration files next to the named program are
/// stopped separately, by `--no-default-config` inserted by the recorder.
/// `-config` (one dash) is not an option; `--config-system-dir=`,
/// `--config-user-dir=` and `-multi-lib-config=` point at directories or
/// data (moot once default files are off) and are left alone; none of these,
/// nor a file named `config`, matches, so all are kept.
///
/// An `-Xclang` or `-Xpreprocessor` right before a removed argument is
/// removed with it, so it never forwards what followed; removal is repeated
/// until nothing is left to remove.
///
/// The option list was taken from `clang-20 --help-hidden` and
/// `clang-20 -cc1 --help` (2026-10-03, configuration options 2026-10-11).
/// Arguments are otherwise unchanged.
[[nodiscard]] FilteredArguments remove_code_loading_arguments(std::vector<std::string> arguments);

} // namespace lcart::analysis
