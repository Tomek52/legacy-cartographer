# Architecture

The C4 model, levels 1–3, plus the data model of the fact database.

| Page | Shows |
|---|---|
| [Context (C4 level 1)](c4-context.md) | `lcart`, its users and the external systems it touches |
| [Containers (C4 level 2)](c4-container.md) | the executable, the fact database, output files, LLM endpoints |
| [Components (C4 level 3)](c4-component.md) | the modules inside `lcart` and their dependencies |
| [Components in detail](components.md) | responsibility, interface and status of each module |
| [Data model](data-model.md) | tables of the fact database |

Diagrams use Mermaid flowcharts in C4 style rather than Mermaid's `C4Context`
syntax, which is marked experimental and renders inconsistently.

## <a id="arch-principles-v2"></a>Principles

1. **Facts first.** The fact database is built deterministically from
   static analysis and is the single source of truth. The LLM reads facts;
   it never writes them.
2. **No network in v1.** LLM work is done by an external agent the user
   runs on a handoff bundle; `lcart` only writes the bundle and reads the
   result ([ADR-0006](../adr/0006-external-agent-handoff-for-llm-work.md)).
3. **The deterministic core never depends on the LLM layer.** The `llm`
   module (built-in providers, M6) may not be linked by the core; enforced at
   configure time by `cmake/ArchitectureRules.cmake`
   ([NFR-001](../specs/requirements.md#nfr001-works-without-llm-v1)).
4. **Interfaces at module boundaries.** Each module exposes an abstract
   interface or free functions (`FactWriter`, `FactQuery`, `Analyzer`,
   `write_bundle`), so tests use fakes, and the Clang dependency stays
   inside `analysis`.
5. **Outputs are reproducible.** Every query returns a defined order; no
   hash-map iteration order reaches output
   ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
