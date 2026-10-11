# Open questions and assumptions

Assumptions made while writing M0, questions that need an answer from the
maintainer, and facts that could not be verified yet. Close an item by
moving the answer into the relevant spec or ADR and deleting it here.

## Assumptions

| # | Assumption | Where it matters | Revisit when |
|---|---|---|---|
| A1 | Repository `legacy-cartographer`, executable `lcart`, namespace `lcart::`. | everywhere | before going public |
| A2 | Apache-2.0 for own code. | [ADR-0003](adr/0003-licensing.md) | – |
| A4 | libmodbus pinned at v3.2.0 (latest tag on 2026-10-03). | [libmodbus plan](targets/libmodbus.md) | next upstream release |
| A5 | One SQLite file per analysed project; no server. | [data model](architecture/data-model.md) | M1 end |
| A7 | Conan manages small libraries only (SQLite, nlohmann_json, GoogleTest, later toml++); a `debug-system` preset builds from Ubuntu packages for offline machines. | `conanfile.py`, presets | B-02 |
| A8 | C4 diagrams as Mermaid flowcharts, not Mermaid's experimental `C4Context`. | [architecture](architecture/index.md) | – |
| A9 | CodeQL workflow is manual-only while private: it needs GitHub Advanced Security. | `.github/workflows/codeql.yml` | at publication |
| A10 | Verification covers only structural claims (5 kinds); behaviour stays prose. | [claim schema](specs/claim-schema.md) | M2 end |
| A11 | The libmodbus `tests/` directory is its own `tests` module (by the default rule; kept in B-14). | [libmodbus plan](targets/libmodbus.md) | M3 |

## Decided

