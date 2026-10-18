# 0019. Parallel, resumable analysis with one writer

- **Status:** Accepted; the TU list (one TU per fact path) and the handler for a second signal (of either kind) amended by [ADR-0020](0020-unit-list-and-second-signal.md); the file that carries the run's `flock` amended by [ADR-0024](0024-run-lock-on-a-separate-file.md)
- **Date:** 2026-10-04
- **Requirements:** [NFR-005](../specs/requirements.md#nfr005-performance-budget-v3),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-16](../plan/backlog.md#b16-resumable-analysis-v2)
- **Supersedes / superseded by:** — (confirms the
  [threading sketch](../architecture/components.md#arch-threading-v1);
  amends [ADR-0016](0016-facts-export-and-command-line.md): a new exit
  status, the `--jobs` and `--resume` options, and the partial database of
  `lcart analyze`)

## Context

`lcart analyze` parses translation units (TUs) one after another and the
collectors write straight into the fact store. They keep state across TUs
and take decisions in the middle of a TU from it: only a function's first
definition gets its calls recorded (`FunctionFactCollector::on_function`
returns an id only then); a second definition with external linkage is
recorded under `path#usr` (ADR-0013); callees resolve through the TU's own
declarations; file ids are assigned in event order (includes during
preprocessing, then functions). libmodbus takes about 1 s; a large code
base may take hours or days, so NFR-005 asks for all cores, progress at
least every 30 s, and resuming an interrupted analysis without parsing
completed TUs again, with the same export as an uninterrupted run (NFR-004).

`clang::tooling::ClangTool` uses `llvm::vfs::getRealFileSystem()` by
default, whose working directory is the process's ("usually
thread-hostile", `llvm/Support/VirtualFileSystem.h`), and changes it to each
compile command's directory. ADR-0016 writes a new database to a hidden
file next to the target (`.<name>.partial-<pid>` in `src/cli`) and renames
it over the target only on success, so an interrupted run leaves nothing to
resume.

## Options considered

1. **Workers write to SQLite directly** (one connection each, WAL). Write
   contention, and the cross-TU decisions above (which definition is first)
   depend on scheduling: not deterministic.
2. **Workers parse, one writer merges in TU order.** Each worker records
   what it saw in a TU, without deciding anything that depends on other
   TUs; the writer replays the records strictly in TU order and takes every
   cross-TU decision exactly as the sequential collectors do today.
3. **Process per TU** (fork/exec). Isolates Clang crashes (security T5) but
   costs a process start per TU and serialisation; can be added later on
   top of option 2, whose batches are self-contained.

## Decision

<a id="adr0019-one-writer-in-tu-order-v1"></a>**Workers and one writer.**
`lcart analyze --jobs N` runs N worker threads; N defaults to
`std::thread::hardware_concurrency()`, or 1 when that returns 0; `--jobs`
takes an integer from 1 to 256 (anything else exits 2) and is a command-line
option only, not read from lcart.toml. TUs are the filtered compilation
database files sorted byte-wise (today's order); a TU's **index** is its
position in that list. Each worker has its own `ClangTool` with its own
`llvm::vfs::createPhysicalFileSystem()` as base file system (its own working
directory). The compilation database is loaded once, and the main thread
fetches each TU's compile commands (with response files expanded) before
dispatching it, so a worker's `ClangTool` never calls into the shared
database; every path the main thread derives from the working directory
(root, exclusions, TU paths) is made absolute before workers start.

A worker turns a TU into a **batch**: the sequence of events the collectors
receive today, in the order they happen (files and include directives,
then functions with their definitions' calls, diagnostics, and the
argument-filter warnings). It records the calls of *every* definition, and
names callees by a batch-local reference to the function record of the
same batch, never by USR alone. The batch holds no database ids.

The main thread is the only writer. It replays batches in index order
through the existing collector logic, which now runs writer-side: file ids,
first-definition rule (dropping the calls of definitions a sequential run
would not record), ADR-0012/0013 identity and warnings, callee resolution.
So the database, the export and the warnings are the same for any N.

<a id="adr0019-dispatch-window-v1"></a>**Dispatch window.** A worker takes
TU *i* only if *i* < *head* + 2 × N, where *head* is the index the writer
needs next; completed batches wait in the writer's buffer. No worker ever
blocks holding a result, so there is no deadlock; at most 2 × N batches
are buffered. A slow TU at the head lets the other workers run ahead only
up to the window, then they idle: accepted for v1, revisited if B-16's
measurements show it (larger window).

<a id="adr0019-commit-per-tu-v1"></a>**Commit per TU.** The writer applies
each batch in one transaction that also sets the TU's `status` (`parsed` or
`failed`), with SQLite's default rollback journal and `synchronous`
setting, which this ADR pins: no `synchronous=OFF` or in-memory journal.
Module dependencies and coverage are derived once, after the last TU;
coverage counts come from the TU statuses in the database, not from the
current run.

<a id="adr0019-resume-v1"></a>**Resume.** A new analysis writes to
`.<name>.partial` next to the target (no pid: the next run must find it),
holding an exclusive `flock` on it for the whole run (a second run on the
same target exits 1); it is renamed over the target when the analysis completes,
following ADR-0016's rule for the target (`--force`, only lcart's own
files). An interrupted or failed run keeps the partial database. Without
`--resume` an existing, unlocked partial database is replaced (it is
lcart's own file), together with its `-journal` file, which a crash or a
second signal can leave behind and SQLite would otherwise roll back into
the new database. `lcart analyze --resume` continues it:

- it refuses (exit 1, naming what differs) if there is no partial database,
  if it is not an lcart fact database, or if `meta` records a different
  SHA-256 of the compilation database file, lcart version, schema version,
  or **configuration hash**: SHA-256 of the analysis root, the module rules
  and exclusions in order, and the sorted TU list after filtering;
- completed TUs are always a prefix of the TU list (the writer commits in
  index order); the writer rebuilds its state (file ids, recorded
  functions and renamed bodies) from the partial database, then continues
  with the first `pending` TU;
- the summary counts the TUs already done; warnings of those TUs were
  printed by the earlier run and are not repeated.

Source files edited between the runs are not detected (the TU's
`inputs_sha256` is not checked); a resumed run then mixes old and new code.
This is recorded as a known limitation (open questions).

<a id="adr0019-interrupt-v1"></a>**Interruption.** SIGINT and SIGTERM are
installed with `SA_RESETHAND`: the handler only stores `true` in a
`std::atomic<bool>` that is `static_assert`ed to be always lock-free, and
the next signal has the default action. Workers take no new TU; a parse in
progress cannot be cancelled, so the run waits for up to N parses to end.
The writer commits every batch that is next in index order, discards the
others (they are parsed again on resume), prints the summary and exits with
**status 5, "interrupted"** (the value ADR-0016 reserved). A completed run
still exits 0 when at least one TU in the database parsed, as ADR-0016
says. A second
signal ends the process at once; the partial database stays consistent
because each TU is one transaction.

No TU is marked before it is parsed, so a TU that crashes Clang every time
stops `--resume` at the same place on each run. This is accepted for v1 and
tracked with the process-per-TU question (security T5).

<a id="adr0019-progress-v1"></a>**Progress.** While analysing, lcart prints
to standard error at least every 30 s and at the end: parsed, failed,
already done and total TUs, and the time remaining, estimated as elapsed
wall time of this run ÷ TUs completed in this run × TUs left. Progress
never reaches the database or any output file (NFR-004).

<a id="adr0019-memory-v1"></a>**Memory.** A worker frees its Clang AST
before taking the next TU; at most N TUs are parsed and at most 2 × N
batches are buffered, so peak memory depends on N and on the largest TUs,
not on the number of TUs.

## Consequences

- The collectors are split: AST traversal and event recording stay in the
  worker (Clang stays in `analysis`); identity and store writes run in the
  writer. This is the bulk of B-16.
- A fixture of ≥ 200 TUs, generated by a script, measures throughput for 1
  and 4 workers and checks that their exports are identical (B-16).
- On acceptance, ADR-0016's status line and index row say it is amended by
  this ADR (as for ADR-0017 and ADR-0018); the CLI help lists exit status 5,
  `--jobs` and `--resume`; the components page links this ADR.
- A Clang crash ends the run, and a TU that always crashes blocks
  `--resume` (above).
- Revisit if the dispatch window stalls workers on code bases with a few
  very large TUs, or if per-TU transactions make SQLite the bottleneck
  (several TUs per transaction).
