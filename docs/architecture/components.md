# Components

Status as of M0: every module has its interface header and no implementation.

| Module | Responsibility | Main interface | Depends on | Delivered in |
|---|---|---|---|---|
| `common` | error type, `Result<T>`, version | `common/result.hpp` | — | M0 |
| `facts` | fact model; write and query interfaces; SQLite implementation | `facts/fact_store.hpp` (`FactWriter`, `FactQuery`) | common, SQLite | M1 (B-06) |
| `analysis` | parse TUs with Clang, emit facts; map files to modules | `analysis/analyzer.hpp` (`Analyzer`), `analysis/module_rules.hpp` | facts, Clang 18 | M1 (B-07…B-11) |
| `claims` | claim model; parse and validate LLM JSON | `claims/claim.hpp`, `claims/claim_parser.hpp` | common, nlohmann_json | M2 |
| `verify` | verification rules | `verify/claim_verifier.hpp`, `verify/verdict.hpp` | claims, facts | M2 |
| `report` | module map (M1), verification report (M2) | `report/report_writer.hpp` | verify | M1 (B-13), M2 |
| `llm` | provider interface, egress policy, providers, egress log | `llm/provider.hpp`, `llm/egress_policy.hpp` | common, HTTP client (TBD) | M2 |
| `testgen` | characterization test skeletons | not created yet | facts | M3 |
| `cli` | the `lcart` executable | `cli/main.cpp` | all | M0 (help/version) |

## <a id="arch-threading-v1"></a>Threading

M1 analyses translation units in parallel (one Clang instance per worker).
Workers do not write to SQLite; they return per-TU fact batches to a single
writer thread, which inserts them in a fixed order (by TU path). This keeps
the fact database deterministic
([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)) and avoids
SQLite write contention. The design is confirmed or replaced by an ADR in
B-07.

## <a id="arch-error-handling-v1"></a>Error handling

Functions that can fail return `common::Result<T>` (a stand-in for C++23
`std::expected`). Exceptions are not used across module boundaries. A failed
translation unit is data (a diagnostic), not an error.
