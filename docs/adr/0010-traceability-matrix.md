# 0010. Generated traceability matrix instead of a formal standard's structure

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [NFR-012](../specs/requirements.md#nfr012-traceability-v2)

## Context

ADR-0005 gives every requirement a versioned ID and makes tests cite those
IDs. The open question was whether requirements should also follow the
structure of a formal software lifecycle standard used in regulated
domains. Such standards ask for evidence that each requirement is verified
and that changes are traced; they do not prescribe a document format. This
project is a tool, not a regulated product, but it is meant to show how such
evidence can be produced cheaply.

## Options considered

1. **IDs and anchors only** (ADR-0005). Links exist; nobody sees the whole
   picture without reading every test.
2. **Restructure specs after a standard's clause structure.** Heavy, ties
   the documents to one standard and one domain, adds no checking.
3. **Generate a traceability matrix** from what already exists: requirement
   anchors, `Verifies:` citations in tests, and CTest results.

## Decision

<a id="adr0010-generated-matrix-v1"></a>CI generates a **traceability
matrix**: one row per requirement (ID, version, milestone, status), with the
tests that cite it and their last result. It is published as a page of the
documentation site (`docs/plan/traceability.md`, generated, not edited by
hand) and as CSV for import into other tools.

<a id="adr0010-matrix-gates-v1"></a>The matrix is also a gate: CI fails if a
requirement of a milestone marked done has no citing test, if a citing test
fails, or if a test cites a version of a requirement that no longer exists.

<a id="adr0010-no-standard-structure-v1"></a>Requirements keep the EARS
format and IDs of [requirements](../specs/requirements.md); they are not
restructured after any standard. A mapping to a specific standard can be
added later as an extra column without changing the specs.

## Consequences

- The evidence an auditor asks for ("which tests verify FR-004 v1, and did
  they pass on this commit?") is one page, regenerated on every commit.
- A stale citation (test still citing `-v1` after a bump to `-v2`) fails
  CI, which forces the review the version bump rule asks for.
- Work: an extension of `scripts/check-doc-anchors.py` plus a CTest result
  reader (B-15).
