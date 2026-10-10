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
| Traceability format | IDs + versioned anchors, plus a generated traceability matrix that also gates CI; no restructuring after a standard | [ADR-0010](adr/0010-traceability-matrix.md) |
| Path glob syntax for module rules and exclusions | `*` within a directory, `**` across directories, anchored at the root; exclusions select translation units | [ADR-0014](adr/0014-path-globs-in-the-configuration.md) |
| Undefined behaviour in toml++ 3.4.0 on malformed input | `lcart.toml` is ASCII and toml++'s assertions are off until a fixed release is packaged | [ADR-0015](adr/0015-ascii-configuration-while-tomlplusplus-3-4.md) |
| Paths of files outside the analysis root in the facts export | the spelling of the first directive that includes the file, `#2`... if taken, no hash; translation unit and diagnostic paths stored like file paths, diagnostics linked to their file | [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1) (Accepted) |
| Function with external linkage defined in several translation units | each definition is a function with its own calls; the analysis warns | [ADR-0013](adr/0013-functions-with-external-linkage-defined-twice.md) |
| Performance for large code bases | hours or days are acceptable; the tool must use all cores, show progress and resume interrupted runs; incremental re-analysis later | [NFR-005](specs/requirements.md#nfr005-performance-budget-v3) |

## Questions for the maintainer

| Question | Context | Decide by |
|---|---|---|
| NFR-013 criterion 3 (a tested way to get a compilation database from an MSBuild solution) needs a Windows machine, which neither the development container nor CI has. Try it on a Windows machine before M1 is marked done, or move the criterion to a later milestone? | [NFR-013](specs/requirements.md#nfr013-linux-and-wsl2-v1), [B-17](plan/backlog.md#b17-msvc-code-spike-v2) | M1 end |

## Open technical questions

| Question | Context | Decide by |
|---|---|---|
| Detect source files edited between an interrupted analysis and `--resume` (check `translation_unit.inputs_sha256`)? Today a resumed run mixes old and new code. | [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1) | M2 |
| Run each translation unit in a child process so a Clang crash cannot end the run? Costs process start-up per TU. | [security T5](security.md), [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-interrupt-v1) | M2 (one process for now; a TU that always crashes Clang blocks `--resume`) |
| How to show paths of files outside the analysis root (stored absolute, e.g. Clang's resource directory or a home directory) in handoff bundles? The export's rule (spelling of the first directive, [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)) is the candidate. | [file facts](architecture/data-model.md#arch-data-model-file-facts-v1), [security](security.md) | M2 (bundles) |
| The worker pool (threads, condition variables, dispatch window) has no ThreadSanitizer run: there is no TSan preset. Add one, at least for `parallel_analysis_test.cpp`? | [ADR-0019](adr/0019-parallel-resumable-analysis.md#adr0019-one-writer-in-tu-order-v1), [NFR-004](specs/requirements.md#nfr004-deterministic-output-v1) | M1 end |

## Not verified yet ("TODO: verify")

| Item | Where | Verified by |
|---|---|---|
| Seam for feeding frames to libmodbus through the public API only | [testing strategy](testing/strategy.md#testing-characterization-v1) | M3 |
| `modbus_mapping_new` signature and per-iteration cost for fuzzing | [libmodbus plan](targets/libmodbus.md) | M4 |
| `modbus_close` / `modbus_free` semantics for the RAII wrapper | [libmodbus plan](targets/libmodbus.md) | M5 |
| Exact terms of `tests/LICENSE` in libmodbus | [ADR-0003](adr/0003-licensing.md) | before reusing any upstream test code |
| Exact Clang 20 flags for MSVC-targeted code; LibTooling handling of `clang-cl` arguments | [ADR-0009](adr/0009-linux-and-wsl2-only.md) | B-17 |
| A tested way to get a compilation database from an MSBuild solution | [ADR-0009](adr/0009-linux-and-wsl2-only.md), [NFR-013](specs/requirements.md#nfr013-linux-and-wsl2-v1) | a Windows machine (question for the maintainer) |
| MkDocs/Material settings that keep raw HTML in generated docs from executing | [security](security.md) | M2 |
| Usable local model sizes for claim generation | [NFR-003](specs/requirements.md#nfr003-fully-local-mode-v1) | M6 |
| Claude Code permission rule syntax for a bundle's `.claude/settings.json` | [handoff contract](specs/handoff-contract.md#claim-agent-permissions-v1) | M2 |
| A CI check that the committed libmodbus map's Mermaid block parses and renders (FR-007 criterion 1; checked by hand once in B-14) | [FR-007](specs/requirements.md#fr007-module-map-without-llm-v1) | M1 end |
| Parsing Mermaid without a browser to validate agent diagrams | [FR-008](specs/requirements.md#fr008-generate-documentation-v2) | M2 |
