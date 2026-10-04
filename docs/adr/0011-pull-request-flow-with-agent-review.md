# 0011. Pull-request flow with agent review and merge on green CI

- **Status:** Accepted
- **Date:** 2026-10-04
- **Requirements:** [NFR-012](../specs/requirements.md#nfr012-traceability-v2)
- **Supersedes / superseded by:** —

## Context

Until B-06, changes were committed straight to `main`. That leaves no
place for a review before the change lands, and CI only reports after the
fact. The project has one engineer with 4–6 hours a week and LLM coding
agents doing most of the implementation, so every step that waits for the
human slows the project down. A sibling project used six subagents, six
skills and separate task spec files; most of that was more process than a
project of this size needs.

GitHub constraints: the agents act through the owner's GitHub account, so
a review they post cannot approve the owner's own pull request; branch
protection and rulesets are not available for this private repository on
the current plan.

## Options considered

1. **Commit to `main`** (status quo). Fastest; no review before landing.
2. **Pull requests, human approves every merge.** Strongest control; every
   backlog item waits for the human, who then mostly re-reads what CI and
   the reviewer already checked.
3. **Pull requests, agent review, merge on green CI.** Every change gets an
   independent review and a full CI run before it lands; the human reads
   pull requests when they choose and can stop a merge with a comment.

## Decision

<a id="adr0011-pr-flow-v1"></a>**Every change goes through a pull request.**
One branch per backlog item, `b/NN-short-name` (other changes:
`<type>/short-name`, e.g. `chore/agent-workflow`), opened against `main`
with the template in `.github/pull_request_template.md`.

<a id="adr0011-agent-review-v1"></a>**Each pull request is reviewed by the
`reviewer` agent** (`.claude/agents/reviewer.md`), which has no edit tools
and works from the checklist in [ai-workflow](../ai-workflow.md). Its
findings are posted on the pull request as a `COMMENT` review. Every
blocker and major finding is fixed or answered before merge.

<a id="adr0011-merge-on-green-v1"></a>**The agent driving the pull request
merges it** (squash, pull-request title as the commit subject) when all of
these hold: CI green on the head commit, no unresolved blocker or major
finding, no open question or "hold" from the owner on the pull request.
The owner may merge, hold or revert at any time.

Two roles only: `developer` (implements a backlog item) and `reviewer`.
Backlog items are the specs; there are no separate task files. Agent files
name no model and inherit the session's.

## Consequences

- Every commit on `main` is one reviewed, CI-green backlog item; `git log`
  reads as the backlog.
- The merge rule is a convention until the repository is public; then a
  ruleset requiring a pull request and the CI checks is added (M1
  Definition of Done).
- Review by an agent of the same model family can share its blind spots;
  the owner's spot reviews and CI's sanitizers and tests remain the backstop.
  Revisit if reviews miss defects that later surface (record them under
  lessons learned in [ai-workflow](../ai-workflow.md)).
- Squash merges lose the individual commits of a branch; a branch that
  needs separate commits on `main` is split into several pull requests.
