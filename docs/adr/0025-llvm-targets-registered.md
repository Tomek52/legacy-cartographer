# 0025. LLVM targets are registered before the first parse

- **Status:** Accepted (decided by delegation: the owner delegated the
  decision on 2026-10-11; merged on 2026-10-12 as
  [#31](https://github.com/Tomek52/legacy-cartographer/pull/31); recorded in
  [B-39](../plan/backlog.md#b39-adrs-for-merged-decisions-v1))
- **Date:** 2026-10-11
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1)
- **Supersedes / superseded by:** — (amends
  [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-clang-cl-commands-v1):
  its statement that lcart registers no LLVM targets no longer holds; see
  Consequences)

## Context

The Win32 configuration of an MSBuild solution gives
`--target=i686-pc-windows-msvc` commands
([ADR-0022](0022-msbuild-logger-for-compilation-databases.md)). Tried on
Windows with the sample solution and `lcart` at commit `41f6e4f`: the
translation units that include `<windows.h>` failed in `lcart` with
"MS-style inline assembly is not available: Unable to find target for this
triple (no targets are registered)", while `clang-cl` parsed them; x64 was
not affected. The Windows SDK's `winnt.h` has MS-style inline assembly
(`__asm`) for x86, and Clang's parser looks the target up in LLVM's target
registry to assemble such a block (`ParseStmtAsm.cpp`, LLVM 20.1.2).
`lcart` registered no targets; `clang`'s own `main` registers them before
it parses.

The open question asked whether to register the X86 target, and noted that
registering targets also changes what the `inferTargetAndDriverMode()`
wrapper of
[ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-clang-cl-commands-v1)
may add.

## Options considered

1. **Register nothing**; the how-to says to analyse the x64 configuration.
   Win32 translation units that include `<windows.h>` stay unparsed.
2. **Register the X86 target only.** Covers `winnt.h` for x86, but parsing
   depends on which targets lcart chose, unlike `clang`.
3. **Register every target the linked LLVM provides**, as `clang`'s `main`
   does.

## Decision

Option 3.

<a id="adr0025-register-all-targets-once-v1"></a>**Every LLVM target is
registered once, before the first parse.** `ClangAnalyzer::analyze()`
(`src/analysis/src/clang_analyzer.cpp`, `register_llvm_targets()`) calls
`llvm::InitializeAllTargets()`, `InitializeAllTargetMCs()`,
`InitializeAllAsmPrinters()` and `InitializeAllAsmParsers()` once per
process (`std::call_once`), before any worker thread starts, because
registration is not thread-safe. The registered targets are those of the
LLVM that lcart links (LLVM 20, ADR-0007).

## Consequences

- MS-style inline assembly parses for the x86 and x86_64 Windows targets.
  arm64 has no MS-style inline assembly in Clang at all ("unsupported
  architecture 'aarch64'"), so it is not covered. The real `<windows.h>`
  of the SDK with the Win32 configuration was not tried again after the
  change (`TODO: verify` in the [how-to](../how-to/msvc-under-wsl2.md)).
- **A target prefix in the program name is now used.** With targets
  registered, `inferTargetAndDriverMode()` treats a
  registered target prefix of the program a command names as its target
  (`ToolChain::getTargetAndModeFromProgramName()` sets `TargetIsValid` only
  for a registered target, `clang/Driver/ToolChain.h`; `Tooling.h` states
  `InitializeAllTargets()` as the precondition of
  `addTargetAndModeForProgramName()`). So the sentence of
  [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-clang-cl-commands-v1)
  "lcart registers no LLVM targets, so the wrapper adds only the driver
  mode, never a target taken from a program name prefix" no longer holds.
  Tried on 2026-10-17 with clang 20.1.2: a command
  `x86_64-w64-mingw32-clang -c a.c` without `--target`, on a file that
  fails unless `_WIN32` is defined, failed to parse with `lcart` built from
  the commit before #31 and parsed with `lcart` after it; the same command
  run as `clang` failed with both. `clang` invoked under such a name does
  the same. An explicit `--target` in the command still
  wins (tried: the same command with `--target=x86_64-linux-gnu` failed).
  No test covers this yet; whether to keep it is an
  [open question](../open-questions.md#open-technical-questions).
- Regression tests in `tests/analysis/msvc_code_test.cpp`:
  `MsvcCodeTest.ParsesMsInlineAssemblyForAnX86Target` and
  `ParsesMsInlineAssemblyForAnX64Target` (a function with `__asm { nop }`
  parses with no diagnostics for `i686-pc-windows-msvc` and
  `x86_64-pc-windows-msvc`).
- Revisit if lcart links an LLVM built without the X86 target, or if a
  target prefix in program names turns out to give wrong facts.
