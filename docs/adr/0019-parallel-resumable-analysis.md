# 0019. Parallel, resumable analysis with one writer

- **Status:** Proposed
- **Date:** 2026-10-04
- **Requirements:** [NFR-005](../specs/requirements.md#nfr005-performance-budget-v2),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-16](../plan/backlog.md#b16-resumable-analysis-v2)
- **Supersedes / superseded by:** — (confirms the
  [threading sketch](../architecture/components.md#arch-threading-v1);
  adds an exit status to [ADR-0016](0016-facts-export-and-command-line.md#adr0016-exit-status-v1)
  and a resumable partial database to its replacement rule)

## Context

`lcart analyze` parses translation units (TUs) one after another and the
collectors write straight into the fact store. Each collector keeps state
across TUs: file ids, the function identity rules of ADR-0012 and ADR-0013
(first definition, second definition reported), call targets. libmodbus
takes about 1 s; a large code base may take hours or days, so NFR-005 asks
for all cores, progress, and resuming after an interruption without parsing
completed TUs again. NFR-004 asks that the export does not depend on thread
scheduling.

ADR-0016 writes a new database to `.<name>.partial-<pid>` and renames it
over the target only when the analysis succeeds; an interrupted run leaves
nothing to resume. The data model already plans one transaction per TU
together with its `translation_unit.status`.

## Options considered

1. **Workers write to SQLite directly** (one connection each, WAL). Write
   contention, and the order of rows and of cross-TU decisions (which
   definition comes first) depends on scheduling: not deterministic.
2. **Workers parse, one writer merges in TU order.** Each worker produces
   a self-contained batch per TU (paths, USRs, spellings; no row ids); the
   writer applies batches strictly in sorted TU order, so every cross-TU
   decision is taken exactly as in a sequential run. Costs a reorder buffer.
3. **Process per TU** (fork/exec). Isolates Clang crashes (open question
   T5) but costs a process start per TU and serialisation; can be added
   later on top of option 2, whose batches are already self-contained.

## Decision

<a id="adr0019-one-writer-in-tu-order-v1"></a>**Workers and one writer.**
`lcart analyze --jobs N` (default: the number of hardware threads) runs N
worker threads, each with its own Clang instance, parsing one TU at a time
from the sorted, filtered list. A worker turns a TU into a **batch**: its
files, includes, functions, calls and diagnostics, named by path and USR,
with no database ids. The calling thread is the only writer: it applies
batches in TU order (index in the sorted list), keeping completed batches
that arrive early in a reorder buffer of at most 2 × N batches; a worker
whose result would overflow it waits. All cross-TU logic (file and function
identity, ADR-0012/0013 warnings, call targets) runs in the writer, so the
database and the export are the same for any N, including 1.

<a id="adr0019-commit-per-tu-v1"></a>**Commit per TU.** The writer applies
each batch in one transaction that also sets the TU's `status` (`parsed` or
`failed`). Module dependencies and coverage are derived once, after the last
TU.

<a id="adr0019-resume-v1"></a>**Resume.** A new analysis writes to
`.<name>.partial` next to the target (no pid: the next run must find it) and
renames it over the target when it completes. An interrupted or failed run
keeps that file. `lcart analyze --resume` (same options) continues it,
skipping TUs whose status is not `pending`, and counts them in the summary
as "already done". The partial database records in `meta` the SHA-256 of
the compilation database file, the lcart version and the schema version;
`--resume` refuses (exit 1, naming what changed) if any differs, if there is
no partial database, or if it is not an lcart fact database. Without
`--resume`, an existing partial database is replaced (it is lcart's own
file). `--resume` and `--force` follow ADR-0016 for the final rename.

<a id="adr0019-interrupt-v1"></a>**Interruption.** SIGINT and SIGTERM set a
flag (an async-signal-safe `std::atomic<bool>` store, the only thing the
handler does). Workers finish or abandon their current TU and take no new
one; the writer commits every batch that is next in TU order and complete,
stops, prints the summary and exits with **status 5, "interrupted"** (the
new value ADR-0016 reserved; no other value changes). A second signal ends
the process at once (default action); the partial database stays
consistent because each TU is one transaction.

<a id="adr0019-progress-v1"></a>**Progress.** While analysing, lcart prints
to standard error, at most every 30 s and at the end: parsed, failed,
already done and total TUs, and an estimate of the time remaining (mean
time per completed TU in this run × TUs left ÷ N). Progress never reaches
the database or any output file (NFR-004).

<a id="adr0019-memory-v1"></a>**Memory.** A worker frees its Clang AST
before taking the next TU; at most N TUs are being parsed and at most 2 × N
batches are waiting, so peak memory depends on N and on the largest TUs,
not on the number of TUs.

## Consequences

- The collectors are split: AST traversal stays in the worker (Clang stays
  in `analysis`), identity and store writes move to a writer-side merger.
  This is the bulk of B-16.
- A fixture of ≥ 200 TUs, generated by a script, measures throughput for 1
  and 4 workers and checks that their exports are identical (B-16).
- Exit status 5 is added to the CLI documentation and ADR-0016's list.
- A Clang crash still ends the run; the per-TU commit means `--resume`
  continues after it (the crashed TU is retried). A process per TU
  (option 3) stays an open question (security T5).
- Revisit if the reorder buffer stalls workers on code bases with a few
  very large TUs (the window can grow), or if per-TU transactions make
  SQLite the bottleneck (batch several TUs per transaction).
