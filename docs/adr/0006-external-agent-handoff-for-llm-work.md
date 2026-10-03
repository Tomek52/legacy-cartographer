# 0006. LLM work through an external agent handoff in v1

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [FR-008](../specs/requirements.md#fr008-generate-documentation-v2), [FR-009](../specs/requirements.md#fr009-structured-claims-v2), [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2)
- **Supersedes:** the [provider interface](0004-llm-provider-abstraction-and-code-privacy.md#adr0004-provider-interface-v1) and [egress gate](0004-llm-provider-abstraction-and-code-privacy.md#adr0004-egress-gate-v1) decisions of ADR-0004 **for v1 (M2)**; they move to M6. The rest of ADR-0004 stands.

## Context

ADR-0004 planned built-in HTTP providers (OpenAI-compatible, Anthropic,
Ollama) for M2. For a project with 4–6 hours a week this means three HTTP
clients, an HTTP library, API key handling, retries and an egress gate, all
before the part that matters (claims and their verification) can be shown.

Capable coding agents (for example Claude Code) already run on the user's
machine, read files, follow a written brief and write Markdown and JSON.
Many organisations already have a policy decision about which agent may be
used on which code, so the tool does not need to make that decision itself.

## Options considered

1. **Built-in providers in M2** (ADR-0004 as written). Full control, much
   more code; delays verification.
2. **External agent handoff.** `lcart` writes a bundle (facts, claim schema,
   a ready prompt, an output contract); the user runs an agent of their choice
   on it; `lcart` ingests and verifies the agent's output. No network code in
   the tool.
3. **MCP server exposing the fact database to an agent.** Lets the agent
   query facts interactively. Attractive, but adds a protocol dependency
   and a server; a better fit after the batch flow works (candidate for M6).

## Decision

<a id="adr0006-handoff-in-v1-v1"></a>**v1 uses an external agent through a
handoff bundle.** `lcart handoff` writes a bundle as specified in the
[handoff contract](../specs/handoff-contract.md); the user runs an agent on
it; `lcart ingest` validates the output, verifies the claims and writes the
report.

<a id="adr0006-no-network-in-v1-v1"></a>**The tool makes no network requests
in v1.** What leaves the machine is decided by the user when choosing and
configuring the agent. By default the bundle contains facts only (names,
paths, edges); source excerpts are added only with `--include-source` on the
command line, and `MANIFEST.json` lists every file and excerpt included.

<a id="adr0006-handoff-is-deterministic-core-v1"></a>**`handoff` is part of
the deterministic core**: it writes files from facts and reads files back.
The `llm` module stays reserved for built-in providers (M6) and the core
must still not depend on it.

<a id="adr0006-providers-deferred-v1"></a>**Built-in providers, the egress
gate and local models move to M6.** Their requirements stay in the spec,
marked deferred.

## Consequences

- M2 shrinks to: bundle writer, prompt, ingest, claim parser, verifier,
  report. The verifier, the core idea, is reached sooner.
- The prompt and the output contract become versioned artefacts; changing
  them changes results, so they are tested with recorded agent outputs.
- Fully local LLM use is not available until M6. Until then the no-LLM mode
  (NFR-001) is the offline mode.
- An agent with tools (file access, shell) widens the prompt-injection risk:
  injected text could try to make the agent *do* things, not only *say*
  things. The handoff contract and [security](../security.md#security-agent-tools-v1)
  require running the agent with restricted permissions.
- Results depend on the agent and model the user chooses; the report records
  what the user states was used (`--agent` option of `lcart ingest`).
