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
| [0009](0009-linux-and-wsl2-only.md) | Linux and WSL2 only; MSVC code analysed in Clang's MSVC mode | Accepted (TODO on flags and clang-cl resolved by 0021; the MSBuild way to a database by 0022) |
| [0010](0010-traceability-matrix.md) | Generated traceability matrix instead of a formal standard's structure | Accepted |
| [0011](0011-pull-request-flow-with-agent-review.md) | Pull-request flow with agent review and merge on green CI | Accepted |
| [0012](0012-function-identity-for-internal-linkage.md) | Function identity: Clang's USR, path-prefixed for internal linkage | Accepted (extended to duplicate external definitions by 0013) |
| [0013](0013-functions-with-external-linkage-defined-twice.md) | A function with external linkage defined twice is two functions | Accepted |
| [0014](0014-path-globs-in-the-configuration.md) | Path globs in the configuration | Accepted |
| [0015](0015-ascii-configuration-while-tomlplusplus-3-4.md) | lcart.toml is ASCII while toml++ 3.4.0 is in use | Accepted |
| [0016](0016-facts-export-and-command-line.md) | Facts export, command line and the git exposure warning | Accepted; option syntax amended by 0017 (a lone `-` value for `--output`), export output replacement by 0018; analyze resume, `--jobs` and exit status 5 by 0019 |
| [0017](0017-module-map-format-and-command-line.md) | Module map: command line, content and escaping | Accepted |
| [0018](0018-facts-export-replaces-only-exports.md) | Facts export replaces only exports | Accepted |
| [0019](0019-parallel-resumable-analysis.md) | Parallel, resumable analysis with one writer | Accepted; TU list (one per fact path) and second signal (either kind) amended by 0020; the file of the run lock by 0024 |
| [0020](0020-unit-list-and-second-signal.md) | One TU per fact path; a second signal of either kind ends lcart | Accepted |
| [0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md) | Clang flags and clang-cl compilation databases for MSVC code | Accepted; "registers no LLVM targets" amended by 0025 (a target prefix in the program name is now used); configuration-file consequence resolved by 0023 |
| [0022](0022-msbuild-logger-for-compilation-databases.md) | A compilation database from an MSBuild solution: an MSBuild logger | Accepted |
| [0023](0023-clang-configuration-files-ignored.md) | Clang configuration files are ignored | Accepted (decided by delegation) |
| [0024](0024-run-lock-on-a-separate-file.md) | The run lock of an analysis is on a separate file | Accepted (decided by delegation) |
| [0025](0025-llvm-targets-registered.md) | LLVM targets are registered before the first parse | Accepted (decided by delegation) |
