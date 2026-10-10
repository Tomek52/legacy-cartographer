# 0020. One TU per fact path; a second signal of either kind ends lcart

- **Status:** Accepted
- **Date:** 2026-10-09
- **Requirements:** [NFR-005](../specs/requirements.md#nfr005-performance-budget-v3),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-18](../plan/backlog.md#b18-second-signal-of-either-kind-v1)
- **Supersedes / superseded by:** — (amends
  [ADR-0019](0019-parallel-resumable-analysis.md): the TU list of
  [Resume](0019-parallel-resumable-analysis.md#adr0019-resume-v1) and the
  handler of [Interruption](0019-parallel-resumable-analysis.md#adr0019-interrupt-v1))

## Context

ADR-0019 is accepted and lags the code of B-16 in two places (open
questions, 2026-10-04):

- **TU list.** ADR-0019 defines the TUs as the filtered compilation
  database files sorted byte-wise. The fact database records a TU once per
  fact path (its path relative to the analysis root), so two entries that
  name one file (through a symbolic link, or spelled differently) cannot be
  two TUs. Since B-16 the analysis keeps the first of them in sorted order
  (`AnalysisRun`, `src/analysis/src/clang_analyzer.cpp`), and the
  configuration hash, dispatch and totals use that list.
- **Second signal.** ADR-0019 installs the SIGINT and SIGTERM handler with
  `SA_RESETHAND`, which resets only the signal delivered. "A second signal
  ends the process at once" therefore holds only for a second signal of the
  same kind; SIGINT and then SIGTERM (a user pressing Ctrl-C, then a
  supervisor such as `timeout` or systemd sending SIGTERM) is handled as a
  second stop request, and lcart goes on waiting for the parses in
  progress.

POSIX lists `sigaction()`, `sigemptyset()` and `signal()` among the
async-signal-safe functions (`signal-safety(7)`), so a handler may change
the action of another signal. Signals to a process go to any thread that
does not block them; lcart's worker threads do not block SIGINT or SIGTERM.

## Options considered

1. **Keep ADR-0019's handler** and document "of the same kind". Cheap, but
   the user who stops lcart one way and then another waits for no reason.
2. **The handler resets both signals to `SIG_DFL`** with `sigaction()`,
   keeping `SA_RESETHAND` and blocking both signals while it runs (its
   `sa_mask`). Small and async-signal-safe.
3. **`signal(other, SIG_DFL)` in the handler.** Equally safe, but
   `signal()`'s semantics differ between systems; `sigaction()` states the
   flags and mask explicitly, as the installation already does.
4. **A signal-handling thread** (`sigwait` with SIGINT and SIGTERM blocked
   in every thread). Exact, but every thread, including those started in
   `src/analysis`, must block the signals: a larger change than the problem.

## Decision

<a id="adr0020-tu-list-by-fact-path-v1"></a>**One TU per fact path.** The TU
list of ADR-0019 (behind the configuration hash, dispatch, a TU's index and
the totals) is the filtered compilation database files sorted byte-wise and
then de-duplicated by fact path: entries naming one file, for example
through a symbolic link or a different spelling, are one TU, the first in
that sorted order. A TU's index is its position in the de-duplicated list.

<a id="adr0020-second-signal-of-either-kind-v1"></a>**A second signal of
either kind ends lcart.** The SIGINT and SIGTERM handler of ADR-0019 is
installed with `SA_RESETHAND | SA_RESTART` and with both signals in its
`sa_mask`. Besides storing `true` in the lock-free flag, it sets the action
of **both** signals to `SIG_DFL` with `sigaction()` (option 2), saving and
restoring `errno`. So after the first SIGINT or SIGTERM has been handled,
any second signal of either kind has the default action and ends lcart at
once; the partial database stays consistent because each TU is one
transaction (ADR-0019). The rest of ADR-0019's interruption rule (exit
status 5, the summary, the units committed) is unchanged.

A signal of the other kind, delivered after the first one but before its
handler's `sigaction()` calls (the kernel resets the first signal's own
action on delivery), can reach another thread; it is then
handled as another stop request, and a third signal ends lcart. The window
is a few instructions long; closing it needs option 4.

## Consequences

- ADR-0019's status line and index row say it is amended by this ADR; the
  `lcart --help` text says that a second signal of either kind ends lcart
  at once; the two open-questions rows are removed (B-18).
- Tests in `tests/cli/lcart_cli_test.cpp` send SIGINT and then SIGTERM, and
  the reverse, to a running `lcart analyze` and expect it to end by the
  second signal; `--resume` then finishes with the export of an
  uninterrupted run.
- Revisit (option 4) if the remaining window shows in practice, or if lcart
  gains other signals to handle.
