# Vision

## Problem

Large C and C++ code bases that have been maintained for decades often have
no current documentation. The people who designed them have moved on; the
code is the only reliable description. Before such a system can be changed
safely, an engineer has to answer basic questions: which modules exist, what
depends on what, who calls this function, which behaviour must be preserved.
Answering them by reading code takes weeks.

LLMs can summarise code quickly, but their output cannot be trusted as is.
A plausible but wrong statement such as "module A does not depend on module B" in
a design document is worse than no document, especially in regulated
environments, where documentation is evidence.

Many of these code bases are proprietary and sensitive. Sending them to an
external API is often not allowed.

## Idea

1. Build a **fact database** from static analysis with Clang. It is
   deterministic, needs no LLM, and is useful on its own (module map, call
   graph queries).
2. Let a coding agent (optional, e.g. Claude Code) write documentation
   **from the facts**, using a ready prompt from `lcart`, and make it state
   its structural assertions as **structured claims**.
3. **Verify every claim** against the fact database, and publish the
   verification report next to the documentation.
4. Help with the first safe change: generate **characterization test**
   skeletons (GoogleTest) for a chosen function, following *Working
   Effectively with Legacy Code* (Feathers).

## Who it is for

- Engineers who inherit or modernise a large C/C++ code base and need a map
  before changing it.
- Architects who have to produce or review architecture documentation of an
  existing system and must be able to show where each statement comes from.
- Teams in regulated domains (medical, automotive, industrial), where code
  may not leave the machine and documentation must be traceable.

## What it is not

- **Not a compiler, linter or bug finder.** It records structure; it does not
  judge code quality. Use clang-tidy, static analysers and sanitizers for that.
- **Not a refactoring tool.** It never edits the analysed code.
- **Not a replacement for reading the code.** It shortens the path to the
  right place; it does not prove behaviour.
- **Not a proof of documentation correctness.** Verification covers only the
  claim kinds in the [claim schema](specs/claim-schema.md) (structure: calls,
  includes, dependencies, membership). Statements about *behaviour* or
  *intent* remain unverified prose, and the report says so.
- **Not a hosted service and not an LLM client (yet).** It is a local CLI
  that makes no network requests. LLM work is done by a coding agent the
  user runs on a handoff bundle; built-in providers come later
  ([ADR-0006](adr/0006-external-agent-handoff-for-llm-work.md)).
- **Not a whole-language C++ semantic model.** Templates, macros and
  function pointers are handled as far as Clang's AST allows; where the call
  target is not statically known, the tool says so instead of guessing.

## Success looks like

- On libmodbus: a module map and call graph produced without any LLM in under
  a minute, a generated architecture description whose verification report
  shows no contradicted claims left unflagged, characterization tests for the
  frame-parsing path, a fuzzing harness for it, and a C++20 wrapper with tests
  (the wrapper's API stays C++20 for its users; the tool itself is C++23).
- The same tool, run on a private code base, produces the fact database and
  module map fully offline, and a documentation bundle whose content (facts
  only, by default) the user reviews before any agent sees it.
