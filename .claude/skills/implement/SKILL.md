---
name: implement
description: Drive one legacy-cartographer backlog item (B-NN in docs/plan/backlog.md) from branch to merge - tests first, implementation, local Definition of Done, pull request, reviewer agent, CI to green, squash merge (ADR-0011). Use when asked to implement, do, continue with or pick up B-NN.
argument-hint: "<backlog item, e.g. B-08>"
---

# Implement a backlog item

The flow of [ADR-0011](../../../docs/adr/0011-pull-request-flow-with-agent-review.md).
`$ARGUMENTS` is the item, e.g. `B-08`.

## 1. Start

- Read the item in `docs/plan/backlog.md`, every requirement and ADR it
  links. Check its dependencies are ✅; if not, stop and say which blocks it.
- Ambiguous item or a new decision needed: stop and ask; a decision becomes
  an ADR (copy `docs/adr/template.md`), not a code comment.
- Branch from fresh `main`:
  `git fetch origin main && git switch -c b/NN-short-name origin/main`.

## 2. Tests first, then code

Write the acceptance tests (citing anchors), run them, keep the failure
output for the pull request. Then the smallest change that passes. Inner
loop: `scripts/quick-check.sh [-R <Suite>]`. Hand the work to the
`developer` agent when a fresh context helps; give it the item number and
any decision from this conversation, not pasted docs.

## 3. Local Definition of Done

`scripts/dod.sh` until every line is PASS. Update the docs the item
touches: backlog result paragraph and ✅, requirements, data model,
open questions. Commit (Conventional Commits, `Backlog: B-NN`), push with
`git push -u origin <branch>`.

## 4. Pull request

Open it against `main` with `.github/pull_request_template.md` filled in:
title `type(scope): summary (B-NN)`, the quoted test failure, the `dod.sh`
summary. Then subscribe to its activity.

## 5. Review

Run the `reviewer` agent on the branch. Post its findings as one
`COMMENT` review on the pull request (a review from the owner's account
cannot approve), inline where a line is named. Fix every blocker and
major, and plainly correct nits; answer the rest in the thread. If a
`developer` agent wrote the code, send it the findings with `SendMessage`.
Re-run `scripts/dod.sh` before each push.

## 6. Merge

Merge (squash; subject = pull-request title, body = `Backlog: B-NN` and the
`Co-Authored-By:` trailer, see CLAUDE.md "Commits") only when CI is green
on the head commit, no blocker or major is open, and the owner has not
asked to hold. If the pull request adds an ADR or adds or bumps a versioned
anchor in `docs/specs/` or `docs/plan/`, also wait for the owner's explicit
approval and ask for it in one line
([ADR-0011](../../../docs/adr/0011-pull-request-flow-with-agent-review.md#adr0011-owner-approves-decisions-v1)). Then
`git switch main && git pull`, delete the branch, and tell the owner in
two or three lines what landed and what is next.
