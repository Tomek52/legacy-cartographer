# C4 level 3: components of `lcart`

Each component is a module under `src/` and a CMake target `lcart::<name>`.
Arrows are build dependencies (who includes and links whom).

```mermaid
flowchart TB
    cli["<b>cli</b><br/>command-line parser, analyze / query / map /<br/>handoff / verify, git exposure warning"]

    subgraph det["Deterministic core (no LLM, no network)"]
        analysis["<b>analysis</b><br/>Clang LibTooling frontend,<br/>module rules"]
        config["<b>config</b><br/>lcart.toml (toml++), path globs"]
        facts["<b>facts</b><br/>fact model, FactWriter / FactQuery,<br/>SQLite store, JSON Lines export"]
        claims["<b>claims</b><br/>claim model, JSON parser + validation"]
        verify["<b>verify</b><br/>per-kind verification rules"]
        report["<b>report</b><br/>module map, verification report,<br/>document annotation"]
        handoff["<b>handoff</b><br/>bundle writer, agent output reader"]
        common["<b>common</b><br/>Result (std::expected), errors, version"]
    end

    subgraph later["Later (M6)"]
        llm["<b>llm</b><br/>LlmProvider, egress gate,<br/>built-in providers"]
    end

    testgen["<b>testgen</b> (M3)<br/>characterization test skeletons"]

    cli --> analysis & config & facts & report & handoff & verify & llm & testgen
    analysis --> facts & config
    config --> common
    handoff --> claims & facts
    verify --> claims & facts
    report --> verify & handoff
    claims --> common
    facts --> common
    llm --> common
    testgen --> facts
```

No module of the deterministic core may depend on `llm`; the build fails at
configure time if one links it (`cmake/ArchitectureRules.cmake`), and a
test (label `architecture`) fails if one includes a header from `src/llm/`
or if a Clang or LLVM header is included outside `analysis`
(`cmake/CheckIncludeBoundaries.cmake`).

The `ingest` command in `cli` chains the core: `handoff::read_agent_output`
→ `verify::verify_claim` for each claim → `report` writes the annotated
documents and the report. Nothing in this chain needs an LLM, so recorded
agent outputs make it fully testable.
