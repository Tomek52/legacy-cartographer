# Components

Status: `facts` (SQLite store and JSON Lines export, B-06, B-12),
`analysis` (Clang driver and file, function, call and module facts,
B-07–B-11), `config` (B-11), `cli` (`analyze`, `query`, B-12; `map`, B-13)
and the module map in `report` (B-13) are implemented; every other module
has its interface header and no implementation.

| Module | Responsibility | Main interface | Depends on | Delivered in |
|---|---|---|---|---|
| `common` | error type, `Result<T>` = `std::expected<T, Error>`, version | `common/result.hpp` | — | M0 |
| `config` | parse `lcart.toml` (toml++); path globs | `config/project_config.hpp`, `config/path_glob.hpp` | common, toml++ | M1 (B-11) |
| `facts` | fact model; write and query interfaces; SQLite implementation; JSON Lines export ([ADR-0016](../adr/0016-facts-export-and-command-line.md#adr0016-export-format-v1)) | `facts/fact_store.hpp` (`FactWriter`, `FactQuery`), `facts/fact_export.hpp` | common, SQLite | M1 (B-06, B-12) |
| `analysis` | parse TUs with Clang, emit facts; map files to modules | `analysis/analyzer.hpp` (`Analyzer`), `analysis/module_rules.hpp` | facts, config, Clang 20 | M1 (B-07…B-11) |
| `claims` | claim model; parse and validate claims JSON | `claims/claim.hpp`, `claims/claim_parser.hpp` | common, nlohmann_json | M2 |
| `verify` | verification rules | `verify/claim_verifier.hpp`, `verify/verdict.hpp` | claims, facts | M2 |
| `report` | module map: Markdown and Mermaid from facts ([ADR-0017](../adr/0017-module-map-format-and-command-line.md)); verification report and document annotation (M2) | `report/report_writer.hpp` | verify (and through it facts) | M1 (B-13), M2 |
| `handoff` | write bundles for an external agent, read its output | `handoff/handoff.hpp` | claims, facts | M2 |
| `llm` | built-in providers, egress gate, egress log | `llm/provider.hpp`, `llm/egress_policy.hpp` | common, HTTP client (TBD) | M6 |
| `testgen` | characterization test skeletons | not created yet | facts | M3 |
| `cli` | command-line parser, commands and the git exposure warning ([ADR-0016](../adr/0016-facts-export-and-command-line.md), [ADR-0017](../adr/0017-module-map-format-and-command-line.md)), as library `lcart::cli`; `main.cpp` builds the `lcart` executable from it; reads `lcart.toml` through `config` | `cli/command_line.hpp`, `cli/commands.hpp`, `cli/git_exposure.hpp` | analysis, config, facts, report (the executable: all) | M0 (help/version), M1 (B-12, B-13) |

## <a id="arch-threading-v1"></a>Threading

M1 analyses translation units in parallel (one Clang instance per worker).
Workers do not write to SQLite; they return per-TU fact batches to a single
writer thread, which inserts them in a fixed order (by TU path). This keeps
the fact database deterministic
([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)) and avoids
SQLite write contention. The design is confirmed or replaced by an ADR in
B-07.

## <a id="arch-error-handling-v2"></a>Error handling

Functions that can fail return `common::Result<T>`, an alias of
`std::expected<T, common::Error>` ([ADR-0007](../adr/0007-cpp23-and-toolchain-baseline.md)).
Exceptions are not used across module boundaries. A failed translation unit
is data (a diagnostic), not an error.
