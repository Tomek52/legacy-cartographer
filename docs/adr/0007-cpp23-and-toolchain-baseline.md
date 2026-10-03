# 0007. C++23 and toolchain baseline: GCC 14, Clang/LLVM 20

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [NFR-006](../specs/requirements.md#nfr006-toolchain-baseline-v2)
- **Supersedes:** the LLVM/Clang **18** version in [ADR-0002](0002-static-analysis-frontend.md#adr0002-libtooling-v1) (LibTooling itself stands); the C++20 baseline of M0.

## Context

M0 targeted C++20 and used a hand-written `Result<T>` and `unreachable()`
in place of C++23's `std::expected` and `std::unreachable`. The maintainer
accepted C++23. Checked on Ubuntu 24.04 (2026-10-03):

- GCC 13 (default `g++`) compiles `std::expected` with `-std=c++23`.
- **Clang 18 with libstdc++ 13 does not**: `<expected>` declares nothing,
  because libstdc++ requires a newer `__cpp_concepts` value than Clang 18
  reports. Clang 20 compiles it.
- GCC 14, Clang 19 and Clang 20 (with `libclang-20-dev`,
  `libclang-cpp20-dev`, `llvm-20-dev`, `clang-tidy-20`, `clang-format-20`,
  `libclang-rt-20-dev`) are in the Ubuntu 24.04 archive.

## Options considered

1. **Stay on C++20** with own `Result`. Works everywhere; extra code.
2. **C++23 with GCC 13 + Clang 18.** Fails: no `std::expected` under Clang 18.
3. **C++23 with GCC 14 + Clang 20, and LLVM 20 for LibTooling.** One LLVM
   version for compiler, tools and analysis; all from the distribution.

## Decision

<a id="adr0007-cpp23-v1"></a>The tool is written in **C++23**. Fallible
functions return `std::expected<T, common::Error>` (`common::Result<T>`);
exhaustive switches end in `std::unreachable()`.

<a id="adr0007-toolchain-v1"></a>Supported compilers: **GCC 14** and
**Clang 20** on Ubuntu 24.04. clang-tidy and clang-format are version 20.
LibTooling uses **LLVM/Clang 20** from the same packages.

<a id="adr0007-wrapper-stays-cpp20-v1"></a>The libmodbus wrapper (M5) is a
library for others to use, so its public headers stay **C++20**-compatible
even though the tool uses C++23.

<a id="adr0007-probe-before-use-v1"></a>Before using a C++23 library feature
not used yet, check that both compilers accept it (CI builds with both);
if one does not, prefer a simple alternative over a fallback layer.

## Consequences

- Less own code (`Result` is an alias, `unreachable` is gone).
- Clang 20 picks the newest installed libstdc++; CI installs `g++-14` in
  every job so both compilers use libstdc++ 14.
- A work environment with an older toolchain cannot build the tool from
  source; it can still run a binary built elsewhere. Revisit if that becomes
  a real constraint.
