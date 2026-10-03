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
        int file_id FK
        text status "parsed | failed"
    }
    DIAGNOSTIC {
        int id PK
        int tu_id FK
        int file_id FK
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
        int module_id FK "null if external"
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
        text via "member name for indirect"
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

## <a id="arch-data-model-rules-v1"></a>Rules

- `META` holds `schema_version`, `tool_version`, `analysis_root` and the
  SHA-256 of `compile_commands.json`. No timestamps in fact tables
  ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
- Row ids are assigned in a deterministic insertion order (TUs sorted by
  path, declarations in source order), so ids are stable across runs on the
  same input.
- Functions are identified by USR; the same function seen from several TUs
  is one row.
- `MODULE_DEPENDENCY` is derived from `INCLUDE_EDGE` and `CALL_EDGE` after
  analysis and keeps the justifying edge, so `depends_on` claims have
  evidence.
- Claims and verdicts live in the same file so a report can be regenerated
  without the LLM.
- All SQL uses bound parameters; analysed names are never concatenated into
  SQL ([security](../security.md)).
- The physical DDL is written in B-06; this page is its specification.
