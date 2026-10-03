# Contributing

This project is developed spec-first
([ADR-0001](docs/adr/0001-spec-driven-development-and-adrs.md)). Code changes
start from a backlog item in [docs/plan/backlog.md](docs/plan/backlog.md)
that links to the requirements it implements.

## Workflow

1. Pick or write a backlog item with acceptance criteria. If it needs a
   decision someone could reasonably make differently, write an ADR
   (copy [docs/adr/template.md](docs/adr/template.md)).
2. Turn the acceptance criteria into tests; run them and see them fail.
   Each test cites the requirement it verifies:
   `// Verifies: fr004-record-call-edges-v1`.
3. Implement the smallest change that makes them pass.
4. Run the Definition of Done (below) and report each item with the command
   you ran.
5. Open a pull request that names the backlog item and lists what was not
   checked, if anything.

LLM-assisted changes follow the same process; the review checklist is in
[docs/ai-workflow.md](docs/ai-workflow.md).

## Definition of Done

- [ ] Acceptance criteria pass; tests cite requirement anchors.
- [ ] `cmake --workflow --preset <p>` passes for `debug`, `clang-debug`,
      `release`, `asan-ubsan` (or `debug-system` where Conan is unavailable).
- [ ] `scripts/check-format.sh` and `scripts/run-clang-tidy.sh` are clean.
- [ ] `scripts/docs-check.sh` passes; changed requirements have their `vN`
      bumped ([ADR-0005](docs/adr/0005-versioned-requirement-anchors.md)).
- [ ] Roadmap checkboxes, architecture docs and open questions updated.
- [ ] No `TODO` without a reference (`TODO(B-07)`, `TODO(M2)`); no `NOLINT`
      without a reason on the same line.

## Conventions

- C++20, formatted by `.clang-format`, checked by `.clang-tidy` (naming
  rules included): `snake_case` functions and variables, `CamelCase` types,
  trailing `_` on private members, namespaces `lcart::<module>`.
- Headers under `src/<module>/`, included as `"<module>/<file>.hpp"`.
- `[[nodiscard]]` on functions returning `Result`, `std::optional` or a
  status `bool`.
- Exhaustive `switch` over enums without `default:`, followed by
  `common::unreachable()`.
- Comments explain *why*; public interfaces document their contract with
  `///`.
- The deterministic core (`common`, `facts`, `analysis`, `claims`,
  `verify`, `report`) never depends on `llm`. The build enforces it.
- Never modify anything under `targets/`.

## Commits

[Conventional Commits](https://www.conventionalcommits.org/):
`type(scope): summary`, imperative, ≤ 72 characters.

- Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`,
  `chore`.
- Scopes: `facts`, `analysis`, `llm`, `claims`, `verify`, `report`, `cli`,
  `testgen`, `fuzz`, `cmake`, `conan`, `targets`, `adr`, `specs`.
- The body says *why* and references the backlog item (`Backlog: B-07`) and
  ADRs.
- Every commit builds and passes its tests.

## Licence of contributions

Contributions are accepted under Apache-2.0 (section 5 of the licence).
