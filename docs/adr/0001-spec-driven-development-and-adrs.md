# 0001. Spec-driven development and ADRs

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [NFR-012](../specs/requirements.md#nfr012-traceability-v1)

## Context

The project is built by one engineer with a few hours a week, with help from
LLM coding agents. Work happens in short sessions, often weeks apart. Two
risks follow:

- Context is lost between sessions: why something was built a certain way
  lives only in someone's memory or in a chat transcript.
- LLM agents implement whatever they are asked, plausibly. Without a written
  definition of "correct", review cannot tell a right implementation from a
  convincing wrong one.

The tool itself is about traceable documentation; its own repository should
demonstrate the practice.

## Options considered

1. **Code first, document later.** Fastest start; documentation goes stale
   and decisions are reconstructed from code.
2. **Issue tracker only.** Good for tasks, poor for decisions; not versioned
   with the code, not available offline to agents.
3. **Specs, plans and ADRs in the repository, before code.** Slower start;
   every change has a written contract that agents and reviewers can read.

## Decision

<a id="adr0001-spec-before-code-v1"></a>**Spec before code.** Requirements
(FR/NFR with acceptance criteria) and a backlog item exist before
implementation starts. A change refers to the backlog item and the
requirements it implements; acceptance criteria are turned into tests first.

<a id="adr0001-adr-for-decisions-v1"></a>**ADRs for decisions.** Any decision a
reasonable engineer could have made differently (dependency, format,
protocol, threading rule, security policy) gets an ADR in `docs/adr/`.
Accepted decisions are superseded, never edited.

<a id="adr0001-docs-in-repo-v1"></a>**Documentation lives in the
repository** as Markdown under `docs/`, built with MkDocs in strict mode in
CI, so a missing page or broken link fails the build.

## Consequences

- The first milestone (M0) produces no features, only the skeleton and the
  documents.
- Changing a requirement is an explicit, reviewable edit (and a version bump,
  ADR-0005).
- Agents get a stable, repository-local context (`CLAUDE.md`, specs, ADRs)
  instead of conversation history.
- Overhead: small changes still need a backlog reference. Trivial fixes
  (typos, formatting) are exempt.
