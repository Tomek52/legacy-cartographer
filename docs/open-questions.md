# Open questions and assumptions

Assumptions made while writing M0, questions that need an answer from the
maintainer, and facts that could not be verified yet. Close an item by
moving the answer into the relevant spec or ADR and deleting it here.

## Assumptions

| # | Assumption | Where it matters | Revisit when |
|---|---|---|---|
| A1 | Repository `legacy-cartographer`, executable `lcart`, namespace `lcart::`. | everywhere | before going public |
| A2 | Apache-2.0 for own code. | [ADR-0003](adr/0003-licensing.md) | – |
| A4 | libmodbus pinned at v3.2.0 (latest tag on 2026-10-03). | [libmodbus plan](targets/libmodbus.md) | B-01 |
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

## Questions for the maintainer

1. **Q1. Toolchain of the private code bases.** The analysis needs a
   `compile_commands.json` and Clang must accept the code. If the private
   code builds only with MSVC on Windows: does `lcart` have to run on
   Windows, or is running it on Linux/WSL with Clang in MSVC-compatibility
   mode (`clang-cl`, Windows SDK headers copied over) acceptable? And can the
   build produce a compilation database (CMake can; MSBuild `.vcxproj` needs
   an extra tool)? This decides whether M1 needs a Windows CI job.
2. **Q7. Traceability format.** Is the ID + versioned-anchor scheme enough,
   or should requirements map to a formal standard's structure (for example a
   software lifecycle standard used in a regulated domain)?
3. **Q8. Performance target for large code bases** (NFR-005 part 2): what
   size and time budget is realistic for the intended private code base?

## Not verified yet ("TODO: verify")

| Item | Where | Verified by |
|---|---|---|
| Conan Center has `sqlite3/3.46.1`, `nlohmann_json/3.11.3`, `gtest/1.15.0` (Conan Center was not reachable from the authoring environment) | `conanfile.py` | first CI run (B-02) |
| `find_package(Clang)` from `/usr/lib/llvm-20/lib/cmake/clang` links with our presets | [ADR-0007](adr/0007-cpp23-and-toolchain-baseline.md) | B-07 |
| Complete list of Clang 20 flags that load code (`-fplugin`, `-load`, …) | [security](security.md) | B-07 |
| libmodbus builds out of tree with autotools | [libmodbus plan](targets/libmodbus.md) | B-05 |
| Initializer form of libmodbus backend tables (`modbus-rtu.c`, `modbus-tcp.c`) | [FR-015](specs/requirements.md#fr015-resolve-function-pointer-tables-v1) | B-10 |
| Seam for feeding frames to libmodbus through the public API only | [testing strategy](testing/strategy.md#testing-characterization-v1) | M3 |
| `modbus_mapping_new` signature and per-iteration cost for fuzzing | [libmodbus plan](targets/libmodbus.md) | M4 |
| `modbus_close` / `modbus_free` semantics for the RAII wrapper | [libmodbus plan](targets/libmodbus.md) | M5 |
| Exact terms of `tests/LICENSE` in libmodbus | [ADR-0003](adr/0003-licensing.md) | before reusing any upstream test code |
| How to run a GitHub Actions job without network access | [NFR-001](specs/requirements.md#nfr001-works-without-llm-v1) | B-14 |
| MkDocs/Material settings that keep raw HTML in generated docs from executing | [security](security.md) | M2 |
| Usable local model sizes for claim generation | [NFR-003](specs/requirements.md#nfr003-fully-local-mode-v1) | M6 |
| Conan Center has `tomlplusplus` 3.4.0 | [ADR-0008](adr/0008-configuration-file-format.md) | B-11 |
| Claude Code permission rule syntax for a bundle's `.claude/settings.json` | [handoff contract](specs/handoff-contract.md#claim-agent-permissions-v1) | M2 |
| Parsing Mermaid without a browser to validate agent diagrams | [FR-008](specs/requirements.md#fr008-generate-documentation-v2) | M2 |
