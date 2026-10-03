# C4 level 2: containers

```mermaid
flowchart TB
    engineer["<b>Engineer</b><br/>[Person]"]

    subgraph machine["User's machine"]
        cli["<b>lcart</b><br/>[C++20 executable]<br/>All analysis, verification,<br/>generation and reporting"]
        db[("<b>Fact database</b><br/>[SQLite file]<br/>files, includes, functions,<br/>calls, modules, claims, verdicts")]
        cfg["<b>Project config</b><br/>[TOML/YAML file, TBD]<br/>module rules, provider"]
        out["<b>Output directory</b><br/>[Markdown, Mermaid, JSON]<br/>module map, docs, report,<br/>test skeletons, egress log"]
        clang["<b>Clang/LLVM 18</b><br/>[Library, LibTooling]<br/>parses C/C++"]
        src["<b>Analysed sources</b><br/>+ compile_commands.json"]
        ollama["<b>Local LLM server</b><br/>[optional, loopback]"]
    end

    remote["<b>Remote LLM API</b><br/>[optional]"]

    engineer --> cli
    cli -- "reads" --> cfg
    cli -- "links" --> clang
    clang -- "parses (read-only)" --> src
    cli -- "reads/writes SQL" --> db
    cli -- "writes" --> out
    cli -- "HTTP, loopback" --> ollama
    cli -. "HTTPS, explicit consent only" .-> remote
```

Notes:

- One process, one database file per analysed project. There is no server
  and no background service.
- The fact database and the output directory may contain the analysed
  project's source code (excerpts, names). They are as sensitive as the code
  itself ([security](../security.md)).
- The configuration file format is an open question
  ([open questions](../open-questions.md)).
