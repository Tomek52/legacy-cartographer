# 0009. Linux and WSL2 only; MSVC code analysed in Clang's MSVC mode

- **Status:** Accepted; the TODO in its MSVC decision (exact flags, clang-cl arguments) resolved by [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md)
- **Date:** 2026-10-03
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1), [FR-001](../specs/requirements.md#fr001-ingest-compilation-database-v1)

## Context

The tool is meant for large legacy code bases, some of which build only with
MSVC on Windows. Analysis needs two things from such a code base: a
compilation database, and Clang accepting the code (MSVC extensions,
Windows SDK and framework headers). The maintainer confirmed that running
the tool under WSL2 is acceptable; a native Windows build is not needed.

## Options considered

1. **Native Windows support.** Windows CI, MSVC or clang-cl builds of the
   tool, LLVM on Windows. Most faithful for MSVC code; doubles the build
   and CI matrix.
2. **Linux and WSL2 only.** One platform to build and test. MSVC code is
   parsed by Clang in MSVC-compatibility mode, with the Windows headers
   made available to the Linux file system.

## Decision

<a id="adr0009-linux-wsl2-only-v1"></a>`lcart` is built and supported on
**Ubuntu 24.04, native or under WSL2**. No native Windows build.

<a id="adr0009-msvc-code-via-clang-v1"></a>Code written for MSVC is analysed
by Clang 20 with MSVC compatibility: target triple `x86_64-pc-windows-msvc`
and the compatibility flags Clang provides for MSVC code (`-fms-extensions`,
`-fms-compatibility`, `-fms-compatibility-version=…`), taken from or added to
the compilation database. Header search paths must point to copies of, or
WSL mounts of, the Windows SDK and framework headers. (TODO: verify the
exact flag set and how clang-cl-style arguments in a compilation database
are handled by LibTooling, in the B-17 spike.)

<a id="adr0009-compdb-is-input-v1"></a>Producing `compile_commands.json` for
an MSBuild solution stays outside the tool, as for any build system
([ADR-0002](0002-static-analysis-frontend.md#adr0002-requires-compilation-database-v1)).
The docs describe at least one tested way to get it (B-17).

## Consequences

- One CI platform. WSL2 paths (`/mnt/c/...`) must work as analysis roots
  and in compilation databases; file paths are normalised to the analysis
  root, so facts do not contain drive letters.
- Analysing headers from `/mnt/c` is slower than from the Linux file
  system; the docs recommend copying the code base into WSL2.
- If Clang cannot parse a part of an MSVC code base, those translation units
  become diagnostics and lower coverage; the verifier then answers
  `unverified` instead of `contradicted` for claims that depend on them
  ([claim schema](../specs/claim-schema.md#claim-entity-resolution-v1)).
