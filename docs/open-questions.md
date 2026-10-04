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
| A11 | The libmodbus `tests/` directory is its own `tests` module. | [libmodbus plan](targets/libmodbus.md) | B-14 |

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
| Performance for large code bases | hours or days are acceptable; the tool must use all cores, show progress and resume interrupted runs; incremental re-analysis later | [NFR-005](specs/requirements.md#nfr005-performance-budget-v2) |

## Questions for the maintainer

None open. New questions are added here as they come up.

## Open technical questions

| Question | Context | Decide by |
|---|---|---|
| Run each translation unit in a child process so a Clang crash cannot end the run? Costs process start-up per TU. | [security T5](security.md) | B-16, after measuring on libmodbus |
| How to show paths of files outside the analysis root (stored absolute, e.g. Clang's resource directory or a home directory) in handoff bundles? The export's rule (spelling of the first directive, [ADR-0016](adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)) is the candidate. | [file facts](architecture/data-model.md#arch-data-model-file-facts-v1), [security](security.md) | M2 (bundles) |
| `TRANSLATION_UNIT.file_id` is not set yet: the row is written with diagnostics, before per-TU status exists. | [data model](architecture/data-model.md) | B-16 |

## Not verified yet ("TODO: verify")

| Item | Where | Verified by |
|---|---|---|
| Seam for feeding frames to libmodbus through the public API only | [testing strategy](testing/strategy.md#testing-characterization-v1) | M3 |
| `modbus_mapping_new` signature and per-iteration cost for fuzzing | [libmodbus plan](targets/libmodbus.md) | M4 |
| `modbus_close` / `modbus_free` semantics for the RAII wrapper | [libmodbus plan](targets/libmodbus.md) | M5 |
| Exact terms of `tests/LICENSE` in libmodbus | [ADR-0003](adr/0003-licensing.md) | before reusing any upstream test code |
| Exact Clang 20 flags for MSVC-targeted code; LibTooling handling of `clang-cl` arguments; a tested way to get a compilation database from MSBuild | [ADR-0009](adr/0009-linux-and-wsl2-only.md) | B-17 |
| How to run a GitHub Actions job without network access | [NFR-001](specs/requirements.md#nfr001-works-without-llm-v2) | B-14 |
| Mermaid, as loaded by MkDocs Material, decodes `#<decimal>;` entity codes in quoted flowchart labels (the module map's escaping) | [ADR-0017](adr/0017-module-map-format-and-command-line.md#adr0017-escaping-v1) | B-14 (rendered libmodbus map) |
| MkDocs/Material settings that keep raw HTML in generated docs from executing | [security](security.md) | M2 |
| Usable local model sizes for claim generation | [NFR-003](specs/requirements.md#nfr003-fully-local-mode-v1) | M6 |
| Claude Code permission rule syntax for a bundle's `.claude/settings.json` | [handoff contract](specs/handoff-contract.md#claim-agent-permissions-v1) | M2 |
| Parsing Mermaid without a browser to validate agent diagrams | [FR-008](specs/requirements.md#fr008-generate-documentation-v2) | M2 |
