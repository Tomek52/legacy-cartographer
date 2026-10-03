# Specifications

What the tool must do, independent of how it is built.

- [Requirements](requirements.md): functional (FR-xxx) and non-functional
  (NFR-xxx) requirements in EARS form, each with acceptance criteria.
- [Claim schema](claim-schema.md): the structured form of LLM claims and the
  rules that verify them.
- [Agent handoff contract](handoff-contract.md): the bundle given to an
  external coding agent, the ready prompt, and the output `lcart ingest`
  accepts.

Requirements and rules carry versioned anchors
([ADR-0005](../adr/0005-versioned-requirement-anchors.md)): link to the
specific item, and bump its `vN` when its meaning changes.
