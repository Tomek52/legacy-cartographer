# 0004. LLM provider abstraction and code privacy

- **Status:** Accepted; provider interface and egress gate deferred to M6 by [ADR-0006](0006-external-agent-handoff-for-llm-work.md)
- **Date:** 2026-10-03
- **Requirements:** [FR-012](../specs/requirements.md#fr012-provider-selection-v1), [FR-013](../specs/requirements.md#fr013-explicit-remote-consent-v1), [FR-014](../specs/requirements.md#fr014-egress-log-v1), [NFR-001](../specs/requirements.md#nfr001-works-without-llm-v1), [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2), [NFR-003](../specs/requirements.md#nfr003-fully-local-mode-v1)

## Context

- The tool will be run on proprietary, sometimes regulated code. Sending it
  to a third-party API may be forbidden by contract, policy or law. A single
  accidental request is a data leak that cannot be undone.
- Model quality and availability change quickly; users will want different
  providers (a corporate OpenAI-compatible gateway, Anthropic, a local model).
- Static analysis must stay useful with no LLM at all.

## Options considered

1. **One hard-coded provider.** Simplest; locks users in and does not
   address offline use.
2. **A third-party multi-provider client library.** Saves code, but adds a
   dependency whose network behaviour we do not control and must audit.
3. **Own thin provider interface, few implementations, one egress gate.**
   More code (three small HTTP clients), full control over what leaves the
   machine.

## Decision

<a id="adr0004-provider-interface-v1"></a>**Provider interface.** `llm::LlmProvider`
(`src/llm/provider.hpp`) with implementations: `none` (default),
`openai-compatible` (also covers many local servers and corporate gateways),
`anthropic`, `ollama`. A provider reports its `Locality`; it is `local` only
if its endpoint resolves to a loopback address.

<a id="adr0004-egress-gate-v1"></a>**One egress gate.** Every request goes
through `llm::may_send()` (`src/llm/egress_policy.hpp`) before reaching a
provider. A request that contains anything from the analysed project (code,
comments, identifiers, paths) and targets a remote provider is sent only if
`--allow-remote-source` was given **on the command line of that
invocation**. It cannot be set in a configuration file or environment
variable. The prompt builder marks requests as containing source by default;
clearing the mark requires a code path that provably uses no project data.

<a id="adr0004-no-llm-dependency-in-core-v1"></a>**The deterministic core
does not depend on `llm`.** Enforced at configure time by
`cmake/ArchitectureRules.cmake`. The `none` provider is the default, and all
M1 commands work without network access.

<a id="adr0004-egress-log-v1"></a>**Egress log and dry run.** Every request
that is sent is recorded (provider, endpoint, size, files and line ranges
included). `--dry-run` writes the prompts and the log without sending.

<a id="adr0004-secrets-v1"></a>**Secrets.** API keys come from environment
variables only; a key found in a configuration file is an error. Keys never
appear in logs.

<a id="adr0004-llm-output-untrusted-v1"></a>**LLM output is data.** It is
parsed against the claim schema and rendered as Markdown; it is never
executed, never used to build shell commands or SQL, and the LLM gets no
tools (no function calling).

## Consequences

- The privacy guarantee rests on one small, exhaustively tested function
  and an architecture rule, which are easy to audit.
- We write and maintain three thin HTTP clients. The HTTP library is an open
  decision for M2 (candidates: libcurl, cpp-httplib; new ADR).
- Default behaviour is safe and also unhelpful for documentation generation:
  users must choose a provider. That is intended.
- Redaction (e.g. replacing identifiers before sending) is not part of this
  decision; it can be added later behind the same gate.
