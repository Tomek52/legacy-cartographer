# 0031. Labelled claim set, accuracy gate and callees that are not found

- **Status:** Accepted (decided by delegation)
- **Date:** 2026-10-18
- **Requirements:** [NFR-009](../specs/requirements.md#nfr009-verifier-accuracy-measured-v1),
  [B-29](../plan/backlog.md#b29-labelled-claim-set-v1),
  [testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1),
  [entity resolution](../specs/claim-schema.md#claim-entity-resolution-v3)
- **Supersedes / superseded by:** — (narrows the open question on complete
  coverage that B-27 left to B-29; amends step 1 of the verification rules)

## Context

B-29 measures the verifier like a classifier: a labelled claim set over
the fixtures and libmodbus, a confusion matrix per claim kind, and a CI gate
at the NFR-009 targets (precision of `confirmed` ≥ 0.99, of `contradicted`
≥ 0.95). Assumption A12: the implementing agent drafts the labels from the
code, each with a file and line, and the reviewer agent checks a sample.
Open before it:

1. **How labels are stored**, so that a reviewer can check one against the
   code and a tool can run them. The claim schema allows no extra keys.
2. **What a label means.** The testing strategy listed calls inside
   `#ifdef` branches the flags do not compile as "expected to be
   `unverified`", and code in units that failed to parse likewise. A label
   that is an expected *status* mixes what the code says with what the
   verifier can see; a precision computed from it then counts a correct
   confirmation of a true claim as an error if the label said
   `unverified`.
3. **Where the gate runs.** Only CI's `libmodbus baseline` job has a
   libmodbus fact database; the fixtures can be analysed in any build.
4. **The open question on complete coverage** (B-27): a function declared
   only outside the analysis root (`printf`) is not recorded, so under
   complete coverage a true claim "`main` calls `printf`" is
   `contradicted` / `entity_not_found`.

Measured on the first draft of the set (312 claims, 192 of libmodbus,
2026-10-18) with the B-27 rules: precision of `confirmed` 1.00 (158 of
158), of `contradicted` 0.90 (113 of 125). Nine of the twelve false
contradictions were true `calls` claims whose callee is declared outside
the root (`memcpy`, `send`, `read`, `tcflush`, `strlcpy`, `strerror`,
`strlen` twice, `printf`); the other three are negated claims naming a
function or file that only an inactive `#if` branch has (below).

## Options considered

Label storage:

1. **Claims files plus a separate expectation file.** Valid claim files as
   they are; the label and its justification are far from the claim.
2. **One JSON Lines file per set**, a header line naming the target and,
   for a fixture, its units and flags, then one claim per line with its
   label, origin, `file:line` and one sentence. A reviewer can sample lines
   and read each against the code; a script writes the claims file.

Meaning of a label:

1. **The expected status**, as the strategy said. Mixes truth and
   visibility (above).
2. **The truth in the analysed configuration**: `confirmed` if the
   statement is true of the code as the compilation database compiles it,
   `contradicted` if false, `unverified` only if the code itself cannot
   settle it (a call through a pointer, whose target depends on run time;
   a name that denotes several functions of which the claim fits only
   some). Inactive `#if` branches are judged by the flags: libmodbus with
   `HAVE_STRLCPY` does not define its own `strlcpy`.

A callee that is not found:

1. **Keep `contradicted`.** Fails the gate (0.90).
2. **Record callees outside the root by name** (a fact-model change: a
   new kind of function row or edge, a schema version bump, new exports
   and bundles). Gives `confirmed` for `memcpy` and keeps invented callees
   `contradicted`; larger than B-29.
3. **`unverified` for every function that is not found.** Also loses the
   contradictions of invented callers, of `defines` and of `belongs_to`,
   where a function outside the root cannot make the claim true.
4. **`unverified` for the callee of a `calls` claim only**: the one
   position where a function outside the root can make the claim true.
   Invented callees (`modbus_rtu_check_crc`) become `unverified` too.

## Decision

<a id="adr0031-labelled-set-format-v1"></a>**Labelled sets** (option 2).
`tests/claims/labelled/<set>.jsonl`, UTF-8, one JSON object per line. The
first line is a header: `format` `"lcart-labelled-claims"`,
`format_version` 1, `target` `"libmodbus"` or `"fixture"`, and for a
fixture its directory under `tests/fixtures/` (`fixture`), its units and
its compile flags. Every other line is a label with exactly the keys `id`,
`kind`, `polarity`, `subject`, `object` (as in the claim schema), then
`expected` (`confirmed`, `unverified` or `contradicted`), `origin`
(`true`, `mutated` or `hard`: the sources 1 to 3 of the testing strategy),
`at` (the file and line or line range it was read from, relative to the
repository: `targets/libmodbus/...` at the pinned commit, the fixture, or
`scripts/libmodbus/lcart.toml` for a module rule) and `why` (one
sentence). The claim's `text` is `why`.

<a id="adr0031-labels-from-analysed-configuration-v1"></a>**A label is the
truth in the analysed configuration** (option 2), never lcart's output:
`unverified` only when the code cannot settle the claim. A claim the
verifier cannot see (a unit that failed to parse, an ambiguous name) keeps
its true label, so the confusion matrix shows the cost as `unverified`
verdicts, not as wrong labels.

<a id="adr0031-callee-not-found-v1"></a>**A callee that is not found is
`unverified`** (option 4). Under complete coverage the object of a
`calls` claim that resolves to no function is `unverified` /
`entity_not_found`, in both polarities; every other entity that is not
found stays `contradicted` / `entity_not_found`. Recording callees outside
the root (option 2) stays open: it would win back the invented callees.

<a id="adr0031-accuracy-gate-v1"></a>**The gate.**
`scripts/claim-accuracy.py` analyses each fixture set with `lcart analyze`
(one job, so the facts do not depend on scheduling), verifies every set
with `lcart verify`, and prints the confusion matrix per kind and overall,
the precision of `confirmed` and `contradicted`, the recall of
`contradicted` and the unverified rate, and every claim whose verdict
differs from its label. It fails (exit 1) below the NFR-009 precision
targets over all sets it ran, and, with the libmodbus set, when the set
has fewer than 200 claims, 30 of a kind or 60 false ones. It rejects
labels that break the format or cite a line past the end of their file
(exit 2). CTest runs it on the fixture sets on every preset
(`ClaimAccuracy.FixtureClaims`, with a self-test that a wrong label fails
the gate); the `libmodbus baseline` job runs it on both sets and writes
the result page (`docs/testing/verifier-accuracy.md`), which must not change
(as the module map). Python, as the other repository scripts: the gate
reads lcart's JSON report, so it needs no C++ of its own and links nothing.

## Consequences

- With the decision, on the same 312 claims: precision of `confirmed`
  1.00 (158 of 158), of `contradicted` 0.98 (108 of 110), recall of
  `contradicted` 0.91 (down from 0.95: invented callees are now
  `unverified`), unverified rate 0.14.
- The two false contradictions left are negated claims whose object exists
  only in an inactive branch ("`modbus-rtu-private.h` does not include
  `windows.h`", "`modbus.c` does not define `strlcpy`"): step 1's outcomes
  do not swap ([negation](../specs/claim-schema.md#claim-negation-rule-v2)).
  Recorded as an [open question](../open-questions.md#open-technical-questions);
  they cost 0.02 of precision.
- The any-error rule cost nothing on libmodbus (no error diagnostics); on
  the `broken_tu` set it leaves four false claims `unverified` (in
  `also_ok.c` and `ok.c`, which parsed). It stays an open question until a code base with errors is
  labelled.
- Excluded translation units are not measured: the libmodbus configuration
  excludes none. That part of the coverage question stays open.
- Every verifier bug found later adds labelled claims; a change to the
  verification rules shows up as a changed result page in the same pull
  request.
- Labels are drafted by an agent (A12); a wrong label found in the
  reviewer's sample sends its whole kind back for a re-read.
