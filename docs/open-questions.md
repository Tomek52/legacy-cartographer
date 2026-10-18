# Open questions and assumptions

Assumptions made while writing M0, questions that need an answer from the
maintainer, and facts that could not be verified yet. Close an item by
moving the answer into the relevant spec or ADR and deleting it here.

## Assumptions

| # | Assumption | Where it matters | Revisit when |
|---|---|---|---|
| A1 | Repository `legacy-cartographer`, executable `lcart`, namespace `lcart::`. Public under this name since 2026-10-11. | everywhere | – |
| A2 | Apache-2.0 for own code. | [ADR-0003](adr/0003-licensing.md) | – |
| A4 | libmodbus pinned at v3.2.0 (latest tag on 2026-10-03). | [libmodbus plan](targets/libmodbus.md) | next upstream release |
| A5 | One SQLite file per analysed project; no server. | [data model](architecture/data-model.md) | M1 end |
| A7 | Conan manages small libraries only (SQLite, nlohmann_json, GoogleTest, later toml++); a `debug-system` preset builds from Ubuntu packages for offline machines. | `conanfile.py`, presets | B-02 |
| A8 | C4 diagrams as Mermaid flowcharts, not Mermaid's experimental `C4Context`. | [architecture](architecture/index.md) | – |
| A9 | CodeQL runs on pull requests, `main` and weekly since the repository became public (2026-10-11); it needed GitHub Advanced Security while private. Results appear after the first run on `main`. | `.github/workflows/codeql.yml` | first CodeQL run on `main` |
| A10 | Verification covers only structural claims (5 kinds); behaviour stays prose. | [claim schema](specs/claim-schema.md) | M2 end |
| A11 | The libmodbus `tests/` directory is its own `tests` module (by the default rule; kept in B-14). | [libmodbus plan](targets/libmodbus.md) | M3 |
| A12 | Labels of the claim set may be drafted by an agent from the source, each citing a file and line, and checked by the reviewer agent on a sample; the owner does not label. | [B-29](plan/backlog.md#b29-labelled-claim-set-v1) | if the sample check finds wrong labels |
| A13 | The real agent run of M2 uses Claude Code with a facts-only bundle, in a separate session without the repository or libmodbus source. | [B-37](plan/backlog.md#b37-real-agent-run-on-libmodbus-v1) | before B-37 |

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
| Delayed template parsing for MSVC code before C++20 | lcart inserts `-fno-delayed-template-parsing` after the program name, so calls in templates a unit does not instantiate are recorded; a command naming `/Zc:twoPhase-`, `/permissive` or `-fdelayed-template-parsing` keeps delayed parsing (measured on `msvc_templates`: 3 more calls, 1 of 3 units fails with its facts kept; 2026-10-18) | [ADR-0027](adr/0027-two-phase-template-parsing-for-msvc-code.md#adr0027-two-phase-by-default-v1) |
| Traceability format | IDs + versioned anchors, plus a generated traceability matrix that also gates CI; no restructuring after a standard | [ADR-0010](adr/0010-traceability-matrix.md) |
| Line endings | LF everywhere, whatever `core.autocrlf` says: `.gitattributes` has `* text=auto eol=lf` (2026-10-11); a Windows checkout used to get CRLF and 11 tests failed (golden files, file hashes) | `.gitattributes`, [how-to](how-to/msvc-under-wsl2.md) |
| Path glob syntax for module rules and exclusions | `*` within a directory, `**` across directories, anchored at the root; exclusions select translation units | [ADR-0014](adr/0014-path-globs-in-the-configuration.md) |
| Undefined behaviour in toml++ 3.4.0 on malformed input | `lcart.toml` is ASCII and toml++'s assertions are off until a fixed release is packaged | [ADR-0015](adr/0015-ascii-configuration-while-tomlplusplus-3-4.md) |
| Paths of files outside the analysis root in the facts export | the spelling of the first directive that includes the file, `#2`... if taken, no hash; translation unit and diagnostic paths stored like file paths, diagnostics linked to their file | [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1) (Accepted) |
| Paths of files outside the analysis root in handoff bundles | as in the facts export, but a directive spelled with an absolute path counts as `<outside-root>/` and the file name; no absolute path in a bundle (2026-10-18, B-30) | [ADR-0028](adr/0028-handoff-bundle-paths-run-id-and-contract-texts.md#adr0028-paths-outside-root-v1), [handoff contract](specs/handoff-contract.md#claim-handoff-bundle-paths-v1) |
| Names, evidence and exit statuses of the verification report | files named as in a handoff bundle (no absolute path); evidence as the fact with file and line, never a row id; JSON keys in a fixed order, `format` first; `lcart verify` exits 6 when a claim is contradicted, 2 for an invalid claims file (2026-10-18, B-28) | [ADR-0030](adr/0030-verification-report-and-lcart-verify.md), [report format](specs/claim-schema.md#claim-verification-report-format-v1) |
| Function with external linkage defined in several translation units | each definition is a function with its own calls; the analysis warns | [ADR-0013](adr/0013-functions-with-external-linkage-defined-twice.md) |
| Clang configuration files (`--config`, default `<triple>-clang.cfg`) in compile commands | removed from every command, and `--no-default-config` passed, so they never influence the analysis (2026-10-12) | [ADR-0023](adr/0023-clang-configuration-files-ignored.md), [security T5](security.md#security-malicious-build-input-v1) |
| LLVM targets in `lcart` | registered once before the first parse, so MS-style inline assembly (`winnt.h` for x86) parses as in clang-cl (2026-10-12) | [ADR-0025](adr/0025-llvm-targets-registered.md), [how-to](how-to/msvc-under-wsl2.md) |
| Default database on a WSL2 drive mount (drvfs, `v9fs`) failed with "database is locked" | the run lock moved to a separate file (`.<name>.partial.lock`), so the default database works on `/mnt/c` (2026-10-12, #30) | [ADR-0024](adr/0024-run-lock-on-a-separate-file.md), [how-to](how-to/msvc-under-wsl2.md) |
| Performance for large code bases | hours or days are acceptable; the tool must use all cores, show progress and resume interrupted runs; incremental re-analysis later | [NFR-005](specs/requirements.md#nfr005-performance-budget-v3) |

## Questions for the maintainer

None at the moment. The question whether to try NFR-013 criterion 3 on a
Windows machine before M1 is done was answered by doing it on 2026-10-11
([ADR-0022](adr/0022-msbuild-logger-for-compilation-databases.md)).

## Open technical questions

| Question | Context | Decide by |
|---|---|---|
| Detect source files edited between an interrupted analysis and `--resume` (check `translation_unit.inputs_sha256`)? Today a resumed run mixes old and new code. | [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1) | M2, [B-38](plan/backlog.md#b38-resume-detects-edited-sources-v1) |
| Run each translation unit in a child process so a Clang crash cannot end the run? Costs process start-up per TU. | [security T5](security.md), [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-interrupt-v1) | after M2: not needed for the handoff; revisit when a real code base has a TU that crashes Clang (one process for now; such a TU blocks `--resume`) |
| Keep taking the target from a program name prefix? Since LLVM targets are registered ([ADR-0025](adr/0025-llvm-targets-registered.md#adr0025-register-all-targets-once-v1)), a command run as `x86_64-w64-mingw32-clang` without `--target` is parsed for that target (tried 2026-10-17), as `clang` under that name does; before, the prefix was ignored, as ADR-0021 still says. No test pins either behaviour. | [ADR-0025](adr/0025-llvm-targets-registered.md) consequences, [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-clang-cl-commands-v1) | M2 |
| Reject duplicate property names in one JSON object (`{"polarity": "affirmed", "polarity": "negated"}`)? `parse_claims` uses nlohmann_json, which keeps the last; RFC 8259 only says names SHOULD be unique and the claim schema says nothing, so another reader of the same agent output may keep the first. | [claim schema](specs/claim-schema.md#claim-json-schema-v2), [B-25](plan/backlog.md#b25-claim-parser-v1) | M2, before [B-33](plan/backlog.md#b33-ingest-agent-output-v1) ingests agent output |
| Cap the problems `parse_claims` reports? Every problem is reported (FR-009, B-25), so a 4 MiB input of unknown properties gives a message of about 19 MB (measured in review, 391,400 lines); memory stays a fixed multiple of the size limit, but B-33 will print or store these lines. A cap (the first N and "and M more") changes FR-009 criterion 1. | [FR-009](specs/requirements.md#fr009-structured-claims-v2), [B-25](plan/backlog.md#b25-claim-parser-v1) | M2, before [B-33](plan/backlog.md#b33-ingest-agent-output-v1) |
| "Complete coverage" means every translation unit *of the analysis* was parsed, so code the analysis never saw is reported as nonexistent: under complete coverage a true claim naming it is `contradicted` / `entity_not_found`. This covers a function declared only outside the analysis root (`printf` in `<stdio.h>`, not recorded: [function facts](architecture/data-model.md#arch-data-model-function-facts-v2), "`main` calls `printf`"), and files, functions and modules of translation units removed by `[project] exclude` ([ADR-0014](adr/0014-path-globs-in-the-configuration.md#adr0014-exclude-selects-translation-units-v1)) or missing from `compile_commands.json`. Record callees outside the root by name, answer `unverified` for names a recorded file declares outside the root, count excluded units as not covered, or compare the root's source files with the analysed ones? | [entity resolution](specs/claim-schema.md#claim-entity-resolution-v3), [B-27](plan/backlog.md#b27-claim-verifier-v1) | M2, with the labelled claim set of [B-29](plan/backlog.md#b29-labelled-claim-set-v1), which measures how often it happens |
| Narrow the any-error rule of the verifier: today one error or fatal diagnostic anywhere in the analysis means no file counts as parsed, so every absent `calls`, `includes` and `defines` relation is `unverified`. Per-unit reachability would need the include directives of headers outside the root and the facts of units whose main file is outside the root (both unrecorded), or another bound on what error recovery touched. | [relation rules](specs/claim-schema.md#claim-relation-rules-v2), [B-27](plan/backlog.md#b27-claim-verifier-v1) | M2, with the measurements of [B-29](plan/backlog.md#b29-labelled-claim-set-v1) |
| Name a project in handoff bundles by `[project] name` of `lcart.toml` instead of the directory name of the analysis root? `lcart analyze` would have to record it in the fact database (`META`); `lcart handoff` reads no `lcart.toml`. | [ADR-0028](adr/0028-handoff-bundle-paths-run-id-and-contract-texts.md#adr0028-project-name-v1) | M2, before [B-37](plan/backlog.md#b37-real-agent-run-on-libmodbus-v1) |
| Resolve the bundle's names of files outside the root in claims? The verifier resolves a file entity by the stored path, exact or by `/`-suffix ([entity resolution](specs/claim-schema.md#claim-entity-resolution-v3)), so `stdio.h` finds `/usr/include/stdio.h`, but `<outside-root>/x.h` and `x.h#2` ([bundle paths](specs/handoff-contract.md#claim-handoff-bundle-paths-v1)) find nothing, and an `includes` claim naming them is `contradicted` under complete coverage. Map bundle names back to files when verifying agent output? The verification report names files as bundles do ([ADR-0030](adr/0030-verification-report-and-lcart-verify.md#adr0030-evidence-and-file-names-v1)), so a mapping would serve both. | [ADR-0028](adr/0028-handoff-bundle-paths-run-id-and-contract-texts.md#adr0028-paths-outside-root-v1) | M2, before [B-33](plan/backlog.md#b33-ingest-agent-output-v1) |
| Why does clang-tidy 20.1.2 ignore `HeaderFilterRegex` from `.clang-tidy` in the main clone (findings in `src/` headers dropped as non-user code, while `--dump-config` shows the filter) but apply it in a git worktree? `scripts/run-clang-tidy.sh` passes the filter explicitly, so nothing depends on the answer. | [B-24](plan/backlog.md#b24-clang-tidy-checks-headers-v1) | when clang-tidy is upgraded |

## Not verified yet ("TODO: verify")

| Item | Where | Verified by |
|---|---|---|
| Seam for feeding frames to libmodbus through the public API only | [testing strategy](testing/strategy.md#testing-characterization-v1) | M3 |
| `modbus_mapping_new` signature and per-iteration cost for fuzzing | [libmodbus plan](targets/libmodbus.md) | M4 |
| `modbus_close` / `modbus_free` semantics for the RAII wrapper | [libmodbus plan](targets/libmodbus.md) | M5 |
| Exact terms of `tests/LICENSE` in libmodbus | [ADR-0003](adr/0003-licensing.md) | before reusing any upstream test code |
| The MSBuild logger on other setups than Build Tools 2022 with x64 and Win32 `v143` projects: older Visual Studio, other platform toolsets, C++/CLI, custom build steps, a build with compile errors, `.rc`/`.idl` files, the `arm` and `arm64` branches. Its CI check uses the database it wrote once; `tools/msbuild-compdb/verify.ps1` needs Windows. | [ADR-0022](adr/0022-msbuild-logger-for-compilation-databases.md), [how-to](how-to/msvc-under-wsl2.md) | a Windows machine with the setup |
| Windows SDK and MSVC headers through `/winsysroot`, `/vctoolsdir` + `/winsdkdir`, `-Xmicrosoft-windows-sys-root`, `-isystem`; headers copied to the Linux file system instead of read through `/mnt/c`; case of `#include` names against file names on drvfs; whether a WSL2 `PATH` with Windows directories lets Clang find an MSVC installation. (`/imsvc` with `/mnt/c` is tried.) | [ADR-0021](adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-windows-headers-v1), [how-to](how-to/msvc-under-wsl2.md) | a machine with the Windows SDK and MSVC |
| Two-phase template parsing on real MSVC code: which Windows SDK versions need delayed parsing (Clang's driver: "many old Windows SDK versions"), and whether MSVC without `/permissive-` accepts `tests/fixtures/msvc_templates/lenient.cc` | [ADR-0027](adr/0027-two-phase-template-parsing-for-msvc-code.md), [how-to](how-to/msvc-under-wsl2.md) | the first analysis of a real MSVC code base, on Windows |
| MkDocs/Material settings that keep raw HTML in generated docs from executing | [security](security.md) | M2, [B-35](plan/backlog.md#b35-no-raw-html-in-generated-docs-v1) |
| Usable local model sizes for claim generation | [NFR-003](specs/requirements.md#nfr003-fully-local-mode-v1) | M6 |
| Claude Code permission rule syntax for a bundle's `.claude/settings.json` | [handoff contract](specs/handoff-contract.md#claim-agent-permissions-v1) | M2, [B-32](plan/backlog.md#b32-agent-permissions-in-bundle-v1) |
| Validating agent diagrams with Mermaid in `lcart ingest`. Parsing without a browser works: B-22 runs Mermaid's own `mermaid.parse` under Node.js with jsdom as the only DOM ([B-22 result](plan/backlog.md#b22-mermaid-check-in-ci-v1)). Open: whether `lcart` may depend on Node.js at run time for it (an ADR), and that a parse is weaker than a render (Mermaid 11.17.2 parsed a quoted label with an unescaped `"` inside it). | [FR-008](specs/requirements.md#fr008-generate-documentation-v2) | M2, [B-34](plan/backlog.md#b34-mermaid-check-in-ingest-v1) |
