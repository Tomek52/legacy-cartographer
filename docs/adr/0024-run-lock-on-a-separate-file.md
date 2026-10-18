# 0024. The run lock of an analysis is on a separate file

- **Status:** Accepted (decided by delegation: the owner delegated the
  decision on 2026-10-11; merged on 2026-10-12 as
  [#30](https://github.com/Tomek52/legacy-cartographer/pull/30); recorded in
  [B-39](../plan/backlog.md#b39-adrs-for-merged-decisions-v1))
- **Date:** 2026-10-11
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
  [B-16](../plan/backlog.md#b16-resumable-analysis-v2)
- **Supersedes / superseded by:** — (amends
  [ADR-0019](0019-parallel-resumable-analysis.md#adr0019-resume-v1): which
  file carries the run's `flock`; the rest of ADR-0019 stands)

## Context

[ADR-0019](0019-parallel-resumable-analysis.md#adr0019-resume-v1) has
`lcart analyze` write to `.<name>.partial` next to the target while
holding an exclusive `flock` on that file for the whole run, so that a
second run on the same target exits 1. SQLite takes its own POSIX locks on
the same file.

On a WSL2 drive mount (drvfs, file system type `v9fs` there), with the
analysis root on `/mnt/c/...` and the default database in `lcart-out/` under
it, `lcart analyze` failed with `preparing a fact query: database is
locked` (found while writing
[ADR-0022](0022-msbuild-logger-for-compilation-databases.md); the
[how-to](../how-to/msvc-under-wsl2.md) told users to give `--db` a path on
the Linux file system). The cause was lcart's own run lock: on `v9fs` the
`flock` on the partial database clashed with SQLite's POSIX locks on the
same file; on the Linux file system (ext4) they do not clash.

The open question listed three ways out: default the database elsewhere,
detect the mount and say so, or leave it to the how-to.

## Options considered

1. **Default the database elsewhere** (outside the root). Changes the
   default of [ADR-0016](0016-facts-export-and-command-line.md) for every
   user to work around one file system.
2. **Detect a drive mount and say so.** A clearer error, but the default
   still fails there.
3. **Leave it to the how-to** (`--db` on the Linux file system). No code,
   and the default keeps failing on `/mnt/c`.
4. **Take the run lock on a separate file** next to the partial database,
   so that lcart's `flock` and SQLite's locks are never on the same file.
   Not one of the three listed, and the one that removes the cause.

## Decision

Option 4.

<a id="adr0024-run-lock-on-separate-file-v1"></a>**The run lock is on a
separate file.** `lcart analyze` takes the exclusive `flock` of
[ADR-0019](0019-parallel-resumable-analysis.md#adr0019-resume-v1) for the
whole run on `.<name>.partial.lock` next to the partial database
`.<name>.partial`, never on the partial database itself. The lock is taken
before the partial database is prepared, and dropped after SQLite has
closed it. A second run on the same target is still refused with exit
status 1 while the lock file is locked. The lock file is created with
`O_EXCL` when it is missing; an existing name is opened and refused unless
it is the regular file at that name (not a symbolic link, and not a file
put there after the open). The partial database gets the same check before
it is read or written, with or without `--resume`.

<a id="adr0024-lock-file-never-deleted-v1"></a>**The lock file is never
deleted.** It is empty, carries only the `flock`, and stays in the output
directory after every run, successful or not: removing it would race with
a run waiting for it. A run that finds no lock file creates it; a later
run reuses it.

## Consequences

- The default database works on a drive mount: tried on 2026-10-11 with
  clang 20.1.2 and a one-file project under `/mnt/c` (confirmed `v9fs`),
  which failed with `database is locked` before the change and was analysed
  after it ([how-to](../how-to/msvc-under-wsl2.md)). CI has no drive mount,
  so this stays a manual check. The Linux file system is still the faster
  place for the database.
- A file `.<name>.partial.lock` appears next to every analysed database
  and stays there. It holds no facts, and the git exposure warning
  ([ADR-0016](0016-facts-export-and-command-line.md)) does not name it.
- Regression tests in `tests/cli/lcart_cli_test.cpp`:
  `LcartAnalyzeRunTest.SecondRunOnTheSameTargetIsRefused` (a held lock on
  the lock file refuses a run; the lock file stays, empty),
  `LcartAnalyzeRunTest.FlockOnThePartialDatabaseDoesNotStopARun` (a held
  `flock` on the partial database itself no longer refuses a run), and
  `LcartCliTest.FailedAnalysisKeepsThePreviousDatabase` and
  `LcartCliTest.MapWritesTheDefaultPath` (the lock file is the only file
  left).
- Revisit if lcart writes a database with something other than SQLite, or
  if a file system is found on which an empty file next to the database
  cannot be created or locked.
