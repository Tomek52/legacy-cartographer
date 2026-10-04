---
name: review
description: Review a legacy-cartographer change (current diff, a branch or a pull request number) with the reviewer agent and its checklist, and report verified findings by severity; optionally post them on the pull request. Use when asked to review or check a change before merge.
argument-hint: "[PR number | branch | empty for the current diff]"
---

# Review

1. **Target.** Empty: `git diff origin/main...HEAD` plus uncommitted
   changes. A branch: `git diff origin/main...<branch>`. A pull request:
   read it with the GitHub tools (description, diff, head commit) and check
   out its branch.
2. **Run the `reviewer` agent** with the diff range, the backlog item and
   the pull-request description. It reads the rest itself; do not paste
   the diff or the docs into its prompt.
3. **Check its report.** Every finding has `path:line` and a failure
   scenario; anything without evidence becomes a question.
4. **Deliver.** Locally: print findings, blockers first, and the verdict.
   On a pull request (when asked, or inside the `implement` flow): one
   review with event `COMMENT`, inline comments on the named lines.
5. If the review caught an LLM mistake worth remembering, add it to
   **Lessons learned** in `docs/ai-workflow.md`.
