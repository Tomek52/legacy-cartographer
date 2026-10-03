# C4 level 3: components of `lcart`

Each component is a module under `src/` and a CMake target `lcart::<name>`.
Arrows are build dependencies (who includes and links whom).

```mermaid
flowchart TB
    cli["<b>cli</b><br/>command dispatch, options"]

    subgraph det["Deterministic core (no LLM, no network)"]
        analysis["<b>analysis</b><br/>Clang LibTooling frontend,<br/>module rules"]
        facts["<b>facts</b><br/>fact model, FactWriter / FactQuery,<br/>SQLite store"]
        claims["<b>claims</b><br/>claim model, JSON parser + validation"]
        verify["<b>verify</b><br/>per-kind verification rules"]
        report["<b>report</b><br/>module map, verification report"]
        common["<b>common</b><br/>Result, errors, version"]
    end

    subgraph gen["LLM layer (optional)"]
        llm["<b>llm</b><br/>LlmProvider, egress policy,<br/>providers, prompt builder"]
    end

    testgen["<b>testgen</b> (M3)<br/>characterization test skeletons"]

    cli --> analysis & report & llm & testgen
    analysis --> facts
    verify --> claims & facts
    report --> verify
    claims --> common
    facts --> common
    llm --> common
    testgen --> facts
```

No module of the deterministic core may depend on `llm`; the build fails at
configure time if one does (`cmake/ArchitectureRules.cmake`).

The `llm` module does not depend on `facts` directly: the `document` command
in `cli` reads facts, builds prompts through `llm`, and passes the returned
claims to `verify`. This keeps the LLM layer replaceable and testable with a
stub provider. (Where the prompt builder lives is decided in M2; see
[open questions](../open-questions.md).)
