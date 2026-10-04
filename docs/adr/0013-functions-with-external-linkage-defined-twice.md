# 0013. A function with external linkage defined twice is two functions

- **Status:** Accepted
- **Date:** 2026-10-04
- **Requirements:** [FR-003](../specs/requirements.md#fr003-record-functions-v1),
  [FR-004](../specs/requirements.md#fr004-record-call-edges-v1)
- **Supersedes:** in part, [ADR-0012](0012-function-identity-for-internal-linkage.md#adr0012-internal-linkage-usr-v1): a stored USR is Clang's except for internal linkage *and* for a later definition of a function with external linkage (the rule for internal linkage stands)

## Context

One compilation database can hold several programs: libmodbus's lists the
library and its test programs, each with its own `main`. A function with
external linkage has one USR wherever it is defined (`c:@F@main`), and the
fact database stores one row per USR
([ADR-0012](0012-function-identity-for-internal-linkage.md)), so after
B-09 only the first definition in path order was recorded.

Call edges (B-10) make this matter. If the later `main`'s calls are
dropped, a claim "`main` calls `X`" about the second program can be
`contradicted` although it is true
([claim schema](../specs/claim-schema.md): `calls` is `contradicted` when
the subject's parsed definition has no such call). If they are merged
into the first row, a claim about one program can be `confirmed` by the
other's body. Neither the analyser nor the compilation database says which
translation units are linked together.

The same definition reached from several translation units (a function
defined in a header) is one piece of source and must stay one function.

## Options considered

1. **Keep the first definition only** (B-09 behaviour), with a warning.
   Simple; drops the second body's calls, so true claims can be
   contradicted.
2. **One row, calls of every definition merged.** No false contradiction;
   false confirmations across programs, and the row's lines describe one
   body only.
3. **Each distinct definition is a function of its own**; the first in path
   order keeps Clang's USR, a later one is keyed by its file like internal
   linkage; a warning names both. Each body keeps its calls. A call from a
   translation unit that defines the name goes to that definition; one from
   a translation unit that only declares it goes to the first definition,
   which may be the wrong program.
4. **Split the database per program** (a link map from the user or the
   build). Exact, but needs input that compilation databases do not carry;
   excluding a program from the analysis (B-11's `exclude_globs`) already
   gives this for a single program.

## Decision

<a id="adr0013-each-definition-a-function-v1"></a>**A definition of a
function with external linkage at a different place (file or first line)
from the definition already recorded for its USR is recorded as a function
of its own, with USR `<root-relative path of its file>#<Clang's USR>`
(`two/main.c#c:@F@main`), and the analysis reports a warning naming both
places.** Translation units are analysed in path order, so which definition
keeps Clang's USR is deterministic. Within a translation unit, a call names
that translation unit's last definition when it has one, else the function
with Clang's USR; each definition's own calls are recorded under its own
function, also when one translation unit has two (a GNU `extern inline`
definition and the out-of-line one).

## Consequences

- Each program's `main` keeps its own calls; `find_functions("main")`
  returns one row per program, as it does for `static` functions with one
  name. The verifier has to treat a name that matches several functions
  as ambiguous (M2).
- A call from a file that only declares the function is attributed to the
  first definition, which can be another program's. The warning tells the
  user; excluding the other programs avoids it.
- An ODR violation (one C++ inline function defined differently in two
  files) is reported the same way.
- Which definition keeps the plain USR depends on the set of translation
  units: adding a program whose path sorts first renames the others.
  Revisit if claims or exports must name such functions stably, e.g. with a
  per-program prefix from a link map (option 4).
