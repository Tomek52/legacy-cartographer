# C4 level 1: system context

```mermaid
flowchart TB
    engineer["<b>Engineer / architect</b><br/>[Person]<br/>Maps and documents a C/C++ code base"]
    lcart["<b>legacy-cartographer (lcart)</b><br/>[Software system]<br/>Fact database, module map, handoff bundles,<br/>claim verification, test skeletons"]
    code["<b>Analysed code base</b><br/>[External system]<br/>Sources + compile_commands.json<br/>(e.g. libmodbus, or private code)"]
    agent["<b>Coding agent</b><br/>[External system, user-run]<br/>e.g. Claude Code, with its own LLM provider"]
    providers["<b>LLM APIs / local models</b><br/>[External, M6 only]<br/>built-in providers"]
    docs["<b>Documentation site</b><br/>[External system]<br/>MkDocs or any Markdown viewer"]

    engineer -- "runs CLI commands" --> lcart
    engineer -- "runs on the bundle,<br/>restricted permissions" --> agent
    lcart -- "reads (never modifies)" --> code
    lcart -- "handoff bundle (files)" --> agent
    agent -- "docs + claims.json (files)" --> lcart
    lcart -. "M6: prompts" .-> providers
    lcart -- "writes annotated docs, reports" --> docs
    engineer -- "reads, reviews" --> docs
```

**Trust boundaries.** The analysed code and everything the agent returns are
untrusted input. In v1 `lcart` itself sends nothing over the network; the
bundle reaches an LLM provider only through the agent the user chooses and
configures ([security](../security.md)).
