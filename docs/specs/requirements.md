# Requirements

Each requirement has an ID, a version, the milestone that delivers it, an
EARS statement and acceptance criteria. Tests cite the anchor ID of the
requirement they verify (for example `// Verifies: fr004-record-call-edges-v1`).

EARS patterns used: *ubiquitous* ("The tool shall…"), *event-driven*
("When…, the tool shall…"), *state-driven* ("While…"), *unwanted behaviour*
("If…, then the tool shall…"), *optional feature* ("Where…").

"The tool" means the `lcart` executable. Commands are named as planned in
`src/cli/main.cpp`; options may change until the milestone that delivers
them.

## Functional requirements

### Static analysis and fact database (M1)

#### <a id="fr001-ingest-compilation-database-v1"></a>**[FR-001 v1]** Ingest a compilation database

When the user runs `lcart analyze` with a compilation database, the tool
shall parse every translation unit it lists with the recorded compiler
arguments, and store the results in a fact database file.

Acceptance criteria:

1. Given the fixture project `tests/fixtures/two_modules` (3 TUs), the fact
   database records 3 parsed TUs and coverage 3/3.
2. If a TU fails to parse, then the tool records a diagnostic for it,
   continues with the remaining TUs, and reports coverage below 100%.
3. The tool exits non-zero only if no TU could be parsed or the database
   cannot be written.
4. Compiler arguments are passed to the Clang frontend only; no compile
   command is executed as a process ([security](../security.md)).

#### <a id="fr002-record-files-and-includes-v1"></a>**[FR-002 v1]** Record files and includes

The tool shall record every source and header file reached during analysis
(path relative to the analysis root, SHA-256 of the content, header or
source) and every `#include` directive (includer, included file if found,
spelling, line, system or user include).

Acceptance criteria:

1. For the fixture, the set of `(includer, included)` pairs equals the
   hand-written expected list.
2. An include that cannot be resolved is recorded with no target and the
   spelled name.
3. Files outside the analysis root (system headers) are recorded only as
   include targets, flagged as external, and never assigned to a module.

#### <a id="fr003-record-functions-v1"></a>**[FR-003 v1]** Record functions

The tool shall record every function declaration and definition in files
under the analysis root with its USR, name, qualified name, signature, file,
line range, linkage and whether it is a definition.

Acceptance criteria:

1. Two `static` functions with the same name in different files are
   recorded as two functions with different USRs.
2. A function declared in a header and defined in a source file is one
   function (one USR) with one definition location.
3. Line ranges of definitions match the fixture's expected values.

#### <a id="fr004-record-call-edges-v1"></a>**[FR-004 v1]** Record call edges

The tool shall record each call expression inside a function body as a call
edge with caller, call site and kind: `direct` with the callee's identity,
or `indirect` when the callee is reached through a pointer.

Acceptance criteria:

1. Direct calls in the fixture produce exactly the expected
   `(caller, callee)` set.
2. A call through a function pointer or a struct member function pointer is
   recorded as `indirect`, with the expression's member name when available
   (for example `backend.recv`), and no callee.
3. Calls in macros are attributed to the function where the macro expands.

#### <a id="fr005-map-files-to-modules-v1"></a>**[FR-005 v1]** Map files to modules

The tool shall assign every file under the analysis root to exactly one
module, using ordered path-glob rules from the project configuration file;
a file matching no rule belongs to the module named after its top-level
directory under the analysis root (files directly in the root: `(root)`).

The default alone is too coarse for libmodbus, whose library sources all sit
in `src/`; its configuration defines `core`, `rtu` and `tcp` modules by file
name ([libmodbus plan](../targets/libmodbus.md)).

Acceptance criteria:

1. With no configuration, the fixture yields the expected default modules.
2. With rules, the first matching rule wins; a test covers overlapping
   globs.
3. Module dependencies are derived from include and call edges between files
   of different modules and stored with the edges that justify them.

#### <a id="fr006-query-and-export-facts-v1"></a>**[FR-006 v1]** Query and export facts

