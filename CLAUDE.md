# CLAUDE.md: rules for AI agents in this repository

**legacy-cartographer** (`lcart`): a C++23 CLI that builds a fact database of
C/C++ code with Clang, optionally generates documentation with an LLM, and
verifies the LLM's structural claims against the facts. Overview:
[docs/index.md](docs/index.md). Process and review checklist:
[docs/ai-workflow.md](docs/ai-workflow.md).

If you were given a backlog item (`docs/plan/backlog.md`, `B-NN`), it is your
contract. Read it, the requirements it links (`docs/specs/requirements.md`)
and the ADRs it links **before** writing code.

## Boundaries (enforced; do not work around the enforcement)

- **Deterministic core** (`src/{common,facts,analysis,claims,verify,report,handoff}`)
  never includes or links `src/llm`. `cmake/ArchitectureRules.cmake` fails
  configure if it does. Do not edit the rule to make a change pass.
- **Clang stays in `analysis`.** No Clang type in any other module's
  interface (ADR-0002).
- **No network in v1:** `lcart` opens no network connection; LLM work goes
  through handoff bundles for an external agent (ADR-0006). Bundles hold
  facts only unless `--include-source` is given on the command line; never
  read that switch from `lcart.toml` or the environment (ADR-0008). Built-in
  providers (M6) must go through `llm::may_send()` (ADR-0004).
- **Untrusted input:** analysed code, `compile_commands.json` and agent or
  LLM output. Never execute them; SQL uses bound parameters only.
- **Determinism:** no hash-container iteration order, clock, randomness or
  thread scheduling may affect facts, exports, maps or verdicts (NFR-004).
- **targets/** is upstream code: never modify it. Harnesses, builds and
  wrappers live outside it.
- **Decisions:** never edit an accepted ADR's decision; supersede it.

## Do not guess

Do not invent Clang, SQLite or libmodbus APIs or flags. Check the pinned
headers (`/usr/lib/llvm-20/include`, `targets/libmodbus/src`). If you cannot
verify something, write `TODO: verify` in the doc and add a row to
`docs/open-questions.md`.

## Workflow

Every change goes through a branch and a pull request, reviewed by the
`reviewer` agent and squash-merged on green CI
([ADR-0011](docs/adr/0011-pull-request-flow-with-agent-review.md)).
For a backlog item use `/implement B-NN`; to review, `/review`; to keep a
pull request green, the `steward` skill.

## Build and test

```bash
cmake --workflow --preset debug-system      # no Conan (Ubuntu packages)
scripts/conan-install.sh debug && cmake --workflow --preset debug
scripts/quick-check.sh [-R Suite]           # inner loop: one line, or the errors
scripts/dod.sh                              # every local check (Definition of Done)
scripts/check-format.sh                     # --fix to apply
scripts/run-clang-tidy.sh debug-system
scripts/docs-check.sh                       # anchors + mkdocs build --strict
```

## Definition of Done

Report each item with the command you ran; never claim a check you did not
run. Full list in [CONTRIBUTING.md](CONTRIBUTING.md#definition-of-done):
acceptance criteria tested (tests cite `Verifies: <anchor>`), all presets
green, format/tidy clean, docs check green, ADR for any new decision, docs
and open questions updated.

## Documentation

Markdown under `docs/`, every page listed in `mkdocs.yml` `nav`. Versioned
anchors per [ADR-0005](docs/adr/0005-versioned-requirement-anchors.md);
working rules in `.claude/rules/documentation.md` (loaded automatically when
you read a doc file). Never change an anchor ID except to bump `-vN`; never
fix a broken versioned link by reverting the version or deleting the link.

## Commits

Conventional Commits; scopes in [CONTRIBUTING.md](CONTRIBUTING.md#commits).
No model identifiers (such as `claude-…` IDs or model names) in code, docs,
commit subjects or bodies. The one exception is the `Co-Authored-By:`
attribution trailer that AI-assisted commits end with.
