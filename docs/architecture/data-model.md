# Fact database data model

One SQLite file per analysed project. Schema version 1
(`facts::schema_version` in `src/facts/fact_model.hpp`); any change bumps it.
Paths are relative to the analysis root with `/` separators.

```mermaid
erDiagram
    META {
        text key PK
        text value
    }
    TRANSLATION_UNIT {
        int id PK
        text path UK "as in compile_commands.json"
        int file_id FK "null until file facts exist"
        text status "pending | parsed | failed"
        text command_sha256 "compile command"
        text inputs_sha256 "main file + headers read"
    }
    DIAGNOSTIC {
        int id PK
        int tu_id FK
        text file_path "as reported by Clang; empty if none"
        int file_id FK "null until resolved (B-08)"
        int line
        text severity
        text message
    }
    MODULE {
        int id PK
        text name UK
    }
    FILE {
        int id PK
        text path UK
        text sha256
        int is_header
        int is_external
        int module_id FK "null if and only if external"
    }
    INCLUDE_EDGE {
        int id PK
        int includer_id FK
        int included_id FK "null if unresolved"
        text spelled
        int line
        int is_system
    }
    FUNCTION {
        int id PK
        text usr UK
        text name
        text qualified_name
        text signature
        int file_id FK
        int line_begin
        int line_end
        int is_definition
        text linkage
    }
    CALL_EDGE {
        int id PK
        int caller_id FK
        int callee_id FK "null if indirect"
        text kind "direct | indirect | may_call"
        text via "member name for indirect, else empty"
        int file_id FK
        int line
        int col
    }
    MODULE_DEPENDENCY {
        int from_module_id FK
        int to_module_id FK
        text via "include | call"
        int edge_id
    }
    CLAIM {
        text id PK
        text kind
        text json
        text document
        text section
    }
    VERDICT {
        text claim_id PK
        text status "confirmed | unverified | contradicted"
        text reason
        text evidence "JSON array of fact ids"
    }

    TRANSLATION_UNIT ||--o{ DIAGNOSTIC : has
    FILE ||--o| TRANSLATION_UNIT : "is main file of"
    MODULE ||--o{ FILE : contains
    FILE ||--o{ INCLUDE_EDGE : "includes"
    FILE ||--o{ FUNCTION : "declares/defines"
    FUNCTION ||--o{ CALL_EDGE : "calls from"
    MODULE ||--o{ MODULE_DEPENDENCY : "depends"
    CLAIM ||--o| VERDICT : "verified as"
```

## <a id="arch-data-model-rules-v2"></a>Rules

- `META` holds `schema_version`, `tool_version`, `analysis_root`, the
  SHA-256 of `compile_commands.json` and the coverage counts
  (`translation_units_total`, `translation_units_parsed`). No timestamps in
  fact tables
  ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
- Opening a database whose `schema_version` differs from the tool's, or a
  SQLite file without `META`, is an error; there is no migration in v1
  (re-run the analysis).
- Row ids are assigned in a deterministic insertion order (TUs sorted by
  path, declarations in source order), so ids are stable across runs on the
  same input.
- Functions are identified by USR; the same function seen from several TUs
  is one row, located at its definition once one is seen. Modules are unique
  by name and files by path; adding a known one returns its id.
- `MODULE_DEPENDENCY` is derived from `INCLUDE_EDGE` and direct
  `CALL_EDGE`s after analysis and keeps the justifying edge, so `depends_on`
  claims have evidence. Until B-11 fills the table, `module_dependencies()`
  derives the same set on each query.
- `kind = 'indirect'` if and only if `callee_id` is null; `may_call` edges
  ([FR-015](../specs/requirements.md#fr015-resolve-function-pointer-tables-v1)) are never returned with direct calls.
- Claims and verdicts live in the same file so a report can be regenerated
  without the LLM.
- Each TU's facts are committed in one transaction together with its
  `status`, so an interrupted analysis resumes from the TUs still `pending`
  ([NFR-005](../specs/requirements.md#nfr005-performance-budget-v2)). The
  per-TU hashes allow incremental re-analysis later.
- All SQL uses bound parameters; analysed names are never concatenated into
  SQL ([security](../security.md)).
- The physical DDL is `schema_sql` in `src/facts/src/sqlite_fact_store.cpp`
  (`STRICT` tables, foreign keys on, `CHECK` constraints for the rules above);
  this page is its specification.
