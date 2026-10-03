# Glossary

**ADR (Architecture Decision Record)**
: A short document recording one decision, its context and consequences.
  See [adr/](adr/index.md).

**Analysis root**
: The directory that paths in the fact database are relative to.

**C4 model**
: A way to describe software architecture in four levels: context,
  containers, components, code. This project uses levels 1–3
  ([architecture](architecture/index.md)).

**Characterization test**
: A test that records what code currently does (not what it should do), so
  that later changes can be checked against it. From M. Feathers, *Working
  Effectively with Legacy Code*.

**Claim**
: A structured statement produced by the LLM about the code's structure, for
  example "function A calls function B". Defined in the
  [claim schema](specs/claim-schema.md).

**Compilation database**
: `compile_commands.json`: the list of translation units and the exact
  compiler arguments used to build them. The tool's input.

**Confirmed / unverified / contradicted**
: The three verification statuses of a claim: the facts support it, the
  facts can neither support nor refute it, or the facts refute it.

**Coverage (analysis coverage)**
: Fraction of translation units in the compilation database that were parsed
  successfully. Absence of a fact is only evidence when coverage is complete.

**Direct call / indirect call**
: A direct call names its callee (`f(x)`). An indirect call goes through a
  function pointer (`ctx->backend->recv(...)`); its callee is not known from
  the call site alone.

**EARS**
: *Easy Approach to Requirements Syntax*: requirement templates such as
  "When <trigger>, the <system> shall <response>". Used in
  [requirements](specs/requirements.md).

**Egress**
: Any data leaving the user's machine. Here: source code in an LLM prompt
  sent to a remote endpoint.

**Fact / fact database**
: A fact is a record derived deterministically from the code by static
  analysis (a file, an include, a function, a call). The fact database is the
  SQLite file holding them ([data model](architecture/data-model.md)).

**LibTooling**
: Clang's C++ library for writing tools that parse code with the real Clang
  frontend ([ADR-0002](adr/0002-static-analysis-frontend.md)).

**Module**
: A named group of files, defined by path rules or by directory. The unit of
  the module map and of `depends_on` claims.

**Prompt injection**
: Text in the analysed code (for example in a comment) that tries to make
  the LLM follow instructions instead of describing the code. See
  [security](security.md).

**Provider (LLM provider)**
: An implementation of the LLM interface for one API: OpenAI-compatible,
  Anthropic, Ollama, or none.

**Seam**
: A place where behaviour can be changed without editing the code there
  (Feathers), for example a function pointer or a link-time substitution.
  Used by the characterization test generator.

**Translation unit (TU)**
: One source file after preprocessing, as compiled by one compiler call.

**USR (Unified Symbol Resolution)**
: Clang's stable string identity for a declaration; used to match the same
  function across translation units.

**Versioned anchor**
: A stable HTML anchor with a content version, such as
  `fr004-record-call-edges-v1`, used to link to one requirement or rule
  ([ADR-0005](adr/0005-versioned-requirement-anchors.md)).