When the user runs `lcart query` with one of `callers`, `callees`,
`includes`, `included-by`, `module-deps`, the tool shall print the matching
facts; when the user runs `lcart query export`, the tool shall write all
facts as JSON Lines.

Acceptance criteria:

1. `callers <function>` lists every direct caller, sorted by file and line.
2. An ambiguous function name prints all candidates with their files and
   exits with a distinct status.
3. The export of the fixture equals the committed golden file byte for byte.

#### <a id="fr007-module-map-without-llm-v1"></a>**[FR-007 v1]** Module map without LLM

When the user runs `lcart map`, the tool shall write a Markdown document with
the module list (files, function counts) and a Mermaid graph of module
dependencies, using only the fact database.

Acceptance criteria:

1. The output renders with MkDocs + Mermaid without errors (checked in CI for
   the libmodbus map).
2. The command works with no LLM provider configured and no network access.
3. Running it twice on the same database produces identical output.

### LLM documentation and verification (M2)

#### <a id="fr008-generate-documentation-v1"></a>**[FR-008 v1]** Generate documentation with an LLM

Where an LLM provider is configured, when the user runs `lcart document`, the
tool shall generate per-module descriptions, C4 context and container
diagrams in Mermaid, and a glossary, using the fact database as the primary
input and source excerpts only as allowed by [NFR-002](#nfr002-no-source-egress-by-default-v1).

Acceptance criteria:

1. With a stub provider returning canned responses, the command writes the
   expected Markdown files.
2. Every generated file states that it is LLM-generated, which provider and
   model produced it, and links to the verification report.
3. Generated Mermaid that fails to parse is reported as an error, not
   written as a diagram.

#### <a id="fr009-structured-claims-v1"></a>**[FR-009 v1]** Structured claims

The tool shall require the LLM to emit every structural statement as a claim
conforming to the [claim schema](claim-schema.md), and shall link each claim
to the sentence in the generated text that states it.

Acceptance criteria:

1. Output that does not validate against the schema is rejected with
   `schema_violation`; the tool retries at most N times (configurable,
   default 2) and then reports the section as failed.
2. Claims of a kind not in the schema are rejected, not ignored.
3. The parser never crashes on arbitrary input (fuzzed, see
   [testing strategy](../testing/strategy.md)).

#### <a id="fr010-verify-claims-v1"></a>**[FR-010 v1]** Verify claims

When claims are produced or supplied by the user (`lcart verify <claims.json>`),
the tool shall assign each claim the status `confirmed`, `unverified` or
`contradicted` with a reason and the supporting fact IDs, following the
[verification rules](claim-schema.md#claim-verification-rules-v1).

Acceptance criteria:

1. Every rule in the claim schema has at least one test per possible
   outcome.
2. Verification is deterministic and needs no LLM.
3. `lcart verify` works on a hand-written claims file without any provider
   configured.

#### <a id="fr011-verification-report-v1"></a>**[FR-011 v1]** Verification report

The tool shall write a verification report in Markdown and JSON with counts
per status, every contradicted and unverified claim with its reason and
evidence, and the analysis coverage.

Acceptance criteria:

1. Status names in JSON are exactly `confirmed`, `unverified`,
   `contradicted`.
2. Sentences in the generated documentation whose claims are contradicted
   are marked in the documentation itself, not only in the report.
3. The report records tool version, fact database schema version, provider
   and model.

#### <a id="fr012-provider-selection-v1"></a>**[FR-012 v1]** Provider selection

The tool shall support the providers `none` (default), `openai-compatible`,
`anthropic` and `ollama`, selected by configuration, with API keys read from
environment variables only.

Acceptance criteria:

1. With provider `none`, `lcart document` exits with a clear message and no
   network activity.
2. Each provider is tested against a local HTTP stub that checks the request
   format.
3. An API key in the configuration file is rejected with an error.

#### <a id="fr013-explicit-remote-consent-v1"></a>**[FR-013 v1]** Explicit consent for remote egress

If a request containing source code would be sent to a remote provider and
the `--allow-remote-source` flag was not given on the command line, then the
tool shall not send it and shall fail with `egress_denied`, naming the files
the request would have contained.

Acceptance criteria:

1. Decision table test: {local, remote} × {contains source, no source} ×
   {flag, no flag} gives the expected allow/deny for all 8 cases.
2. The flag cannot be set from the configuration file or an environment
   variable.
3. A provider whose endpoint resolves to a non-loopback address is remote,
   even if it is an Ollama server.

#### <a id="fr014-egress-log-v1"></a>**[FR-014 v1]** Egress log

While a provider is in use, the tool shall append to an egress log, for each
request, the provider, endpoint, time, size and the list of files and line
ranges whose content was included.

Acceptance criteria:

1. A `--dry-run` mode writes the prompts and the egress log without sending
   anything.
2. The log never contains API keys.

#### <a id="fr015-resolve-function-pointer-tables-v1"></a>**[FR-015 v1]** Resolve function-pointer tables

Where a struct field of function-pointer type is assigned functions in
static initializers (for example a backend table), the tool shall record
`may_call` edges from indirect calls through that field to every function
assigned to it.

Acceptance criteria:

1. For libmodbus, an indirect call through the backend's `check_integrity`
   field yields `may_call` edges to the RTU and TCP implementations
   (TODO: verify the initializer form in `modbus-rtu.c` / `modbus-tcp.c`
   when B-10 is done).
2. `may_call` edges are reported separately from direct calls and never
   turn a `calls` claim into `confirmed` (see the claim schema).

### Characterization tests (M3)

#### <a id="fr016-characterization-test-skeleton-v1"></a>**[FR-016 v1]** Characterization test skeleton

When the user runs `lcart testgen <function>`, the tool shall generate a
GoogleTest source file for that function with the includes it needs, one
test per scenario placeholder, the function's direct callees and indirect
call sites listed as candidate seams, and assertions marked to be filled in
with the observed current behaviour.

Acceptance criteria:

1. The generated file compiles against the target library unchanged (tested
   on a fixture and on one libmodbus function).
2. Generated tests fail until a human fills in the observed values (they
   assert on a placeholder that cannot pass), so an unfinished test cannot
   pass silently.
3. Generation is deterministic and needs no LLM; an LLM may optionally
   propose input scenarios, which are marked as such.

## Non-functional requirements

#### <a id="nfr001-works-without-llm-v1"></a>**[NFR-001 v1]** Works without an LLM

The tool shall provide analysis, query, module map, claim verification and
test skeleton generation with no LLM provider configured and no network
access.

Acceptance criteria:

1. The deterministic modules (`common`, `facts`, `analysis`, `claims`,
   `verify`, `report`) do not link the `llm` module; the build fails at
   configure time if they do (`cmake/ArchitectureRules.cmake`).
2. CI runs the M1 commands on libmodbus in a job without network access
   (TODO: verify how to cut network in GitHub Actions; fallback is
   `unshare -n`).

#### <a id="nfr002-no-source-egress-by-default-v1"></a>**[NFR-002 v1]** No source egress by default

The tool shall not send source code, comments, identifiers or file paths of
the analysed project to any non-loopback network endpoint unless the
`--allow-remote-source` flag is given for that invocation.

Acceptance criteria:

1. Covered by [FR-013](#fr013-explicit-remote-consent-v1) tests.
2. The default configuration uses provider `none`.
3. Telemetry: none. The tool makes no network request that is not an LLM
   request the user configured.

#### <a id="nfr003-fully-local-mode-v1"></a>**[NFR-003 v1]** Fully local mode

Where a local provider (Ollama or an OpenAI-compatible server on loopback)
is configured, the tool shall generate documentation and claims with no
data leaving the machine.

Acceptance criteria:

1. An end-to-end test with a loopback stub server passes with outbound
   network blocked.
2. The documentation names a tested local model and its hardware needs
   (TODO: verify after M2 which model sizes give usable claims).

#### <a id="nfr004-deterministic-output-v1"></a>**[NFR-004 v1]** Deterministic output

The tool shall produce byte-identical fact exports, module maps and
verification results for identical inputs, independent of thread count,
file system order and time of day.

Acceptance criteria:

1. Golden-file tests for export and map.
2. A test runs the analysis with 1 and N threads and compares exports.
3. No timestamps in exported facts; the report's generation time is in a
   separate metadata block.

#### <a id="nfr005-performance-budget-v1"></a>**[NFR-005 v1]** Performance budget

The tool shall analyse libmodbus (about 5.8 kLOC in `src/` at v3.2.0) in
under 60 seconds on a 4-core developer laptop, and record the time in CI.

Acceptance criteria:

1. CI prints the analysis time for libmodbus; a regression above 2× the
   recorded baseline fails the job.
2. A budget for 1 MLOC code bases is set after the M1 baseline (open
   question).

#### <a id="nfr006-toolchain-baseline-v1"></a>**[NFR-006 v1]** Toolchain baseline

The tool shall build with C++20 on Ubuntu 24.04 with GCC 13 and Clang 18,
using CMake presets and Conan 2, and also from distribution packages without
Conan (`debug-system` preset) for offline environments.

Acceptance criteria:

1. CI builds and tests the `debug`, `clang-debug`, `release` and
   `asan-ubsan` presets.
2. `cmake --workflow --preset debug-system` passes on a machine with only
   Ubuntu packages.

#### <a id="nfr007-quality-gates-v1"></a>**[NFR-007 v1]** Quality gates

Every change shall pass clang-format, clang-tidy (warnings as errors), the
unit tests under ASan and UBSan, and the strict documentation build.

Acceptance criteria:

1. Each gate is a CI job and a script under `scripts/` that runs the same
   check locally.
2. New suppressions (`NOLINT`) carry a reason on the same line.

#### <a id="nfr008-upstream-unmodified-v1"></a>**[NFR-008 v1]** Analysed targets stay unmodified

The repository shall contain analysed third-party code only as pinned git
submodules under `targets/`, with no modifications.

Acceptance criteria:

1. CI checks that each submodule is at the pinned commit and its working
   tree is clean.
2. Build files, harnesses and wrappers for a target live outside its
   submodule directory.

#### <a id="nfr009-verifier-accuracy-measured-v1"></a>**[NFR-009 v1]** Verifier accuracy is measured

The project shall measure the verifier's accuracy on a labelled claim set
and publish the result with each release.

Acceptance criteria:

1. A labelled set of at least 200 claims over the fixtures and libmodbus,
   including deliberately false claims (see [testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1)).
2. Precision of `confirmed` ≥ 0.99 and precision of `contradicted` ≥ 0.95;
   CI fails below these.

#### <a id="nfr010-untrusted-input-v1"></a>**[NFR-010 v1]** Analysed code and LLM output are untrusted

The tool shall treat analysed source code, compilation databases and LLM
output as untrusted input: it never executes them, and every parser of them
is fuzzed.

Acceptance criteria:

1. Fuzz targets for the claim parser and the configuration parser run in
   CI for at least 60 s per change.
2. Prompts delimit analysed code as data
   ([security](../security.md#security-prompt-injection-v1)).

#### <a id="nfr011-licensing-v1"></a>**[NFR-011 v1]** Licensing

The tool's own code shall be licensed under Apache-2.0, and any component
that links libmodbus shall meet the LGPL-2.1 obligations in
[ADR-0003](../adr/0003-licensing.md).

Acceptance criteria:

1. Every distributed artefact that links libmodbus links it dynamically, or
   ships with the means to relink it.
2. `NOTICE` and the docs state that `targets/` is not covered by Apache-2.0.

#### <a id="nfr012-traceability-v1"></a>**[NFR-012 v1]** Traceability

Every requirement shall be traceable to the tests that verify it.

Acceptance criteria:

1. Each FR/NFR has a versioned anchor; `scripts/check-doc-anchors.py` checks
   format and uniqueness.
2. From M1, the checker lists requirements of completed milestones that no
   test cites, and CI fails on any (backlog [B-15](../plan/backlog.md#b15-requirement-test-traceability-v1)).
