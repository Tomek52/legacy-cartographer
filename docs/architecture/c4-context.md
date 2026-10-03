# C4 level 1: system context

```mermaid
flowchart TB
    engineer["<b>Engineer / architect</b><br/>[Person]<br/>Maps and documents a C/C++ code base"]
    lcart["<b>legacy-cartographer (lcart)</b><br/>[Software system]<br/>Fact database, module map, verified<br/>LLM documentation, test skeletons"]
    code["<b>Analysed code base</b><br/>[External system]<br/>Sources + compile_commands.json<br/>(e.g. libmodbus, or private code)"]
    local["<b>Local LLM</b><br/>[External system, optional]<br/>Ollama / OpenAI-compatible on loopback"]
    remote["<b>Remote LLM API</b><br/>[External system, optional]<br/>OpenAI-compatible, Anthropic"]
    docs["<b>Documentation site</b><br/>[External system]<br/>MkDocs or any Markdown viewer"]

    engineer -- "runs CLI commands" --> lcart
    lcart -- "reads (never modifies)" --> code
    lcart -- "prompts with facts and excerpts" --> local
    lcart -. "only with --allow-remote-source" .-> remote
    lcart -- "writes Markdown, Mermaid, reports" --> docs
    engineer -- "reads, reviews" --> docs
```

**Trust boundaries.** The analysed code and everything the LLM returns are
untrusted input. The remote API is outside the machine: crossing that
boundary with source code requires explicit, per-invocation consent
([security](../security.md)).
