# 0012. Function identity: Clang's USR, path-prefixed for internal linkage

- **Status:** Accepted
- **Date:** 2026-10-04
- **Requirements:** [FR-003](../specs/requirements.md#fr003-record-functions-v1)
- **Supersedes / superseded by:** —

## Context

`FUNCTION.usr` is the key that call edges (B-10), claims and verification
use to name a function, and it is unique in the fact database
([data model](../architecture/data-model.md)). Clang's USR
([ADR-0002](0002-static-analysis-frontend.md)) is unique for functions with
external linkage (`c:@F@core_length`). For a function with internal linkage
(`static`, anonymous namespace) it names only the *base name* of the file
of the first declaration: checked with `c-index-test-20 -index-file`,
`static int helper(void)` in both `left/util.c` and `right/util.c` gets
`c:util.c@F@helper`, and a `static int callback(void);` declared in a
shared header and defined in two `.c` files gets `c:shared.h@F@callback`
in both translation units. Stored as is, these are one function, which
breaks FR-003 criterion 1 and would send calls to the wrong body (B-10).

A function with internal linkage is a different function in every
translation unit that defines it. A `static inline` function defined in a
header is technically one per translation unit too, but it is one piece of
source, and a reader (or a claim) means that source.

## Options considered

1. **Clang's USR as is.** No work; merges distinct functions as above.
2. **Prefix the main file of the translation unit.** Exactly the scope of
   internal linkage; but a `static inline` in a header becomes one row per
   including translation unit, so `find_functions("twice")` returns N
   identical-looking rows and claims about it are ambiguous.
3. **Prefix the file of the first declaration.** Fixes `left/util.c` vs
   `right/util.c`; still merges two definitions of a `static` first
   declared in a shared header.
4. **Prefix the file of the definition in the translation unit, else of the
   first declaration.** Distinct definitions get distinct identities; a
   function defined in a header stays one row; a declaration is grouped
   with the definition of its own translation unit.
5. **A synthetic id (hash of path and line).** Unique, but loses the link to
   Clang's USR that cross-checks and debugging rely on.

## Decision

<a id="adr0012-internal-linkage-usr-v1"></a>**A function's stored USR is
Clang's USR. For a function with internal linkage it is prefixed with the
root-relative path of the file of its definition in the translation unit,
or of its first declaration if the translation unit has no definition,
followed by `#`** (`left/util.c#c:util.c@F@helper`). Files are named as in
the fact database: the location is the macro's use for a function written
by a macro, and the path is absolute for a file outside the analysis root.

## Consequences

- FR-003 criterion 1 holds also for a `static` declared in a shared header;
  a `static inline` function in a header is one function.
- USRs with internal linkage are no longer Clang's; code that compares them
  with Clang output (e.g. a future cross-check with `clangd`) strips the
  prefix up to and including the `#` before `c:` (Clang's own USRs start
  with `c:` and may contain `#` later, e.g. for C++ parameter types).
- A declaration-only row of a `static` function (a translation unit that
  declares it and never defines it) is keyed by the declaring file, so it is
  not merged with any definition.
- Revisit if a claim needs per-translation-unit identity (e.g. a `static`
  function in a header whose body differs by macros per translation unit).
