---
name: steward
description: Conventions for driving a legacy-cartographer pull request to green - each CI job and its local command, known failure causes, review handling and merge rule. Use when watching a pull request, fixing a CI failure or answering review comments in this repository.
---

# Pull-request steward

## CI jobs and their local commands

| CI job | Local command |
|---|---|
| `C++ debug`, `clang-debug`, `release`, `asan-ubsan` | `scripts/conan-install.sh <p> && cmake --workflow --preset <p>` (needs Conan Center) |
| `C++ <p> (no Conan)` | `cmake --workflow --preset <p>` with `<p>` = `debug-system` or `asan-ubsan-system` |
| `Targets pinned and clean` | `scripts/check-targets.sh` |
| `Format + clang-tidy` | `scripts/check-format.sh`, `scripts/run-clang-tidy.sh debug-system` |
| `Anchors + mkdocs build --strict` | `scripts/docs-check.sh` |

Reproduce locally first, show the same command passing, then push.
`scripts/dod.sh` runs all of them except the Conan presets.

## Known failure causes

- **Format or tidy red on CI, green locally:** the new file was untracked
  (the scripts use `git ls-files`). `git add -N` it; `scripts/dod.sh` does.
- **Conan preset red, system presets green:** a Conan package version
  differs from Ubuntu's (`conanfile.py` pins; SQLite 3.46 vs 3.45). Read
  the job log before changing code.
- **No Conan Center behind a proxy:** use the `*-system` presets locally;
  CI covers the Conan ones.
- **clang-tidy `bugprone-unchecked-optional-access` in tests:** gtest
  `ASSERT_*` is not understood by the check; compare whole values
  (`EXPECT_EQ(opt, expected)`) instead of dereferencing.

A failing test is not a flake until the same test passes on a rerun of the
same commit. Never skip, disable or weaken a test.

## History and reviews

- Own branches (`b/*`, `chore/*`, ...): add commits, no force-push unless
  the owner asks. `main` gets only squash merges.
- Small asks: implement, push, reply, resolve. Interface, format or
  decision changes: reply with a proposal; a decision change needs an ADR.
- Merge rule: [ADR-0011](../../../docs/adr/0011-pull-request-flow-with-agent-review.md#adr0011-merge-on-green-v1).
