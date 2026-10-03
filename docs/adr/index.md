# Architecture Decision Records

Decisions that shape the tool, in [Michael Nygard's format](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions):
**Context → Decision → Consequences**, plus the options considered.

Rules:

- An accepted ADR's decision is never edited. A new ADR supersedes it, and
  the old one's status says "Superseded by NNNN".
- Status values: Proposed, Accepted, Superseded, Rejected. Promote
  Proposed → Accepted once the decision is in use (a decision the repository
  already enforces is not "Proposed").
- To add one: copy the [template](template.md), take the next number, add it
  to the table below and to `nav` in `mkdocs.yml`.

| # | Decision | Status |
|---|---|---|
| [0001](0001-spec-driven-development-and-adrs.md) | Spec-driven development and ADRs | Accepted |
| [0002](0002-static-analysis-frontend.md) | Clang LibTooling as the analysis frontend | Accepted |
| [0003](0003-licensing.md) | Apache-2.0 for own code; LGPL-2.1 obligations for libmodbus | Accepted |
| [0004](0004-llm-provider-abstraction-and-code-privacy.md) | LLM provider abstraction and code privacy | Accepted |
| [0005](0005-versioned-requirement-anchors.md) | Versioned anchors and requirement-to-test traceability | Accepted |
