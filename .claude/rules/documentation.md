---
paths:
  - "docs/**/*.md"
  - "README.md"
  - "CLAUDE.md"
  - "CONTRIBUTING.md"
---

# Documentation rules

Decision: docs/adr/0005-versioned-requirement-anchors.md.

- Every new page goes into `nav` in `mkdocs.yml`; every directory under
  `docs/` has an `index.md`. `scripts/docs-check.sh` must pass.
- Anchor IDs: `<doc>-<slug>-v<N>`, `<doc>` one of `fr001`, `nfr001`, `b01`,
  `m0`, `adr0001`, `arch`, `claim`, `testing`, `security`. The slug says
  what is required (1–6 kebab-case words), never a position.
- Requirements, backlog items and milestones: anchor and marker at the start
  of the heading text:
  `#### <a id="fr004-record-call-edges-v1"></a>**[FR-004 v1]** Record call edges`.
  Other anchored items: only `<a id="..."></a>`, first thing in the line.
- Link to the specific item (`requirements.md#fr004-record-call-edges-v1`),
  not the whole file, when the text names it.
- Meaning changed → bump `vN` in anchor and marker, run the docs check, and
  review every link to the old ID; update a link only if the referring text
  is still true. Typos and formatting do not bump.
- Concise, concrete, no marketing. Facts about Clang or libmodbus need a
  source (header, file, tag) or a `TODO: verify` plus an open-questions row.
