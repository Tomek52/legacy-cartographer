# legacy-cartographer

`lcart` is a command-line tool that maps large C/C++ code bases. It builds a
database of facts from static analysis (files, modules, functions, call
graph, includes) and can use an LLM to write documentation from those facts.
Every structural statement the LLM makes is recorded as a machine-readable
claim and checked against the fact database: **confirmed**, **unverified** or
**contradicted**. The verification report ships with the documentation.

The public demonstration target is [libmodbus](targets/libmodbus.md), a
small, real-world C library that parses an industrial protocol.

**Status:** M0 (foundations). No analysis is implemented yet. See the
[roadmap](plan/roadmap.md).

## Where to start

| If you want to… | Read |
|---|---|
| understand the problem and the scope | [Vision](vision.md) |
| know what the tool must do | [Requirements](specs/requirements.md) |
| understand claims and their verification | [Claim schema](specs/claim-schema.md) |
| see how it is built | [Architecture](architecture/index.md) |
| know why it is built that way | [Decisions (ADR)](adr/index.md) |
| see how it is tested | [Testing strategy](testing/strategy.md) |
| assess the risks of running it on private code | [Security](security.md) |
| pick up work | [Roadmap](plan/roadmap.md), [Backlog](plan/backlog.md) |
| see what is still undecided | [Open questions](open-questions.md) |

## How this repository is developed

Specification first: requirements, decisions and acceptance criteria are
written before code, and code changes refer to them by ID. The process,
including how LLM coding agents are used, is in
[AI-assisted workflow](ai-workflow.md). Terms are defined in the
[glossary](glossary.md).
