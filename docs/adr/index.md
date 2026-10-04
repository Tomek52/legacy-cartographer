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
| [0002](0002-static-analysis-frontend.md) | Clang LibTooling as the analysis frontend | Accepted (LLVM version superseded by 0007) |
| [0003](0003-licensing.md) | Apache-2.0 for own code; LGPL-2.1 obligations for libmodbus | Accepted |
| [0004](0004-llm-provider-abstraction-and-code-privacy.md) | LLM provider abstraction and code privacy | Accepted (providers deferred to M6 by 0006) |
| [0005](0005-versioned-requirement-anchors.md) | Versioned anchors and requirement-to-test traceability | Accepted |
| [0006](0006-external-agent-handoff-for-llm-work.md) | LLM work through an external agent handoff in v1 | Accepted |
| [0007](0007-cpp23-and-toolchain-baseline.md) | C++23 and toolchain baseline: GCC 14, Clang/LLVM 20 | Accepted |
| [0008](0008-configuration-file-format.md) | Project configuration in TOML | Accepted |
| [0009](0009-linux-and-wsl2-only.md) | Linux and WSL2 only; MSVC code analysed in Clang's MSVC mode | Accepted |
| [0010](0010-traceability-matrix.md) | Generated traceability matrix instead of a formal standard's structure | Accepted |
| [0011](0011-pull-request-flow-with-agent-review.md) | Pull-request flow with agent review and merge on green CI | Accepted |
| [0012](0012-function-identity-for-internal-linkage.md) | Function identity: Clang's USR, path-prefixed for internal linkage | Accepted (extended to duplicate external definitions by 0013) |
| [0013](0013-functions-with-external-linkage-defined-twice.md) | A function with external linkage defined twice is two functions | Accepted |
