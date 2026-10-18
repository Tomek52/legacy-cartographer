# 0023. Clang configuration files are ignored

- **Status:** Accepted (decided by delegation: the owner delegated the
  decision on 2026-10-11; merged on 2026-10-12 as
  [#29](https://github.com/Tomek52/legacy-cartographer/pull/29); recorded in
  [B-39](../plan/backlog.md#b39-adrs-for-merged-decisions-v1))
- **Date:** 2026-10-11
- **Requirements:** [NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1),
  [security T5](../security.md#security-malicious-build-input-v1)
- **Supersedes / superseded by:** — (extends the filter of
  [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-pass-through-filtered-v1);
  its decision stands)

## Context

A compilation database is untrusted input
([NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1),
[T5](../security.md#security-malicious-build-input-v1)). Before the Clang
driver inside `lcart` sees a command, `remove_code_loading_arguments()`
(`src/analysis/argument_filter.hpp`) removes the arguments that would load
code into the analysing process, including those passed through clang-cl's
`/clang:` and `-clang:` (ADR-0021).

The B-17 spike showed that the driver also reads Clang configuration files,
and that their arguments bypass that filter:

- an explicit `--config=<file>`, or the separate form `--config <file>`, in
  the command;
- default configuration files, such as `clang.cfg` or `<triple>-clang.cfg`,
  in the directory of the program the command names.

LibTooling does not load `-load` plugins today (ADR-0021, finding 4), but the
filter should not depend on that. The open question from the spike (to be
decided by the end of M1) asked whether to remove `--config` and add
`--no-default-config`. The option list was taken from `clang-20
--help-hidden` (2026-10-11).

## Options considered

1. **Leave configuration files alone** and rely on LibTooling never loading
   plugins. No work, but the filter of T5 has a known way around it.
2. **Remove `--config` and pass `--no-default-config`** (the open question's
   proposal): the explicit file is never named to the driver, and the
   driver never looks for a default one.

## Decision

Option 2.

<a id="adr0023-explicit-config-removed-v1"></a>**An explicit configuration
file is removed.** `remove_code_loading_arguments()` removes
`--config=<file>` (one argument) and the separate form `--config <file>`
(both arguments; a trailing `--config` alone), plain or passed through
clang-cl (`/clang:`, `-clang:`), in every place its other removals look,
with an `-Xclang` or `-Xpreprocessor` right before it. Only `--config=` and
the whole word `--config` match: `-config`, `--config-system-dir=`,
`--config-user-dir=`, `-multi-lib-config=` and a file named `config` are
kept. Each removal is reported as a warning naming the removed arguments
and the reason "reads a configuration file" (plugins keep "loads code into
the analyser").

<a id="adr0023-no-default-config-v1"></a>**Default configuration files are
never read.** The recorder (`src/analysis/src/translation_unit_recorder.cpp`)
inserts `--no-default-config` right after the program name of every command,
after the filter has run, so the driver reads no default `clang.cfg` or
`<triple>-clang.cfg` next to the named program. clang-cl accepts the flag
too (checked with clang 20.1.2). A `-cc1` command line is left without it,
because it must start with `-cc1` right after the program name; an explicit
`--config` in it is still removed by the filter.

## Consequences

- [T5](../security.md#security-malicious-build-input-v1) lists
  configuration files among the inputs that are stopped; the open question
  is in the Decided table of the [open questions](../open-questions.md).
- A project whose build relies on a Clang configuration file (for example a
  cross toolchain's `<triple>-clang.cfg`) is analysed without that file's
  arguments. Its include paths or macros are then missing, and the affected
  translation units become diagnostics and lower coverage; the warning
  names an explicit `--config` that was removed, but a default file is
  skipped silently.
- Regression tests:
  - `tests/analysis/argument_filter_test.cpp`:
    `ArgumentFilter.RemovesConfigEqualsForm`,
    `RemovesConfigSeparateFormWithItsValue`,
    `KeepsSurroundingArgumentsInOrderAroundConfig`,
    `RemovesConfigPassedThroughClangCl`,
    `RemovesConfigSeparateFormPassedThroughClangCl`,
    `KeepsOptionsThatMerelyLookLikeConfig` and
    `PluginRemovalsKeepTheCodeLoadingReason`;
  - `tests/analysis/clang_analyzer_test.cpp`:
    `ClangAnalyzerTest.IgnoresExplicitConfigFileAndWarns` (a translation
    unit that parses only if the file is read fails, and the removal is
    reported) and `IgnoresDefaultConfigFileNextToTheProgram` (the same with
    a `clang.cfg` next to the program the command names).
- Revisit if Clang adds another way to read a configuration file (check the
  option list on each LLVM upgrade, ADR-0007), or if users need a
  configuration file's arguments; they would then have to be added to the
  compilation database itself.