| Question | Decision | Where |
|---|---|---|
| C++20 or C++23 | C++23 with GCC 14 and Clang/LLVM 20 (Clang 18 lacks `std::expected` with libstdc++); wrapper API stays C++20 | [ADR-0007](adr/0007-cpp23-and-toolchain-baseline.md) |
| Configuration format | TOML (`lcart.toml`, toml++) | [ADR-0008](adr/0008-configuration-file-format.md) |
| LLM in v1 | external coding agent via handoff bundle with a ready prompt; built-in providers and local models in M6 | [ADR-0006](adr/0006-external-agent-handoff-for-llm-work.md) |
| Publication | private until M1 is done, then public with Pages and CodeQL | [roadmap](plan/roadmap.md#publication) |
| HTTP client | not needed before M6; decided then | [M6](plan/roadmap.md#m6-built-in-providers-v1) |
| Platform / MSVC code | Linux and WSL2 only; MSVC code parsed by Clang in MSVC mode | [ADR-0009](adr/0009-linux-and-wsl2-only.md) |
| Clang flags for MSVC code; clang-cl commands in a compilation database | the database is the only source of flags (`--target=x86_64-pc-windows-msvc` implies the MS flags); clang-cl commands analysed as they are, with `--driver-mode=cl` made explicit; `/clang:` arguments filtered | [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md) |
| Traceability format | IDs + versioned anchors, plus a generated traceability matrix that also gates CI; no restructuring after a standard | [ADR-0010](adr/0010-traceability-matrix.md) |
| Path glob syntax for module rules and exclusions | `*` within a directory, `**` across directories, anchored at the root; exclusions select translation units | [ADR-0014](adr/0014-path-globs-in-the-configuration.md) |
| Undefined behaviour in toml++ 3.4.0 on malformed input | `lcart.toml` is ASCII and toml++'s assertions are off until a fixed release is packaged | [ADR-0015](adr/0015-ascii-configuration-while-tomlplusplus-3-4.md) |
| Paths of files outside the analysis root in the facts export | the spelling of the first directive that includes the file, `#2`... if taken, no hash; translation unit and diagnostic paths stored like file paths, diagnostics linked to their file | [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1) (Accepted) |
| Function with external linkage defined in several translation units | each definition is a function with its own calls; the analysis warns | [ADR-0013](adr/0013-functions-with-external-linkage-defined-twice.md) |
| Performance for large code bases | hours or days are acceptable; the tool must use all cores, show progress and resume interrupted runs; incremental re-analysis later | [NFR-005](specs/requirements.md#nfr005-performance-budget-v3) |

## Questions for the maintainer

None at the moment. The question whether to try NFR-013 criterion 3 on a
Windows machine before M1 is done was answered by doing it on 2026-10-11
([ADR-0022](adr/0022-msbuild-logger-for-compilation-databases.md)).

## Open technical questions

| Question | Context | Decide by |
|---|---|---|
| Detect source files edited between an interrupted analysis and `--resume` (check `translation_unit.inputs_sha256`)? Today a resumed run mixes old and new code. | [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1) | M2 |
| Run each translation unit in a child process so a Clang crash cannot end the run? Costs process start-up per TU. | [security T5](security.md), [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-interrupt-v1) | M2 (one process for now; a TU that always crashes Clang blocks `--resume`) |
| How to show paths of files outside the analysis root (stored absolute, e.g. Clang's resource directory or a home directory) in handoff bundles? The export's rule (spelling of the first directive, [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)) is the candidate. | [file facts](architecture/data-model.md#arch-data-model-file-facts-v1), [security](security.md) | M2 (bundles) |
| With an MSVC target before C++20, Clang defaults to `-fdelayed-template-parsing`, and calls in never-instantiated function templates are not recorded (B-17 spike). Append `-fno-delayed-template-parsing` to every MSVC command? It changes the facts, and code that only MSVC's lenient template parsing accepts would fail to parse. | [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md), [how-to](how-to/msvc-under-wsl2.md) | M2 |
| The Clang driver inside lcart reads configuration files: `--config=<file>`, and default ones such as `<triple>-clang.cfg` in the directory of the program a command names (both shown in the B-17 spike). Their arguments bypass `remove_code_loading_arguments()` (plugins are not loaded by LibTooling today, ADR-0021 finding 4, but the filter should not depend on that). Remove `--config` and add `--no-default-config`? | [security T5](security.md#security-malicious-build-input-v1), [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md) | M1 end |
| Why does clang-tidy 20.1.2 ignore `HeaderFilterRegex` from `.clang-tidy` in the main clone (findings in `src/` headers dropped as non-user code, while `--dump-config` shows the filter) but apply it in a git worktree? `scripts/run-clang-tidy.sh` passes the filter explicitly, so nothing depends on the answer. | [B-24](plan/backlog.md#b24-clang-tidy-checks-headers-v1) | when clang-tidy is upgraded |
| Register the LLVM X86 target in `lcart`? Without any target, a Win32 (`i686`) translation unit that includes `<windows.h>` fails: `winnt.h` has MS-style inline assembly, and Clang reports "Unable to find target for this triple (no targets are registered)" (tried on Windows with the sample solution; x64 is not affected). Registering targets also changes what ADR-0021's `inferTargetAndDriverMode()` wrapper may add. | [ADR-0022](adr/0022-msbuild-logger-for-compilation-databases.md), [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md), [how-to](how-to/msvc-under-wsl2.md) | M2 |
| On a WSL2 drive mount (drvfs, file system type `v9fs`) `lcart analyze` with the default database `lcart-out/facts.lcart.db` under the root fails with "database is locked" (tried). Default the database elsewhere, detect the mount and say so, or leave it to the how-to (`--db` on the Linux file system)? | [NFR-013](specs/requirements.md#nfr013-linux-and-wsl2-v1), [how-to](how-to/msvc-under-wsl2.md) | M2 |
| The repository has no `.gitattributes`. A checkout on Windows with `core.autocrlf=true` has CRLF line endings, and 11 tests fail when it is built (golden files, file hashes). Add `* text=auto eol=lf` (or `eol=lf` for fixtures and golden files)? | [how-to](how-to/msvc-under-wsl2.md) | M1 end |

## Not verified yet ("TODO: verify")

| Item | Where | Verified by |
|---|---|---|
| Seam for feeding frames to libmodbus through the public API only | [testing strategy](testing/strategy.md#testing-characterization-v1) | M3 |
| `modbus_mapping_new` signature and per-iteration cost for fuzzing | [libmodbus plan](targets/libmodbus.md) | M4 |
| `modbus_close` / `modbus_free` semantics for the RAII wrapper | [libmodbus plan](targets/libmodbus.md) | M5 |
| Exact terms of `tests/LICENSE` in libmodbus | [ADR-0003](adr/0003-licensing.md) | before reusing any upstream test code |
| The MSBuild logger on other setups than Build Tools 2022 with x64 and Win32 `v143` projects: older Visual Studio, other platform toolsets, C++/CLI, custom build steps, a build with compile errors, `.rc`/`.idl` files, the `arm` and `arm64` branches. Its CI check uses the database it wrote once; `tools/msbuild-compdb/verify.ps1` needs Windows. | [ADR-0022](adr/0022-msbuild-logger-for-compilation-databases.md), [how-to](how-to/msvc-under-wsl2.md) | a Windows machine with the setup |
| Windows SDK and MSVC headers through `/winsysroot`, `/vctoolsdir` + `/winsdkdir`, `-Xmicrosoft-windows-sys-root`, `-isystem`; headers copied to the Linux file system instead of read through `/mnt/c`; case of `#include` names against file names on drvfs; whether a WSL2 `PATH` with Windows directories lets Clang find an MSVC installation. (`/imsvc` with `/mnt/c` is tried.) | [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-windows-headers-v1), [how-to](how-to/msvc-under-wsl2.md) | a machine with the Windows SDK and MSVC |
| MkDocs/Material settings that keep raw HTML in generated docs from executing | [security](security.md) | M2 |
| Usable local model sizes for claim generation | [NFR-003](specs/requirements.md#nfr003-fully-local-mode-v1) | M6 |
| Claude Code permission rule syntax for a bundle's `.claude/settings.json` | [handoff contract](specs/handoff-contract.md#claim-agent-permissions-v1) | M2 |
| Validating agent diagrams with Mermaid in `lcart ingest`. Parsing without a browser works: B-22 runs Mermaid's own `mermaid.parse` under Node.js with jsdom as the only DOM ([B-22 result](plan/backlog.md#b22-mermaid-check-in-ci-v1)). Open: whether `lcart` may depend on Node.js at run time for it (an ADR), and that a parse is weaker than a render (Mermaid 11.17.2 parsed a quoted label with an unescaped `"` inside it). | [FR-008](specs/requirements.md#fr008-generate-documentation-v2) | M2 |
