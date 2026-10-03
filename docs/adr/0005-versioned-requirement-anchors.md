# 0005. Versioned anchors and requirement-to-test traceability

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [NFR-012](../specs/requirements.md#nfr012-traceability-v1)

## Context

Requirements, backlog items, ADRs and tests refer to each other. Prose
references ("see FR-4") break silently when the item changes or is
renumbered, and links to a whole file do not say which rule is meant. When
specs are executed by LLM agents, a stale reference becomes a plausible wrong
implementation. In regulated work, being able to show which test covers
which requirement is expected.

## Options considered

1. **IDs in prose only.** No tooling; references rot.
2. **Requirements-management tool.** Strong traceability; outside the
   repository, heavy for one person.
3. **Versioned HTML anchors in Markdown, checked by a script.** Works on
   GitHub and in MkDocs; cheap; checkable in CI.

## Decision

<a id="adr0005-anchor-format-v1"></a>**Anchor format.** Every requirement,
backlog item, milestone and every rule that other documents link to carries
an anchor `<a id="<doc>-<slug>-v<N>"></a>`:

```text
<doc>  = fr001 | nfr001 | b01 | m0 | adr0001 | arch | claim | testing | security
slug   = 1–6 lowercase kebab-case words saying what is required
N      = content version, starting at 1
```

Items with a public ID also show a visible marker right after the anchor,
repeating ID and version: `**[FR-004 v1]**`, `**[B-07 v1]**`, `**[M1 v1]**`.

<a id="adr0005-version-bump-rule-v1"></a>**Version bump rule.** An ID never
changes except for its `-vN`. When the *meaning* of an anchored item changes,
bump `N` in the anchor and the marker, then review every link to the old
version: move it to the new version only if the referring text is still
true. Typo and formatting fixes do not bump. A broken versioned link is
never "fixed" by reverting the version or deleting the link.

<a id="adr0005-tests-cite-requirements-v1"></a>**Tests cite requirements.**
A test that verifies a requirement says so in a comment:
`// Verifies: fr004-record-call-edges-v1`. From M1, the anchor checker
reports requirements of completed milestones that no test cites (B-15).

<a id="adr0005-checked-in-ci-v1"></a>**Checked in CI.**
`scripts/check-doc-anchors.py` checks format, uniqueness and marker
consistency; MkDocs (strict, `validation.anchors: warn`) checks that every
linked anchor exists. Both run in `scripts/docs-check.sh`.

## Consequences

- Links point at the exact rule, and a changed rule visibly breaks links
  that were not reviewed.
- A little ceremony per requirement edit.
- Kept deliberately simple: plain Markdown, no build-time link rewriting.
