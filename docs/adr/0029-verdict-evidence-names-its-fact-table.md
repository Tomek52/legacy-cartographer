# 0029. Verdict evidence names its fact table

- **Status:** Accepted
- **Date:** 2026-10-18
- **Requirements:** [FR-010](../specs/requirements.md#fr010-verify-claims-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-27](../plan/backlog.md#b27-claim-verifier-v1)
- **Supersedes / superseded by:** —

## Context

FR-010 asks for every verdict to carry "the supporting fact IDs", and
`verify::Verdict::evidence` was a list of bare row ids. Row ids in the fact
database are per table ([data model](../architecture/data-model.md)): file
3, function 3 and call edge 3 are three different facts. The
[relation rules](../specs/claim-schema.md#claim-relation-rules-v2) cite
rows of five tables: modules, files, functions, include directives and
call edges. Which table a bare id belongs to cannot always be derived from
the claim: a `depends_on` verdict cites the include directives *and* the
calls that justify the dependency. B-28 must turn evidence into file and
line for the report, so it must know the table.

`FactQuery` exposed the ids of modules, files and functions, but not of
include directives and call edges (`IncludeFact`, `CallFact`); only
`ModuleDependencyFact::edge_id` named one.

## Options considered

1. **A reference with its table**: `verify::FactRef{table, id}`, the
   table an enum of the five fact tables. Typed, compares and sorts, no
   parsing; the report decides how to print it.
2. **Bare ids, table implied by kind and reason**: no new type, but it
   fails for `depends_on`, and every reader has to know the rule.
3. **Strings such as `call_edge:12`**: self-describing in JSON, but the
   verifier would format and B-28 parse them; a typo is a run-time error.
4. **One id space across all fact tables**: a schema change for every
   table, for one consumer.

## Decision

<a id="adr0029-fact-references-v1"></a>**Fact references.**
`Verdict::evidence` is a list of `verify::FactRef`: a `FactTable`
(`module`, `file`, `function`, `include_edge`, `call_edge`, the tables of
the data model) and the row id in that table (option 1). The evidence of
each reason is listed in `src/verify/verdict.hpp` and follows the claim
schema; its order is that of the queries it comes from, so it is
deterministic.

<a id="adr0029-ids-of-includes-and-calls-v1"></a>**Ids of include
directives and call edges.** `IncludeFact` and `CallFact` get an `id`
(`IncludeId`, `CallId`), filled by every `FactQuery` method and ignored by
`FactWriter`, as for files, functions and modules. `may_call` edges are
rows of `CALL_EDGE`, so they have ids too. The database schema does not
change. How the `VERDICT` table and the reports spell a reference is
decided by B-28, which writes them first.

## Consequences

- B-28 can resolve every piece of evidence to a file and line with the
  existing queries (`file()`, `function()`, and the call and include
  queries, now with ids).
- Tests that compare whole `IncludeFact` or `CallFact` values read from a
  store compare their ids too; ids follow insertion order, so they are
  stable (data model).
- A new evidence table (for example diagnostics) is a new `FactTable`
  enumerator; switches over it are exhaustive, so the compiler finds every
  place to update.
