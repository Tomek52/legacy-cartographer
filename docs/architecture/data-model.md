# Fact database data model

One SQLite file per analysed project. Schema version 1
(`facts::schema_version` in `src/facts/fact_model.hpp`); any change bumps it.
Paths of files under the analysis root are relative to it, with `/`
separators; files outside it have absolute paths
([file facts](#arch-data-model-file-facts-v1)). How functions are
identified: [function facts](#arch-data-model-function-facts-v1).

```mermaid
erDiagram
    META {
        text key PK
        text value
    }
    TRANSLATION_UNIT {
        int id PK
        text path UK "as in compile_commands.json"
        int file_id FK "not set yet (B-16)"
        text status "pending | parsed | failed"
        text command_sha256 "compile command"
        text inputs_sha256 "main file + headers read"
    }
    DIAGNOSTIC {
        int id PK
        int tu_id FK
        text file_path "as reported by Clang; empty if none"
        int file_id FK "null until resolved (B-12)"
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
        text sha256 "lowercase hex, of the content"
        int is_header "not a source extension"
        int is_external "outside the analysis root"
        int module_id FK "null if and only if external"
    }
    INCLUDE_EDGE {
        int id PK
        int includer_id FK
        int included_id FK "null if unresolved"
        text spelled "name between the quotes or brackets"
        int line "of the #, in the includer"
        int is_system "angle brackets"
    }
    FUNCTION {
        int id PK
        text usr UK "path-prefixed if internal (ADR-0012)"
        text name
        text qualified_name
        text signature "type as Clang prints it"
        int file_id FK "definition, else first declaration"
        int line_begin
        int line_end
        int is_definition
        text linkage "internal | external"
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

## <a id="arch-data-model-file-facts-v1"></a>File and include facts

How [FR-002](../specs/requirements.md#fr002-record-files-and-includes-v1)
maps onto `FILE` and `INCLUDE_EDGE` (B-08):

- Paths are canonical (symlinks and `..` resolved) before they are compared
  with the canonical analysis root, so one file has one row however it was
  reached.
- A file is a header unless its extension is a source extension (`.c`,
  `.C`, `.cc`, `.cpp`, `.cxx`, `.c++`, `.m`, `.mm`), so a `.c` file that is
  `#include`d is still a source and `.inc` files are headers.
- A file under the root gets a module: the first matching rule, or the
  default of [FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1)
  (top-level directory, or `(root)`). Rules arrive with B-11.
- A file outside the root (system and third-party headers) is recorded only
  when a file under the root includes it directly, with its absolute path
  (how bundles and golden exports show such paths is an
  [open question](../open-questions.md#open-technical-questions)).
  Directives inside it are not recorded, so the database does not grow with
  the system's headers.
- A file under the root forced in with `-include` is recorded although no
  directive names it (no edge: the includer is the command line).
- An `#include` directive is recorded once per (includer, line, spelling,
  target), although a header is reached from many translation units.
  `is_system` is the spelling (`<...>`), not where the file was found.
- An unresolved include has no target and keeps its spelling; Clang reports
  it as a fatal error, so its translation unit also counts as failed.

## <a id="arch-data-model-function-facts-v1"></a>Function facts

How [FR-003](../specs/requirements.md#fr003-record-functions-v1) maps onto
`FUNCTION` (B-09):

- Only functions whose declaration is in a file under the root are
  recorded; `printf` declared in `<stdio.h>` is not. Compiler-generated
  declarations, lambda call operators and deduction guides are skipped, and
  so are template instantiations (the template itself is recorded).
- `usr` is Clang's USR; for a function with internal linkage it is
  prefixed with the root-relative path of the file of its definition (else
  of its first declaration) and `#`, e.g. `left/util.c#c:util.c@F@helper`
  ([ADR-0012](../adr/0012-function-identity-for-internal-linkage.md#adr0012-internal-linkage-usr-v1)),
  because Clang's USR then names only the base name of a file. Two `static`
  functions in `left/util.c` and `right/util.c`, or two definitions of a
  `static` declared in one shared header, are two rows; a `static inline`
  function defined in a header is one row.
- `linkage` is `external` if the function is visible outside its
  translation unit, else `internal` (`static`, anonymous namespace).
- `signature` is the function's type as Clang prints it, without parameter
  names: `size_t (const char *)`, `int (int, ...)`.
- File and lines are those of the definition once one is seen, else of the
  first declaration seen (translation units are analysed in path order). The
  line range runs from the first token (`static`, the return type) to the
  closing brace or semicolon. A function written by a macro is located at
  the macro's use.
- A function with external linkage defined in two translation units (two
  programs' `main` in one compilation database) is one row at the first
  definition in path order; later definitions are not recorded
  ([open question](../open-questions.md#open-technical-questions)).
