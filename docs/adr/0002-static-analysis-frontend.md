# 0002. Clang LibTooling as the analysis frontend

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [FR-001](../specs/requirements.md#fr001-ingest-compilation-database-v1)…[FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1), [FR-015](../specs/requirements.md#fr015-resolve-function-pointer-tables-v1), [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)

## Context

The fact database must be **correct** enough to contradict an LLM. That
requires the same view of the code as the compiler: macros expanded with the
project's flags, includes resolved, overloads and `static` functions
distinguished, declarations matched to definitions across translation units.
Target code bases range from C (libmodbus) to large C++ code bases built
with MSVC.

## Options considered

1. **Clang LibTooling (C++ API).** Full Clang AST, `RecursiveASTVisitor` and
   AST matchers, preprocessor callbacks (`PPCallbacks`) for includes and
   macros, compilation database support built in. The C++ API is not stable
   across LLVM major versions; builds need the Clang development packages
   (large).
2. **libclang (C API).** Stable ABI, Python bindings, smaller surface. Cursor
   traversal gives declarations, references and calls, but less detail
   (limited access to some expression and type information, no AST
   matchers). Good enough for many facts; harder for function-pointer table
   resolution (FR-015).
3. **tree-sitter.** Fast, error-tolerant, no build configuration needed. It
   is a parser, not a compiler frontend: no preprocessing with real flags, no
   name resolution, no types, no cross-TU identity. Calls through macros,
   overloads and same-named `static` functions are guessed. Too weak to
   *contradict* claims.
4. **Existing indexers (clangd index, Kythe, CodeQL database).** Rich, but
   large dependencies and formats we would have to follow; CodeQL's licence
   does not allow use on arbitrary private code bases.

| Criterion | LibTooling | libclang | tree-sitter |
|---|---|---|---|
| Semantic accuracy (macros, types, USR) | full | high | low |
| Function-pointer table resolution | yes (initializer AST) | partial | heuristic |
| API stability | per LLVM major | stable | stable |
| Build/deploy weight | heavy | medium | light |
| Works without a compilation database | no (needs flags) | no | yes |

## Decision

<a id="adr0002-libtooling-v1"></a>Use **Clang LibTooling** from the
distribution packages of **LLVM/Clang 18** (Ubuntu 24.04: `llvm-18-dev`,
`libclang-18-dev`, `libclang-cpp18-dev`), not from Conan.

<a id="adr0002-isolated-in-analysis-v1"></a>All Clang includes and linking stay
inside the `analysis` module behind the `Analyzer` interface. No other module
sees a Clang type. Upgrading LLVM is a change to one module plus a CI
matrix update.

<a id="adr0002-requires-compilation-database-v1"></a>A compilation database
is required input. For build systems that do not produce one (autotools,
MSBuild), the docs describe how to obtain it (e.g. `bear -- make`); producing
it is not the tool's job.

## Consequences

- Facts are as accurate as the compiler's view, which is what the verifier
  needs.
- The build depends on system LLVM packages; Conan manages only small
  libraries. The `debug-system` preset already follows this model.
- tree-sitter remains an option for a later "no compilation database" quick
  mode, clearly labelled lower confidence. Not planned.
- **Risk:** code bases built only with MSVC may use extensions and headers
  Clang does not accept by default. Clang's MSVC compatibility modes
  (`clang-cl`, `-fms-compatibility`) exist, but the effort on a real MSVC-only code
  base is unknown (TODO: verify; [open questions](../open-questions.md)).
- **Spike first:** backlog item B-07 confirms that `find_package(Clang)` from
  `/usr/lib/llvm-18/lib/cmake/clang` works with our presets before more is
  built on it. (The CMake package file exists on the development container;
  linking has not been tried yet.)
