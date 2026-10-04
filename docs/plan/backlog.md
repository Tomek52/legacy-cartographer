# Backlog (M0–M1)

Each item is small enough for one to three sessions of 2–3 hours. Done
items are marked ✅ in the table; their text stays as the record. Items are
written as contracts: acceptance criteria are turned into tests first
([ADR-0001](../adr/0001-spec-driven-development-and-adrs.md#adr0001-spec-before-code-v1)).
When an item is picked up and needs more detail (interfaces, files), it is
expanded in place; its version is bumped if its meaning changes
([ADR-0005](../adr/0005-versioned-requirement-anchors.md#adr0005-version-bump-rule-v1)).

| ID | Item | Milestone | Requirements | Depends on | Est. |
|---|---|---|---|---|---|
| [B-01](#b01-libmodbus-submodule-v1) | ✅ libmodbus submodule, pinned and checked | M0 | NFR-008 | – | 2 h |
| [B-02](#b02-ci-green-all-presets-v1) | ✅ CI green on all presets with Conan | M0 | NFR-006, NFR-007 | – | 4 h |
| [B-03](#b03-docs-ci-and-anchor-check-v1) | ✅ Docs CI and anchor checker | M0 | NFR-012 | – | 2 h |
| [B-04](#b04-include-boundary-check-v1) | Include boundary check for the core | M0 | NFR-001 | B-02 | 2 h |
| [B-05](#b05-libmodbus-compilation-database-v1) | Compilation database for libmodbus | M1 | FR-001 | B-01 | 3 h |
| [B-06](#b06-sqlite-fact-store-v1) | ✅ SQLite fact store, schema v1 | M1 | FR-002–FR-004, NFR-004 | – | 6 h |
| [B-07](#b07-clang-libtooling-spike-v2) | ✅ Clang LibTooling spike and analysis driver | M1 | FR-001 | B-02 | 6 h |
| [B-08](#b08-file-and-include-facts-v1) | ✅ File and include facts | M1 | FR-002 | B-06, B-07 | 4 h |
| [B-09](#b09-function-facts-v1) | ✅ Function facts | M1 | FR-003 | B-08 | 4 h |
| [B-10](#b10-call-edges-v1) | ✅ Direct and indirect call edges | M1 | FR-004 | B-09 | 5 h |
| [B-11](#b11-module-rules-v2) | Module rules and module dependencies | M1 | FR-005 | B-08 | 4 h |
| [B-12](#b12-query-and-export-v1) | `lcart analyze` / `lcart query` / export | M1 | FR-006, NFR-004 | B-10, B-11 | 5 h |
| [B-13](#b13-module-map-v1) | `lcart map` | M1 | FR-007 | B-12 | 3 h |
| [B-14](#b14-libmodbus-baseline-v1) | libmodbus baseline map and timing | M1 | NFR-001, NFR-005 | B-05, B-13 | 3 h |
| [B-15](#b15-requirement-test-traceability-v2) | Traceability gate and matrix | M1 | NFR-012 | B-03 | 4 h |
| [B-16](#b16-resumable-analysis-v1) | Resumable analysis with progress | M1 | NFR-005, NFR-004 | B-12 | 5 h |
| [B-17](#b17-msvc-code-spike-v1) | MSVC code under Clang: spike and docs | M1 | NFR-013 | B-10 | 4 h |

## M0

### <a id="b01-libmodbus-submodule-v1"></a>**[B-01 v1]** libmodbus submodule, pinned and checked

Add `targets/libmodbus` as a submodule at tag `v3.2.0`, and make CI verify it.

Acceptance criteria:

1. `.gitmodules` lists `targets/libmodbus` with the upstream URL.
2. `git submodule status` shows commit `a9b025d12289855490b10d77461c99e001abfc0f`.
3. `scripts/check-targets.sh` fails if a submodule is not at the commit
   recorded in `docs/targets/*.md` or has local changes; CI runs it.

### <a id="b02-ci-green-all-presets-v1"></a>**[B-02 v1]** CI green on all presets with Conan

Acceptance criteria:

1. The `build` workflow passes for `debug`, `clang-debug`, `release` and
   `asan-ubsan` using `scripts/conan-install.sh`, and for `debug-system`.
2. Conan package versions in `conanfile.py` resolve; if one does not, the
   pin is changed and the open question closed.
3. Conan cache is cached between CI runs (key: hash of `conanfile.py` and
   profiles).
4. The `lint` job runs `scripts/check-format.sh` and
   `scripts/run-clang-tidy.sh` and is green.

### <a id="b03-docs-ci-and-anchor-check-v1"></a>**[B-03 v1]** Docs CI and anchor checker

Acceptance criteria:

1. `scripts/docs-check.sh` passes locally and in the `docs` workflow.
2. A deliberately broken link, an unlisted page, a duplicate anchor and a
   marker with the wrong version each make the check fail (verified once,
   recorded in the PR).
3. The checker's own unit tests run in CI (`python3 -m unittest`).

### <a id="b04-include-boundary-check-v1"></a>**[B-04 v1]** Include boundary check for the core

The configure-time check covers linking; this adds the include side.

Acceptance criteria:

1. A CTest test (label `architecture`) fails if any file under
   `src/{common,facts,analysis,claims,verify,report}/` includes a header
   from `src/llm/`.
2. A self-test fixture with a forbidden include proves the check can fail.
3. No Clang header is included outside `src/analysis/`.

## M1

### <a id="b05-libmodbus-compilation-database-v1"></a>**[B-05 v1]** Compilation database for libmodbus

Acceptance criteria:

1. `scripts/libmodbus-compdb.sh` produces
   `build/targets/libmodbus/compile_commands.json` with entries for
   `modbus.c`, `modbus-data.c`, `modbus-rtu.c`, `modbus-tcp.c`.
2. After the script, `git -C targets/libmodbus status --porcelain` is empty
   (out-of-tree build; see the TODO in the [libmodbus plan](../targets/libmodbus.md)).
3. The script documents its tool requirements (autotools, `bear`) and fails
   with a clear message if one is missing.

### <a id="b06-sqlite-fact-store-v1"></a>**[B-06 v1]** SQLite fact store, schema v1

Implement `FactWriter` and `FactQuery` over SQLite as specified in the
[data model](../architecture/data-model.md).

Acceptance criteria:

1. Round trip: facts written through `FactWriter` are returned by every
   `FactQuery` method, in the documented order.
2. Opening a database with another `schema_version` fails with a clear
   error.
3. All statements use bound parameters (test inserts a name containing
   `'); DROP TABLE file;--`).
4. Tests use an in-memory database; no files left behind.

Result (2026-10-04): `SqliteFactStore` (`src/facts/sqlite_fact_store.hpp`)
implements both interfaces; SQLite is a private dependency of `facts`.
Queries now return `Result` (a read can fail), `FactQuery::diagnostics()`
was added so diagnostics round-trip, `FileFact` gained `is_external` and an
optional module, and `CallFact` gained `via`, so B-08 to B-10 need no schema
change. Schema tests use a named shared in-memory database. Per-TU
transactions and status updates are left to B-16.

### <a id="b07-clang-libtooling-spike-v2"></a>**[B-07 v2]** Clang LibTooling spike and analysis driver

Acceptance criteria:

1. `find_package(Clang 20 CONFIG)` from the distribution packages
   (`libclang-20-dev`, `libclang-cpp20-dev`, `llvm-20-dev`;
   [ADR-0007](../adr/0007-cpp23-and-toolchain-baseline.md)) works in
   all presets; CI installs the packages. If it does not, stop and write an
   ADR before continuing.
2. An `Analyzer` implementation parses each TU of a compilation database and
   reports coverage; a TU with a syntax error is a diagnostic, not a
   failure (`broken_tu` fixture).
3. Arguments that load plugins are stripped with a warning
   ([security T5](../security.md)).
4. Decide and record the threading model (ADR if it deviates from the
   [components page](../architecture/components.md#arch-threading-v1)).

Result (2026-10-04): `find_package` works from the distribution packages
(LLVM's version file accepts only an exact `major.minor`, so CMake finds the
package by path and checks the major version itself). Threading: one TU at a
time for now; the parallel design in
[components](../architecture/components.md#arch-threading-v1) is unchanged and
arrives with B-16, so no ADR. Plugin removal list verified against
`clang-20 --help-hidden` and `-cc1 --help`.

### <a id="b08-file-and-include-facts-v1"></a>**[B-08 v1]** File and include facts

Acceptance criteria: those of [FR-002](../specs/requirements.md#fr002-record-files-and-includes-v1),
on the `two_modules` fixture, plus an unresolved include.

Result (2026-10-04): preprocessor callbacks (`InclusionDirective`,
`FileChanged`) feed `FileFactCollector`
(`src/analysis/src/file_fact_collector.hpp`), one per analysis, so each file
is hashed once (LLVM's `SHA256`, no new dependency) and each directive is
recorded once. The mapping rules are on the
[data model](../architecture/data-model.md#arch-data-model-file-facts-v1)
page. Files under the root get FR-005's default module now; rules and
`exclude_globs` move to B-11, so both use one glob syntax. Diagnostics still
carry Clang's path and no `file_id`. Both are tracked under
[open technical questions](../open-questions.md#open-technical-questions)
until B-11 and B-12 take them up.

### <a id="b09-function-facts-v1"></a>**[B-09 v1]** Function facts

Acceptance criteria: those of [FR-003](../specs/requirements.md#fr003-record-functions-v1),
on the `two_modules` and `static_same_name` fixtures.

Result (2026-10-04): an AST consumer hands every `FunctionDecl` to
`FunctionFactCollector` (`src/analysis/src/function_fact_collector.hpp`),
which writes a function once per USR, and once more when its definition is
first seen. Clang's USR for a `static` function names only a file's base
name, so `left/util.c` and `right/util.c` would share one; the stored USR
gets the path of the defining file as a prefix
([ADR-0012](../adr/0012-function-identity-for-internal-linkage.md)). The
other rules are on the
[data model](../architecture/data-model.md#arch-data-model-function-facts-v2)
page; duplicate external definitions were an open question, answered by
[ADR-0013](../adr/0013-functions-with-external-linkage-defined-twice.md)
in B-10.

### <a id="b10-call-edges-v1"></a>**[B-10 v1]** Direct and indirect call edges

Acceptance criteria: those of [FR-004](../specs/requirements.md#fr004-record-call-edges-v1),
on the `fn_pointers` and `macros` fixtures. The `fn_pointers` fixture
contains a backend table modelled on libmodbus's (struct of function
pointers initialized statically), in preparation for FR-015.

Result (2026-10-04): after a translation unit's functions are recorded,
`CallFactCollector` (`src/analysis/src/call_fact_collector.hpp`) walks the
body of each definition written in that translation unit and records its
call expressions; calls to functions outside the root are not edges. A
third fixture, `two_programs`, settles the open question on functions with
external linkage defined twice: each definition is a function with its own
calls, and a warning
([ADR-0013](../adr/0013-functions-with-external-linkage-defined-twice.md)).
The rules are on the
[data model](../architecture/data-model.md#arch-data-model-call-edges-v1)
page. The libmodbus backend tables use positional initializers, now noted
in FR-015.

### <a id="b11-module-rules-v2"></a>**[B-11 v2]** Module rules and module dependencies

Acceptance criteria: those of [FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1),
plus:

1. Rules are read from `lcart.toml` with toml++
   ([ADR-0008](../adr/0008-configuration-file-format.md)); unknown keys and
   consent switches in the file are errors.
2. A fuzz target for the configuration parser exists and runs in CI
   ([NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1)).

### <a id="b12-query-and-export-v1"></a>**[B-12 v1]** `lcart analyze`, `lcart query`, export

Acceptance criteria: those of [FR-006](../specs/requirements.md#fr006-query-and-export-facts-v1),
plus:

1. `lcart analyze` warns when the database or output directory is inside a
   git work tree and not ignored ([security T2](../security.md)).
2. Determinism test: export with 1 and 4 worker threads is identical
   ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).

### <a id="b13-module-map-v1"></a>**[B-13 v1]** `lcart map`

Acceptance criteria: those of [FR-007](../specs/requirements.md#fr007-module-map-without-llm-v1),
with a golden file for the `two_modules` fixture.

### <a id="b14-libmodbus-baseline-v1"></a>**[B-14 v1]** libmodbus baseline map and timing

Acceptance criteria:

1. CI job (no network after dependency installation) runs B-05, `lcart
   analyze` and `lcart map` on libmodbus with the module configuration from
   the [libmodbus plan](../targets/libmodbus.md#module-configuration).
2. The generated map is committed under `docs/targets/` and the job fails if
   regenerating it changes it.
3. The functions listed in the libmodbus plan (M1 step 3) are present with
   the expected files; calls from `core` to backend implementations appear
   as indirect.
4. Analysis time is printed and recorded as the NFR-005 baseline.

### <a id="b15-requirement-test-traceability-v2"></a>**[B-15 v2]** Traceability gate and matrix

Implements [ADR-0010](../adr/0010-traceability-matrix.md).

Acceptance criteria:

1. `scripts/check-doc-anchors.py --trace <ctest-junit.xml>` lists each
   FR/NFR anchor with the tests citing it (`Verifies: <anchor>`) and their
   results from the CTest JUnit output.
2. CI fails if a requirement of a milestone marked done has no citing test,
   a citing test failed, or a test cites an anchor that does not exist (for
   example an old version).
3. CI writes `docs/plan/traceability.md` (generated, in nav, marked "do not
   edit") and `build/traceability.csv`; the docs build uses the generated
   page.
4. The script's own tests cover each failure case.

### <a id="b16-resumable-analysis-v1"></a>**[B-16 v1]** Resumable analysis with progress

Acceptance criteria: those of [NFR-005](../specs/requirements.md#nfr005-performance-budget-v2)
parts 2–5, plus:

1. A generator script creates the ≥ 200-TU fixture on the fly (not
   committed).
2. Resume refuses to continue if the compilation database, tool version or
   schema version changed since the interrupted run, and says why.
3. SIGINT and SIGTERM end the run cleanly: completed TUs are committed to
   the database, the summary is printed, and the exit status says
   "interrupted".

### <a id="b17-msvc-code-spike-v1"></a>**[B-17 v1]** MSVC code under Clang: spike and docs

Acceptance criteria: those of [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
plus:

1. The exact Clang 20 flags for MSVC-targeted code and how LibTooling
   handles `clang-cl`-style arguments are recorded in ADR-0009's TODO
   (resolved) or a superseding ADR.
2. A how-to page ("Analysing an MSVC code base under WSL2") lists the steps
   tried, including getting a compilation database for an MSBuild solution,
   with what worked and what did not.
