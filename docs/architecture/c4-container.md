# C4 level 2: containers

```mermaid
flowchart TB
    engineer["<b>Engineer</b><br/>[Person]"]

    subgraph machine["User's machine"]
        cli["<b>lcart</b><br/>[C++23 executable]<br/>analysis, map, handoff, ingest,<br/>verification, reports"]
        db[("<b>Fact database</b><br/>[SQLite file]<br/>files, includes, functions,<br/>calls, modules, claims, verdicts")]
        cfg["<b>lcart.toml</b><br/>[TOML file]<br/>project, module rules"]
        bundle["<b>Handoff bundle</b><br/>[directory]<br/>PROMPT.md, facts/*.json,<br/>claim schema, MANIFEST.json"]
        agentout["<b>Agent output</b><br/>[directory]<br/>Markdown docs, claims.json"]
        out["<b>Output directory</b><br/>[Markdown, Mermaid, JSON]<br/>module map, annotated docs,<br/>verification report, test skeletons"]
        clang["<b>Clang/LLVM 20</b><br/>[Library, LibTooling]<br/>parses C/C++"]
        src["<b>Analysed sources</b><br/>+ compile_commands.json"]
        agent["<b>Coding agent</b><br/>[external process, user-run]"]
    end

    llmapi["<b>Agent's LLM provider</b><br/>[outside, agent's choice]"]

    engineer --> cli
    engineer --> agent
    cli -- "reads" --> cfg
    cli -- "links" --> clang
    clang -- "parses (read-only)" --> src
    cli -- "reads/writes SQL" --> db
    cli -- "writes" --> bundle
    agent -- "reads" --> bundle
    agent -- "writes" --> agentout
    cli -- "reads, validates" --> agentout
    cli -- "writes" --> out
    agent -.-> llmapi
```

Notes:

- One process, one database file per analysed project. There is no server
  and no background service. `lcart` opens no network connection in v1.
- The fact database, bundles and outputs contain names (and, with
  `--include-source`, excerpts) of the analysed project. They are as
  sensitive as the code ([security](../security.md)).
- Configuration format: [ADR-0008](../adr/0008-configuration-file-format.md).
