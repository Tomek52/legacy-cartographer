# Backlog (M0–M2)

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
| [B-04](#b04-include-boundary-check-v1) | ✅ Include boundary check for the core | M0 | NFR-001 | B-02 | 2 h |
| [B-05](#b05-libmodbus-compilation-database-v1) | ✅ Compilation database for libmodbus | M1 | FR-001 | B-01 | 3 h |
| [B-06](#b06-sqlite-fact-store-v1) | ✅ SQLite fact store, schema v1 | M1 | FR-002–FR-004, NFR-004 | – | 6 h |
| [B-07](#b07-clang-libtooling-spike-v2) | ✅ Clang LibTooling spike and analysis driver | M1 | FR-001 | B-02 | 6 h |
| [B-08](#b08-file-and-include-facts-v1) | ✅ File and include facts | M1 | FR-002 | B-06, B-07 | 4 h |
| [B-09](#b09-function-facts-v1) | ✅ Function facts | M1 | FR-003 | B-08 | 4 h |
| [B-10](#b10-call-edges-v1) | ✅ Direct and indirect call edges | M1 | FR-004 | B-09 | 5 h |
| [B-11](#b11-module-rules-v2) | ✅ Module rules and module dependencies | M1 | FR-005 | B-08 | 4 h |
| [B-12](#b12-query-and-export-v2) | ✅ `lcart analyze` / `lcart query` / export | M1 | FR-006, NFR-004 | B-10, B-11 | 5 h |
| [B-13](#b13-module-map-v1) | ✅ `lcart map` | M1 | FR-007 | B-12 | 3 h |
| [B-14](#b14-libmodbus-baseline-v1) | ✅ libmodbus baseline map and timing | M1 | NFR-001, NFR-005 | B-05, B-13 | 3 h |
| [B-15](#b15-requirement-test-traceability-v2) | ✅ Traceability gate and matrix | M1 | NFR-012 | B-03 | 4 h |
| [B-16](#b16-resumable-analysis-v2) | ✅ Resumable analysis with progress | M1 | NFR-005, NFR-004 | B-12 | 5 h |
| [B-17](#b17-msvc-code-spike-v2) | ✅ MSVC code under Clang: spike and docs | M1 | NFR-013 | B-10 | 4 h |
| [B-18](#b18-second-signal-of-either-kind-v1) | ✅ ADR-0020: a second signal of either kind ends the analysis | M1 | NFR-005 | B-16 | 2 h |
| [B-19](#b19-throughput-on-800-units-v1) | ✅ NFR-005 throughput measured on 800 TUs | M1 | NFR-005 | B-16 | 1 h |
| [B-20](#b20-threadsanitizer-preset-v1) | ✅ ThreadSanitizer preset and CI job | M1 | NFR-004, NFR-007 | B-16 | 3 h |
| [B-21](#b21-exposure-warning-for-partial-database-v1) | ✅ Exposure warning for the partial database | M1 | NFR-005 | B-16 | 1 h |
| [B-22](#b22-mermaid-check-in-ci-v1) | ✅ CI check that committed module maps parse as Mermaid | M1 | FR-007 | B-14 | 2 h |
| [B-23](#b23-libmodbus-hand-check-v1) | ✅ Hand check of the libmodbus functions | M1 | FR-003, FR-004 | B-14 | 2 h |
| [B-24](#b24-clang-tidy-checks-headers-v1) | ✅ clang-tidy checks the project's headers | M1 | NFR-007 | B-02 | 1 h |
| [B-25](#b25-claim-parser-v1) | ✅ Claim parser and its fuzzer | M2 | FR-009, NFR-010 | – | 5 h |
| [B-26](#b26-function-pointer-tables-v1) | `may_call` edges from function-pointer tables | M2 | FR-015 | – | 8 h |
| [B-27](#b27-claim-verifier-v1) | Claim verifier | M2 | FR-010 | B-25, B-26 | 6 h |
| [B-28](#b28-verification-report-v1) | Verification report and `lcart verify` | M2 | FR-010, FR-011 | B-27, B-30 | 4 h |
| [B-29](#b29-labelled-claim-set-v1) | Labelled claim set and accuracy gate | M2 | NFR-009 | B-27 | 6 h |
| [B-30](#b30-facts-only-handoff-bundle-v1) | `lcart handoff`: facts-only bundle | M2 | FR-008, NFR-002 | B-26 | 6 h |
| [B-31](#b31-include-source-excerpts-v1) | `--include-source` excerpts | M2 | NFR-002, NFR-010 | B-30 | 3 h |
| [B-32](#b32-agent-permissions-in-bundle-v1) | Agent permission configuration in the bundle | M2 | NFR-010 | B-30 | 2 h |
| [B-33](#b33-ingest-agent-output-v1) | `lcart ingest`: output contract and annotated documents | M2 | FR-008, FR-011, NFR-010 | B-28, B-30 | 6 h |
| [B-34](#b34-mermaid-check-in-ingest-v1) | Mermaid check of agent diagrams | M2 | FR-008 | B-33 | 3 h |
| [B-35](#b35-no-raw-html-in-generated-docs-v1) | No raw HTML from the agent in published documents | M2 | NFR-010 | B-33 | 2 h |
| [B-36](#b36-libmodbus-handoff-end-to-end-v1) | libmodbus handoff end to end without network | M2 | NFR-002 | B-31, B-33 | 3 h |
| [B-37](#b37-real-agent-run-on-libmodbus-v1) | Real agent run on libmodbus, published | M2 | FR-008, NFR-009 | B-29, B-32, B-34, B-35, B-36 | 4 h |
| [B-38](#b38-resume-detects-edited-sources-v1) | `--resume` detects edited sources | M2 (carry-over) | NFR-004 | – | 5 h |
| [B-39](#b39-adrs-for-merged-decisions-v1) | ✅ ADRs for the decisions of #29, #30 and #31 | M2 (carry-over) | NFR-010, NFR-013 | – | 2 h |
| [B-40](#b40-msvc-delayed-template-parsing-v1) | MSVC delayed template parsing: decision | M2 (carry-over) | NFR-013 | – | 2 h |

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

Result (2026-10-04): `cmake/CheckIncludeBoundaries.cmake`, run by CTest as
`Architecture.IncludeBoundaries` (label `architecture`) on the source tree,
reads every `#include` and `#include_next` line under `src/` (all C and C++
source and header extensions) and reports each violation as
`path:line: includes header (rule)`. It checks all eight deterministic
modules of CLAUDE.md (the criterion's six plus `config` and `handoff`) and
treats LLVM headers (`llvm/`, `llvm-c/`) like Clang headers, since Clang's
interfaces expose LLVM types (`llvm::StringRef`). A header matches by any
path segment, so `../llm/x.hpp` is caught too, and directive lines inside
`#if 0` or a block comment are checked as well: a false alarm is cheaper
than a missed include. Not detected: an include through a macro
(`#include HEADER`), which `src/` does not use.
`Architecture.IncludeBoundariesSelfTest` runs the same script on
`tests/architecture/forbidden_includes/`, which has four forbidden includes
in different spellings and an allowed Clang include in `src/analysis`, and
passes only if the check fails and reports exactly those four, with their
lines. A root without sources fails rather than passing as clean. The check
is a CMake script, so it needs no tool beyond the build's own.

## M1

### <a id="b05-libmodbus-compilation-database-v1"></a>**[B-05 v1]** Compilation database for libmodbus

Acceptance criteria:

1. `scripts/libmodbus-compdb.sh` produces
   `build/targets/libmodbus/compile_commands.json` with entries for
   `modbus.c`, `modbus-data.c`, `modbus-rtu.c`, `modbus-tcp.c`.
2. After the script, `git -C targets/libmodbus status --porcelain` is empty
   (out-of-tree build; see the [libmodbus plan](../targets/libmodbus.md)).
3. The script documents its tool requirements (autotools, `bear`) and fails
   with a clear message if one is missing.

Result (2026-10-04): `scripts/libmodbus-compdb.sh` exports the pinned
commit with `git archive` to `build/targets/libmodbus/source/` and builds
it there under `bear` (`autogen.sh`, `./configure --enable-tests`, `make`),
because `autogen.sh` writes into the source directory and a plain
out-of-tree configure would still modify the submodule. The database has
14 entries (`src/modbus.c`, `modbus-data.c`, `modbus-rtu.c`,
`modbus-tcp.c` and 10 test programs) and names files under the copy, which
becomes the analysis root for B-14. The script itself checks criteria 1
and 2 (the four entries, an empty `git status --porcelain` of the
submodule) and fails otherwise; before building it lists every missing
tool with the Ubuntu install command (checked by hiding `bear`). CI runs
it in the `libmodbus compilation database` job (about 25 s locally;
`CC=clang-20`), keeping the build log on failure. `lcart analyze` on the
result parsed 14/14 translation units without diagnostics in under 2 s;
the map and timing are B-14.

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

Result (2026-10-04): a new `config` module (`src/config/`) parses
`lcart.toml` with toml++ 3.4.0, header-only and without exceptions, into
`ProjectConfig`; errors name file, line and column. The glob syntax, for
module rules and for `[project] exclude` (moved here from B-08), is
[ADR-0014](../adr/0014-path-globs-in-the-configuration.md). After the last
translation unit the analysis fills `MODULE_DEPENDENCY`
([data model](../architecture/data-model.md#arch-data-model-modules-v1)).
`fuzz/project_config_fuzzer.cpp` runs with the other fuzzers in CI
(`scripts/fuzz-smoke.sh`); within seconds it found two kinds of undefined
behaviour in toml++ 3.4.0 on malformed input, fixed upstream but not
released, and review found unbounded recursion on deeply dotted keys, so
`lcart.toml` is ASCII, small and shallow, and toml++'s assertions are off
([ADR-0015](../adr/0015-ascii-configuration-while-tomlplusplus-3-4.md)).
Conan Center's recipe has `tomlplusplus` 3.4.0
(`recipes/tomlplusplus/config.yml` in conan-center-index). Reading
`lcart.toml` from the command line is B-12.

### <a id="b12-query-and-export-v2"></a>**[B-12 v2]** `lcart analyze`, `lcart query`, export

Acceptance criteria: those of [FR-006](../specs/requirements.md#fr006-query-and-export-facts-v1),
plus:

1. `lcart analyze` warns when the database or output directory is inside a
   git work tree and not ignored ([security T2](../security.md)).
2. Determinism test: analysing the same input twice into fresh databases
   gives byte-identical exports
   ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
   The comparison of 1 and 4 worker threads moved to
   [B-16](#b16-resumable-analysis-v2), which brings parallel analysis
   ([B-07 result](#b07-clang-libtooling-spike-v2),
   [threading](../architecture/components.md#arch-threading-v1)).

Result (2026-10-04): `src/cli` is now a library (`lcart::cli`: parser,
commands, git exposure check) behind a thin `main.cpp`, so the parser is
unit-tested and `tests/cli/lcart_cli_test.cpp` runs the `lcart` binary on the
fixtures. The export format, the naming of files outside the root, the
command line, the exit statuses and the warning are
[ADR-0016](../adr/0016-facts-export-and-command-line.md) (Accepted). The
export (`src/facts/fact_export.hpp`) names files by root-relative path,
external ones by the spelling of their first directive, functions by
stored USR; the golden file `tests/golden/two_modules.facts.jsonl` was
written by hand from the fixture and matched on the first run. The T2
warning reads `.gitignore` files and `.git/info/exclude` itself and runs no
git, because git can run programs named in the analysed repository's
configuration. `FactQuery` gained whole-table and reverse queries
(`calls_to`, `included_by`, ...). Translation unit and diagnostic paths are
now stored like file paths and diagnostics get their `file_id`, which
closes that open question and bumps the schema version to 2.

### <a id="b13-module-map-v1"></a>**[B-13 v1]** `lcart map`

Acceptance criteria: those of [FR-007](../specs/requirements.md#fr007-module-map-without-llm-v1),
with a golden file for the `two_modules` fixture.

Result (2026-10-04): `lcart map [--db FILE] [--output FILE]` writes
`lcart-out/map.md` by default (`--output -`: standard output) through a
renamed hidden file. It replaces only an earlier map (first line
`# Module map`) and never the database it reads; the T2 warning covers the
map file. The map (`report::write_module_map`, the first code in
`src/report`) has a coverage line, a Mermaid `flowchart` whose edges count
the include and call sites behind each dependency, and the modules with
their files and function definition counts; external files are left out.
Command line, content, order and escaping are
[ADR-0017](../adr/0017-module-map-format-and-command-line.md).
Mermaid nodes get generated ids (`m0`, `m1`, ...) and labels in which every
character but letters, digits, space and `_ . / -` is an entity code;
Markdown shows names and paths only as code spans in list items, never in
tables, where `|` would split a cell. The golden file
`tests/golden/two_modules.map.md` was written by hand, matched on the
first run, and is checked by a unit test and against a real analysis by
the CLI test. A lone `-` is now a value for `--output` only, so
`query export --output -` writes to standard output too, which amends
ADR-0016's option syntax. `query export --output` still replaced any file;
the same protection there followed in
[ADR-0018](../adr/0018-facts-export-replaces-only-exports.md). Files under the
root without a module cannot occur with schema 2, so `(unassigned)` is
tested with a `FactQuery` double. FR-007 criterion 1 (renders with MkDocs +
Mermaid) is checked in CI on the libmodbus map by
[B-14](#b14-libmodbus-baseline-v1); `mkdocs build` does not parse Mermaid,
so a docs page with the fixture map would not check it.

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

Result (2026-10-04): `scripts/libmodbus-baseline.sh [LCART]` runs B-05's
script, `lcart analyze` with `scripts/libmodbus/lcart.toml` (the module
table of the libmodbus plan; the root is B-05's copy) and `lcart map` into
[docs/targets/libmodbus-map.md](../targets/libmodbus-map.md), prints the
analysis time and checks criterion 3: the nine functions in their files,
indirect calls from `_modbus_receive_msg` through `backend.recv` and
`backend.check_integrity`, and no direct call from `core` into `rtu` or
`tcp` (each check was seen failing on a wrong expectation). The CI job
`libmodbus baseline` (replacing B-05's job) builds `lcart` in Release from
Ubuntu packages, runs the script in a new network namespace (`sudo unshare
--net`, then `ip link set lo up`: bear talks to its collector over
127.0.0.1, so loopback must be up; nothing else is reachable) and fails if
the committed map changed; its first run passed, with the map unchanged. A
local run in such a namespace passed and could not reach the internet. `CC` defaults to `clang-20`, as in CI, so a
local run regenerates the same map. Baseline: 1.12 s for 14 translation units with a
Release build in that CI job (1.6 s with a debug build on the development
container), recorded in the [libmodbus plan](../targets/libmodbus.md); the 2× regression gate of
NFR-005 criterion 1 is left until CI has a stable reference. The map
renders: its Mermaid block, drawn once by hand in Chromium with Mermaid
11.17.2 (what `mermaid@11`, the tag MkDocs Material loads, resolved to on
2026-10-04), shows all five modules with `(root)` decoded from
`#40;root#41;`, which closes the ADR-0017 escaping question. FR-007
criterion 1 asks for that check in CI; `mkdocs build` does not parse
Mermaid, so it stays open (open questions).

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

Result (2026-10-04): `scripts/check-doc-anchors.py --trace JUNIT [--write]`
reads the FR/NFR headings, the milestones of the roadmap (their status and
`**Requirements:**` links, an en dash between two links being a range) and
every `Verifies:` citation under `tests/`, `scripts/`, `cmake/`, `src/`,
`fuzz/` and `.github/`. A citation in a test file's opening comment block
covers every `TEST` in the file, one right above a `TEST` covers that
test, one in `tests/CMakeLists.txt` covers the `add_test` calls of its
block; any other placement in a C++ test file is an error. The gate fails
on a cited anchor that does not exist (an old version, anywhere in the
repository), a citing test that failed or is missing from the results
(skipped is shown, not failed: one test cannot run as root), and a
requirement of a done milestone without a citing test, from M1 on (M0's
requirements are checked by CI jobs, NFR-012 criterion 2). Python tests
(`tests/docs`) run outside CTest and are listed as such. The `debug-system`
test preset writes `junit.xml`; its CI job runs the gate, regenerates the
committed [matrix](traceability.md) and fails if it differs, and keeps
`build/traceability.csv` as an artifact; `scripts/dod.sh` regenerates the
page. Eleven tests in `tests/docs` cover attribution, ranges and each
failure case; each gate condition, disabled in turn, fails at least one of
them. With M1 not yet done, the matrix shows FR-001 to FR-007 and NFR-004
cited and passing, NFR-001, NFR-005 and NFR-013 not yet cited by a test.

### <a id="b16-resumable-analysis-v2"></a>**[B-16 v2]** Resumable analysis with progress

Acceptance criteria: those of [NFR-005](../specs/requirements.md#nfr005-performance-budget-v3)
parts 2–5, plus:

1. A generator script creates the ≥ 200-TU fixture on the fly (not
   committed).
2. Resume refuses to continue if the compilation database, tool version or
   schema version changed since the interrupted run, and says why.
3. SIGINT and SIGTERM end the run cleanly: completed TUs are committed to
   the database, the summary is printed, and the exit status says
   "interrupted".
4. Determinism test: the export of an analysis with 1 and with 4 worker
   threads is identical
   ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1);
   moved here from B-12, which had no parallel analysis).

Result (2026-10-04): implemented as
[ADR-0019](../adr/0019-parallel-resumable-analysis.md) decides. Workers
(`TranslationUnitRecorder`, `src/analysis/src/translation_unit_recorder.hpp`)
turn each TU into a batch of events without database ids; the calling
thread is the only writer and replays the batches in TU order through the
collectors (`BatchWriter`), one transaction per TU together with its
`status` and its main file (`TRANSLATION_UNIT.file_id`, which closes that
open question). `lcart analyze` gained `--jobs N` (1 to 256, default one per
hardware thread) and `--resume`; it writes `.<name>.partial` next to the
target under an exclusive `flock` and keeps it when interrupted. Nothing is
created or written through a symbolic link there: a new file is created
with `O_EXCL`; an existing name is opened as it is (`fopen` follows a
link), then refused unless it is the regular file at that name, before
anything is written; SQLite reopens it with `SQLITE_OPEN_NOFOLLOW`, which
refuses a link in any component, so the directory of the database is
resolved first (a database in a directory reached through a link works).
Compilation database entries that name one file (through a link, for
example) are one TU, the first in sorted order. Resume compares `META`
(`tool_version`, `analysis_root`, `compilation_database_sha256`,
`configuration_sha256`; the schema version is checked on open) and names
each difference. SIGINT and SIGTERM end the run with exit status 5 and the
summary; a second signal of the same kind ends lcart at once. Progress
goes to standard error at least every 30 s, and each TU's warnings are
printed when it is committed, so a later failure does not lose them.
Tests:
`tests/analysis/parallel_analysis_test.cpp` (1 and 4 workers on every
fixture and on a project whose facts depend on unit order, interrupt,
resume, refusals, progress), the `LcartAnalyzeRun` and `LcartResume`
suites of `tests/cli/lcart_cli_test.cpp` (SIGINT and SIGTERM on the
running binary once its first TU is committed, on a generated project of
120 TUs, then `--resume`; a generated project of 40 TUs with 1 and 4
workers, export and map compared byte for byte; lock, symbolic link,
leftover journal) and the command-line and fact-store tests.
`scripts/gen-many-tus.py` generates the fixture (criterion 1); the
200-TU fixture of NFR-005 is exercised by `scripts/measure-analysis.py`,
outside CTest, which also compares the exports of 1 and 4 workers. Measured with a Release build
(`cmake --preset debug-system -B build/release-system
-DCMAKE_BUILD_TYPE=Release`, as the CI baseline job) on the 4-core
development container, fastest of 3 runs:

| Project | Workers | Wall time | TUs per minute | Peak RSS |
|---|---|---|---|---|
| 200 TUs | 1 | 2.01 s | 5957 | 91 MiB |
| 200 TUs | 4 | 0.87 s | 13718 | 98 MiB |
| 400 TUs | 4 | | | 101 MiB |
| 800 TUs | 1 | 9.46 s | 5074 | 97 MiB |
| 800 TUs | 4 | 3.67 s | 13071 | 105 MiB |
| 1600 TUs | 4 | | | 112 MiB |

NFR-005 criterion 2 (≥ 2.5× with 4 workers) is met on 800 TUs (2.58×) but
not on 200 TUs (2.30×; that run lasts under a second, and earlier runs of
the same build gave 2.30× to 2.49×). The target is left for the owner to
confirm or revise ([open question](../open-questions.md#open-technical-questions)).
The exports of 1 and 4 workers were identical in every run. Peak memory
grows by 3.1 % (200 to 400 TUs) and 6.7 % (800 to 1600 TUs) with twice the
TUs, far from in proportion (criterion 5). Two known causes of that growth:
`gen-many-tus.py` puts 2 × units / 8 prototypes in each `mod.h`, and
every TU includes two of them (its module's and its predecessor's), so
each TU's AST grows with the number of TUs; and the writer's state grows with the total facts.

### <a id="b17-msvc-code-spike-v2"></a>**[B-17 v2]** MSVC code under Clang: spike and docs

Acceptance criteria: criteria 1 and 2 of
[NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1), plus:

1. The exact Clang 20 flags for MSVC-targeted code, and how LibTooling
   handles `clang-cl`-style arguments in a compilation database, are
   verified on Linux with synthetic compilation databases (a `clang`
   driver entry with `--target=x86_64-pc-windows-msvc` and the MS flags,
   and a `clang-cl` entry) and recorded in an ADR that supersedes
   ADR-0009's TODO.
2. A how-to page ("Analysing an MSVC code base under WSL2") lists the steps
   tried, with what worked and what did not.
3. What cannot be tried without Windows (a compilation database from an
   MSBuild solution, real Windows SDK and MSVC headers) is marked
   `TODO: verify` on that page and has a row in the open questions;
   NFR-013 criterion 3 stays open until it is tried.

Version 2 (2026-10-09): the development container and CI have no Windows,
MSBuild or Windows SDK, so the MSBuild part of v1 is not a criterion of
this item any more; it stays a criterion of NFR-013.

Result (2026-10-09): [ADR-0021](../adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md)
records the spike (clang 20.1.2, LLVM source at `llvmorg-20.1.2`). The
target `x86_64-pc-windows-msvc` implies `-fms-extensions`,
`-fms-compatibility` and `-fms-compatibility-version=19.33`; clang-cl
commands (`clang-cl`, `cl`, `cl.exe`, `--driver-mode=cl`) parse as they
are. The fixture `tests/fixtures/msvc_constructs` and
`tests/analysis/msvc_code_test.cpp` verify NFR-013 criterion 1 with both
database styles, and criterion 2 only as path relativity (a root at
`<tmp>/mnt/c/...` on the Linux file system; a real drvfs mount is an open
question). Two fixes: the database is wrapped in
`inferTargetAndDriverMode()`, because LibTooling's dependency-file
adjuster dropped cl's `-MD`/`-MT` from `cl.exe` commands; and the
argument filter now also reads clang-cl's `/clang:` and `-clang:`
pass-through arguments, `-Wp,` lists and `-Xpreprocessor` (the last two
found in review; security T5; such plugins were not loaded before
either, since LibTooling never loads `-load` plugins). The how-to
[Analysing an MSVC code base under WSL2](../how-to/msvc-under-wsl2.md)
lists the steps tried. Left open: a database from MSBuild (NFR-013
criterion 3, a Windows machine), real Windows SDK and MSVC headers,
Windows paths in a database made on Windows, calls lost to delayed
template parsing, and Clang configuration files bypassing the filter
([open questions](../open-questions.md)).

Update (2026-10-11), on a Windows machine with Build Tools 2022 and WSL2:
the MSBuild part is closed outside this item, by
[ADR-0022](../adr/0022-msbuild-logger-for-compilation-databases.md). The
logger `tools/msbuild-compdb/CompDbLogger.cs` writes the database from an
MSBuild solution (sample: `tests/fixtures/msbuild_solution`);
`MsbuildDatabaseTest` in `tests/analysis/msvc_code_test.cpp` analyses the
database it wrote there, and `tools/msbuild-compdb/verify.ps1` checks the
logger on Windows. Tried since: a root on a real drvfs mount (the fact
database must be on the Linux file system), real Windows SDK and MSVC headers
through `/imsvc` and `/mnt/c`, and Windows paths (rewritten by the logger's
`map=`). NFR-013 criterion 3 is met. Open from the Windows run: the Win32
configuration fails in `lcart` (no LLVM targets), and the options other than
`/imsvc` for the headers were not tried ([open questions](../open-questions.md)).

## M1 closing items

Added 2026-10-09 from the open questions left by B-16 and the remaining
M1 Definition of Done items that need no decision from the owner
(publication and the ruleset on `main` do). The two other questions left
by B-16 (checking `inputs_sha256` on resume, a process per TU) need no
answer for M1 and are moved to M2 in the open questions.

M1 cannot be marked done after these items alone: NFR-013 criterion 3 (a
tested way to get a compilation database from MSBuild) needed a Windows
machine. It was tried on 2026-10-11 and is met by
[ADR-0022](../adr/0022-msbuild-logger-for-compilation-databases.md) (see the
update in B-17). The rest of M1's Definition of Done was in the
[roadmap](roadmap.md#m1-fact-database-v3): the repository was made public,
and the ruleset on `main` and the documentation site on Pages were set up,
on 2026-10-11. M1 is `done` from that date.

### <a id="b18-second-signal-of-either-kind-v1"></a>**[B-18 v1]** ADR-0020: a second signal of either kind ends the analysis

ADR-0019 is accepted and lags the code in two places (open questions):
the TU list is de-duplicated by fact path, and only a second signal of the
same kind ends lcart at once.

Acceptance criteria:

1. ADR-0020 amends [ADR-0019](../adr/0019-parallel-resumable-analysis.md#adr0019-interrupt-v1):
   the TU list behind the configuration hash, dispatch and totals is
   de-duplicated by fact path (the first entry in sorted order wins); and
   the first SIGINT or SIGTERM resets **both** to their default action, so
   any second signal of either kind ends lcart at once. ADR-0019's status
   line and the index say it is amended.
2. A test sends SIGINT and then SIGTERM (and the reverse) to a running
   `lcart analyze` and observes that the process ends by the second signal;
   `--resume` then finishes the analysis with an export identical to an
   uninterrupted run.
3. `lcart --help` describes the second signal; the two open-questions rows
   are removed.

Result (2026-10-09): [ADR-0020](../adr/0020-unit-list-and-second-signal.md)
records [one TU per fact path](../adr/0020-unit-list-and-second-signal.md#adr0020-tu-list-by-fact-path-v1)
(the code of B-16, unchanged) and
[a second signal of either kind](../adr/0020-unit-list-and-second-signal.md#adr0020-second-signal-of-either-kind-v1):
the handler in `src/cli/src/commands.cpp` now also sets SIGINT and SIGTERM
to `SIG_DFL` with `sigaction()`, and both are in its `sa_mask`. Tests:
`SigintThenSigtermEndsLcartAtOnceAndResumeFinishesIt` and
`SigtermThenSigintEndsLcartAtOnceAndResumeFinishesIt` in
`tests/cli/lcart_cli_test.cpp` hold the run by filling the socket of its
standard error, send the first signal, wait until `/proc/<pid>/status`
shows no handler for either signal, send the second, and expect lcart to
end by it; `--resume` with 4 workers then gives the export of an
uninterrupted run. `CommandLine.HelpSaysASecondSignalOfEitherKindEndsLcart`
checks the help. ADR-0020 notes the remaining window (a second signal
reaching another thread while the first handler runs).

### <a id="b19-throughput-on-800-units-v1"></a>**[B-19 v1]** NFR-005 throughput measured on 800 TUs

B-16 measured 2.30× with 4 workers on 200 generated TUs (a run of under a
second, dominated by start-up) and 2.58× on 800 TUs.

Acceptance criteria:

1. NFR-005 criterion 2 keeps the ≥ 2.5× target for 4 workers and measures
   it on a generated project of at least 800 TUs (version bump, referrers
   updated).
2. `scripts/measure-analysis.py` uses 800 TUs by default; the result table
   of a run on the development container is recorded in this item, and its
   ratio is ≥ 2.5×. If it is not, the item stops there and the target goes
   to the owner as a question (revising it is the owner's decision).
3. The open-questions row is removed.

Result (2026-10-10): NFR-005 v3 measures criterion 2 on 800 TUs, and
`scripts/measure-analysis.py` uses 800 by default. Measured with a Release
build (as in B-16) on the 4-core development container, fastest of 3 runs:

| Project | Workers | Wall time | TUs per minute | Peak RSS |
|---|---|---|---|---|
| 800 TUs | 1 | 13.75 s | 3491 | 93 MiB |
| 800 TUs | 4 | 5.15 s | 9319 | 101 MiB |
| 1600 TUs | 4 | | | 108 MiB |

The ratio is **2.67×** (target ≥ 2.5×); the exports of 1 and 4 workers are
identical, and peak memory grows by 7 % with twice the TUs. Both runs are
slower than B-16's on the same container (9.46 s and 3.67 s), so absolute
times vary between sessions; the ratio is what the criterion compares,
and it needs an otherwise idle machine (a run beside four other
compilers gave 1.1× to 2.25×). A repeat on the idle container (load
average 0.4 at the start) gave 2.61× (13.05 s and 5.00 s). This also answers the B-16 open question
and replaces the 200-TU fixture named in ADR-0019's consequences.

### <a id="b20-threadsanitizer-preset-v1"></a>**[B-20 v1]** ThreadSanitizer preset and CI job

The worker pool of ADR-0019 (threads, condition variables, dispatch
window) has never run under ThreadSanitizer.

Acceptance criteria:

1. A `tsan-system` configure, build and test preset (Clang 20,
   `-fsanitize=thread`, Ubuntu packages, no Conan) builds lcart and the
   tests.
2. Under it `parallel_analysis_test` and the `lcart analyze` command-line
   tests pass with no ThreadSanitizer report; any race found is fixed with
   a test, not suppressed. Tests left out of the preset are listed with the
   reason.
3. CI runs the preset in the `build` workflow; `scripts/dod.sh` runs it
   too, or documents why not.
4. The open-questions row is removed; the testing strategy names the
   preset.

Result (2026-10-09): `tsan-system` (`LCART_SANITIZERS=thread`) builds lcart
and all tests; its test preset sets `TSAN_OPTIONS=halt_on_error=1`, and the
command-line tests start the `lcart` of the same build, which inherits it.
The whole suite runs, no test is left out (234 tests, 50 to 70 s of test
time on 4 cores), with no suppressions file. TSan found no race in lcart's
worker pool, dispatch window or writer. It did report one race inside
LLVM's statistic registry (`llvm::TrackingStatistic::RegisterStatistic`,
reached from two workers parsing at once), which failed 8 command-line
tests in the one run measured: the registry's lock is an
`llvm::ManagedStatic` created by whichever worker comes first and published
through a `std::atomic` inside Ubuntu's uninstrumented libLLVM
(`llvm/Support/ManagedStatic.h`), which TSan cannot see. `ClangAnalyzer` now
calls `llvm::GetStatistics()` (`llvm/ADT/Statistic.h`) on the main thread
before starting the workers, so the registry exists before any worker does;
the existing command-line tests under this preset are its test (they fail
without the call). The CI job (matrix entry of `cpp-system`, same packages)
sets `vm.mmap_rnd_bits=28` first; `scripts/dod.sh` runs the preset by
default and stops early with that hint when the kernel uses more bits.

### <a id="b21-exposure-warning-for-partial-database-v1"></a>**[B-21 v1]** Exposure warning for the partial database

Acceptance criteria:

1. When `lcart analyze` writes `.<name>.partial` inside a git work tree and
   that file is not ignored, it warns as for the database
   ([security T2](../security.md)); likewise for its `-journal` file, which
   SQLite creates next to it.
2. A test with an ignore rule that covers the database but not the partial
   database shows the warning; a rule covering both shows none.
3. Security T2 names the partial database; the open-questions row is
   removed.

Result (2026-10-09): once the partial database is open, before parsing,
`lcart analyze` runs the T2 check on `.<name>.partial` and on its
`-journal` file and warns once for each one not ignored, in the wording of
the database warning; doubt still counts as not ignored. `LcartCliTest`
shows two warnings with only `facts.lcart.db` in `.gitignore` and none
with `*.lcart.db*`. [Security T2](../security.md) names both files; the
open-questions row is removed.

### <a id="b22-mermaid-check-in-ci-v1"></a>**[B-22 v1]** CI check that committed module maps parse as Mermaid

FR-007 criterion 1 was checked by hand once in B-14.

Acceptance criteria:

1. A script parses every Mermaid block of the committed libmodbus map
   ([libmodbus map](../targets/libmodbus-map.md)) and of the module map
   golden files with Mermaid itself, pinned by version and lock file, with
   no browser and no network beyond installing the pinned packages. This
   also answers the open question on parsing Mermaid without a browser
   (FR-008); if that turns out to be impossible, the item records why and
   stops, and the check stays manual.
2. It fails, naming the file and Mermaid's error, on a deliberately broken
   diagram (a test of the script).
3. CI runs it; the `TODO: verify` row for the CI Mermaid check is removed.

Result (2026-10-09): `scripts/check-mermaid.sh` runs
`tools/mermaid-check/check-mermaid.mjs`, which calls Mermaid's own
`mermaid.parse` on every ` ```mermaid ` block of the libmodbus map and of
`tests/golden/*.map.md`, and prints `file:line`, the Mermaid version and
Mermaid's error for each block that does not parse (exit 1). Parsing
without a browser works: Node.js 22 with jsdom as the only DOM. jsdom is
needed, tried without it: Mermaid sanitises labels with DOMPurify, which
binds to a `window` when Mermaid is loaded, and without one every flowchart
with a label fails with `DOMPurify.addHook is not a function`. Nothing is
laid out or rendered, and a run in a network namespace without a network
passes. The versions are pinned exactly in `package.json` and by
`package-lock.json`: Mermaid 11.17.2 (2026-08-25), the newest 11.x and what
`mermaid@11`, the tag MkDocs Material 9.7.7 loads from unpkg to draw the
site, resolves to today; 12.x (12.1.0, 2026-10-02) is newer but is not what
renders the docs. jsdom 29.1.1, because 30.x needs Node.js 22.22.2 or later. Material's
tag is not pinned, so the site can draw with a newer 11.x than the check
parses with; the pin is moved by hand. `npm ci --ignore-scripts` is the only
step that uses the network. The docs workflow sets up Node.js 22.22.0
(npm cache on the lock file), installs, runs the check, and runs
`tests/docs/test_check_mermaid.py` with `LCART_REQUIRE_MERMAID=1`; locally
the test is skipped without Node.js or the installed packages. A fixture
with a missing `|` after an edge label fails, naming the file and line and
Mermaid's `Parse error`; a fixture with every entity code of ADR-0017
passes. A parse is weaker than a render: a quoted label with an unescaped
`"` inside it parsed (the FR-008 open question records this). `npm audit`
reports two low-severity advisories in KaTeX, a Mermaid dependency for math
labels, which the maps do not use.

### <a id="b23-libmodbus-hand-check-v1"></a>**[B-23 v1]** Hand check of the libmodbus functions

The M1 Definition of Done asks for a hand check of the functions listed in
the [libmodbus plan](../targets/libmodbus.md) (M1, step 3).

Acceptance criteria:

1. For each listed function, its definition (file, lines), direct callees
   and indirect calls in the fact database are compared with the source of
   the pinned tag, read by hand.
2. The result is recorded on the libmodbus page: one row per function,
   matching or not; each mismatch is explained or becomes an open question
   (FR-015's function-pointer tables are out of scope).

Result (2026-10-09): all nine functions match the source of the pinned
tag: definitions, direct calls and indirect calls; the table is on the
[libmodbus page](../targets/libmodbus.md#m1-fact-database-and-module-map-no-llm).
No mismatch, so no new open question.

### <a id="b24-clang-tidy-checks-headers-v1"></a>**[B-24 v1]** clang-tidy checks the project's headers

Found while reviewing B-22 (2026-10-09): in a git worktree,
`scripts/run-clang-tidy.sh` failed on headers of `src/` that the same run
in the main clone, and CI, passed. clang-tidy 20 applied `.clang-tidy`'s
`HeaderFilterRegex` in the worktree but not in the main clone, where it
dropped every finding in the project's headers as "non-user code".

Acceptance criteria:

1. `scripts/run-clang-tidy.sh` passes the header filter on the command line,
   so findings in the project's headers are reported in every checkout; it
   fails if the filter differs from `.clang-tidy`'s.
2. Every finding that appears with it is fixed, or its check is excluded in
   `.clang-tidy` with a reason.
3. `scripts/run-clang-tidy.sh debug-system` passes locally and in CI.

Result (2026-10-09): the only findings in headers were 22 of
`readability-redundant-member-init`, all `{}` initializers of aggregate
members (`fact_model.hpp`, `analyzer.hpp`, `command_line.hpp`). They are
needed: without them GCC 14 (`-Wmissing-field-initializers`) and Clang 20
(`-Wmissing-designated-field-initializers`), both part of `-Wextra` and
errors here, warn for a designated initializer that leaves the member out
(checked on a two-line example and on `parallel_analysis_test.cpp`). The check is excluded with that reason. Why clang-tidy 20 ignored
the configured filter in the main clone was not found
([open question](../open-questions.md#open-technical-questions)); the
explicit option makes it irrelevant.

## M2

Added 2026-10-17 from the [M2 milestone](roadmap.md#m2-llm-verification-v2).
The order follows the dependencies: the verifier side (B-25 to B-29) needs
no agent and can be done first; the handoff side (B-30 to B-36) then feeds
it; B-37 is the run with a real agent that M2's Definition of Done asks for.
B-38 to B-40 (carry-over, outside M2's scope and Definition of Done)
answer open technical questions dated M2 that belong to the analysis
rather than to the handoff, and record decisions already merged. Each item that implements a stub removes its `TODO(M2)` marker.

### <a id="b25-claim-parser-v1"></a>**[B-25 v1]** Claim parser and its fuzzer

`claims::parse_claims` is a stub, and `fuzz/claim_json_fuzzer.cpp` feeds it
bytes that it ignores.

Acceptance criteria: those of [FR-009](../specs/requirements.md#fr009-structured-claims-v2),
plus:

1. `parse_claims` implements the [JSON Schema v1](../specs/claim-schema.md#claim-json-schema-v2)
   by hand with nlohmann_json (no JSON Schema library: one more dependency
   for one fixed schema), including `additionalProperties: false`, the
   `id` pattern, `maxItems` and every `maxLength`, and then the entity types
   per kind. A test per rule shows a violating input rejected.
2. Invalid input yields `schema_violation` with **every** problem found,
   each with its JSON pointer (`/claims/3/kind`); nothing is returned from
   it. Duplicate claim IDs are a problem; the claim schema page says so,
   with its anchor's version bumped.
3. Input over a size limit (a constant, e.g. 4 MiB) and nesting deeper than
   the schema allows are rejected before parsing into a DOM, so neither
   memory nor recursion depends on the input.
4. `claim_json_fuzzer` calls `parse_claims`, has a seed corpus (the schema
   example, valid and invalid files from the tests) and a dictionary, and
   runs in CI with the other fuzzers (`scripts/fuzz-smoke.sh`,
   [NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1)).

Result (2026-10-18): `src/claims/src/claim_parser.cpp` checks the schema in
two passes. A SAX pass of nlohmann_json, after the 4 MiB size check, finds
syntax errors and stops at the first object or array nested deeper than 4
levels; only then is the document built (`parse` without exceptions) and
validated rule by rule, then the entity types per kind and unique IDs. The
problems travel in `common::Error::message`, one line each as
`<JSON pointer>: <description>` (`""` for the whole document), so no new
error type was needed; property names from the input are escaped and cut to
64 bytes in them. `maxLength` counts characters, not bytes, as JSON Schema
does. `tests/claims/claim_parser_test.cpp` has a test per rule; its valid
and invalid files, one of them the schema page's example (a test keeps the
two equal), are the fuzzer's seed corpus, which `scripts/fuzz-smoke.sh` now
passes read-only next to a temporary working corpus. 60 s locally
(`asan-ubsan-system`, `scripts/dod.sh`): 277,555 runs, no finding.
Duplicate property names in one JSON object (nlohmann_json keeps the last)
are an [open question](../open-questions.md#open-technical-questions).

### <a id="b26-function-pointer-tables-v1"></a>**[B-26 v1]** `may_call` edges from function-pointer tables

The schema's `CALL.kind` already allows `may_call`
([data model](../architecture/data-model.md#arch-data-model-call-edges-v1)), but nothing records it.

Acceptance criteria: those of [FR-015](../specs/requirements.md#fr015-resolve-function-pointer-tables-v1),
plus:

1. The analysis records, for each struct field of function-pointer type, the
   functions assigned to it in static initializers, positional and
   designated (`.send = rtu_send`), across translation units; an indirect
   call through `p->field` (or `s.field`) of that struct type gets a
   `may_call` edge to each of them. Fixture: `fn_pointers` (its
   `rtu_backend` table, positional; a designated initializer is added to
   the fixture), expected edges written by hand
   ([fixture rules](../testing/strategy.md#testing-fixture-rules-v1)).
2. An indirect call that is not through such a field (a local function
   pointer, a parameter) gets no `may_call` edge.
3. On libmodbus v3.2.0, the call in `_modbus_receive_msg`
   (`src/modbus.c`) through `check_integrity` has `may_call` edges to
   `_modbus_rtu_check_integrity` and `_modbus_tcp_check_integrity` and no
   other; checked by the libmodbus baseline job.
4. `may_call` edges are a separate query in `FactQuery` and a separate kind
   in the facts export; the export's `format_version` goes to 2 and an ADR
   amends [ADR-0016](../adr/0016-facts-export-and-command-line.md).
   `lcart map` does not count them as module dependencies (an ADR says so if
   the map changes).
5. Determinism: the edges do not depend on the order of translation units
   (shuffled-order test, [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
6. The field assignments are stored per translation unit (resolution runs
   after the last one, like `MODULE_DEPENDENCY`), so `--resume` keeps
   working ([ADR-0019](../adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1));
   the fact database's `schema_version` is bumped and the
   [data model](../architecture/data-model.md#arch-data-model-call-edges-v1)
   updated.

### <a id="b27-claim-verifier-v1"></a>**[B-27 v1]** Claim verifier

Acceptance criteria: those of [FR-010](../specs/requirements.md#fr010-verify-claims-v1)
except criterion 3 (B-28), plus:

1. `verify::verify_claim` implements [entity resolution](../specs/claim-schema.md#claim-entity-resolution-v1),
   the [relation rules](../specs/claim-schema.md#claim-relation-rules-v1)
   and [negation](../specs/claim-schema.md#claim-negation-rule-v1) as
   written; every cell of the relation table and every resolution outcome
   has a test against a `FactQuery` fake, named after the cell.
2. `may_call` edges never confirm a `calls` claim; they make an absent
   direct call `unverified` / `indirect_calls_present`
   ([B-26](#b26-function-pointer-tables-v1)).
3. Every `confirmed` and `contradicted` verdict carries the fact IDs it
   rests on; for `contradicted` / `relation_absent`, the subject's
   definition (or file) is the evidence. `entity_not_found` has no fact to
   cite: its evidence is the coverage (all translation units parsed).
4. A coverage fact with failed translation units turns every
   `entity_not_found` into `unverified` / `coverage_incomplete` (test with
   the `broken_tu` fixture's database).
5. If a rule turns out ambiguous while implementing it, the claim schema is
   corrected first, with a version bump, in the same pull request.

### <a id="b28-verification-report-v1"></a>**[B-28 v1]** Verification report and `lcart verify`

Acceptance criteria: those of [FR-011](../specs/requirements.md#fr011-verification-report-v2)
except criterion 2 (B-33), and criterion 3 of
[FR-010](../specs/requirements.md#fr010-verify-claims-v1), plus:

1. `lcart verify <claims.json> [--db] [--out]` reads a hand-written claims
   file, verifies it against the fact database and writes
   `verification-report.md` and `verification-report.json`; exit status 0
   when no claim is contradicted, a new status otherwise, added to the list
   of [ADR-0016](../adr/0016-facts-export-and-command-line.md) by an
   amending ADR.
2. The JSON report has a versioned format (`format`, `format_version`) and
   is described in a new spec section; the Markdown report shows counts per
   status and per kind, then contradicted and unverified claims with reason
   and evidence (file and line, not row IDs only), then the coverage.
3. Both formats are byte-identical for identical input and match golden
   files for a fixture: the report has no generation time (NFR-004
   criterion 3 allows one only in a separate metadata block, and none is
   needed).
4. The handoff contract version (B-30) is in the report; `lcart verify`,
   which has no bundle, writes `null`.
5. The report's `agent` field is empty for `lcart verify` and filled by
   `--agent` in `lcart ingest` (B-33).

### <a id="b29-labelled-claim-set-v1"></a>**[B-29 v1]** Labelled claim set and accuracy gate

Acceptance criteria: those of [NFR-009](../specs/requirements.md#nfr009-verifier-accuracy-measured-v1),
plus:

1. `tests/claims/labelled/` holds at least 200 claims in the sources 1 to 3
   of the [testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1)
   (source 4 comes with [B-37](#b37-real-agent-run-on-libmodbus-v1)), with
   at least 30 per claim kind and at least 60 false ones. Each label states
   the source file and line it was read from at the pinned libmodbus commit
   or in the fixture; labels are written from the code, never from lcart's
   output. The implementing agent drafts them (assumption A12); the
   reviewer agent re-reads a random sample of at least 20 against the
   source, and any wrong label in the sample means the whole kind is
   checked again.
2. A data-driven test verifies each claim, prints the confusion matrix per
   kind and overall, and fails below the NFR-009 precision targets; it also
   reports recall of `contradicted` and the unverified rate. Fixture claims
   also run in ctest on every preset; the gate itself (both sets, one
   combined precision) runs in the `libmodbus baseline` job, the only
   place with a libmodbus fact database.
3. The result table is published on a docs page (the
   [testing strategy](../testing/strategy.md) links it) and regenerated by a
   script, not by hand.
4. A label that disagrees with the verifier is resolved by reading the code
   again: a wrong label is fixed with a note, a verifier bug is fixed with a
   test.
5. The [testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1)
   is rewritten for assumption A12 (labels drafted by an agent from the
   code, a sample checked by the reviewer agent, also for source 4), with
   its anchor bumped to `-v2` and its referrers checked.

### <a id="b30-facts-only-handoff-bundle-v1"></a>**[B-30 v1]** `lcart handoff`: facts-only bundle

Acceptance criteria: criterion 1 of [FR-008](../specs/requirements.md#fr008-generate-documentation-v2)
and criterion 2 of [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2)
(criterion 1 is [B-36](#b36-libmodbus-handoff-end-to-end-v1)), plus:

1. `lcart handoff [--db] [--out] [--module NAME]...` writes the
   [bundle layout](../specs/handoff-contract.md#claim-handoff-bundle-layout-v1)
   with the run ID as specified; a golden bundle for the `two_modules`
   fixture is committed and compared byte for byte.
2. `PROMPT.md` and `CONTRACT.md` are generated from the
   [handoff contract](../specs/handoff-contract.md) page: a test extracts
   the prompt's code block and the output contract from the page, fills in
   the values, and compares with the bundle, so the page and the tool
   cannot drift. `claim-schema.json` is likewise the schema block of the
   [claim schema](../specs/claim-schema.md#claim-json-schema-v2).
3. Files outside the analysis root are shown as in the facts export
   ([ADR-0016](../adr/0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)),
   except that a directive spelled with an absolute path shows only
   `<outside-root>/` and the file name (with ADR-0016's `#2`... suffixes);
   no absolute path, user name or home directory appears in the bundle
   (tests with a file from a temporary directory, included by a relative
   and by an absolute spelling). ADR-0016 leaves bundles their own rule;
   an ADR records this one, the handoff contract states it, and it answers
   the open question on paths outside the root.
4. A fixture whose comments and string literals contain a marker string
   produces a bundle without that string in any file.
5. The handoff contract gets a version field in `MANIFEST.json`
   (`contract_version: 1`); a change to the prompt or the output contract
   bumps it. `MANIFEST.json` also records the run ID and the options it was
   computed from.
6. `facts/calls.json` lists `may_call` edges ([B-26](#b26-function-pointer-tables-v1))
   apart from direct and indirect calls, and prompt rule 5 says they are
   candidates, not calls.
7. The [bundle layout](../specs/handoff-contract.md#claim-handoff-bundle-layout-v1)
   and the prompt are updated for criteria 3, 5 and 6, with their anchors'
   versions bumped.

### <a id="b31-include-source-excerpts-v1"></a>**[B-31 v1]** `--include-source` excerpts

Acceptance criteria: criterion 3 of [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2),
plus:

1. `lcart handoff --include-source` writes `excerpts/<path>.txt` with the
   bodies of the functions in the selected modules, with line numbers, and
   the *with-source* paragraph of the prompt; `MANIFEST.json` lists every
   file and line range.
2. The switch is read from the command line only
   ([ADR-0008](../adr/0008-configuration-file-format.md#adr0008-no-consent-in-config-v1)):
   an `include_source` key in `lcart.toml` is an error, and no environment
   variable is read (tests for both).
3. Only files under the analysis root are excerpted; a function defined
   outside it is listed in `MANIFEST.json` as skipped.
4. The bundle states at the top of `PROMPT.md` and in `MANIFEST.json` that
   it contains source code.
5. Each excerpt is delimited as data, between fixed begin and end lines
   naming its file and line range, and a test shows that an excerpt whose
   text contains the end line cannot close the block early
   ([NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1) criterion
   2, [T3](../security.md#security-prompt-injection-v2)).

### <a id="b32-agent-permissions-in-bundle-v1"></a>**[B-32 v1]** Agent permission configuration in the bundle

Acceptance criteria:

1. The bundle contains `.claude/settings.json` with permission rules that
   allow reading the bundle and writing the output directory only, and deny
   shell and web tools ([running the agent safely](../specs/handoff-contract.md#claim-agent-permissions-v1),
   [T3a](../security.md#security-agent-tools-v1)). The rule syntax is taken
   from the Claude Code documentation current at the time, with the version
   and link recorded in the contract; the bundle layout lists the file
   (anchor version bumped) and the TODO: verify row is closed.
2. A manual check with Claude Code in a bundle shows a denied shell command
   and a denied write outside the output directory; the result and Claude
   Code version are recorded in this item. (Not automated: it needs an
   agent and an account.)
3. The how-to for running an agent on a bundle is a docs page (start in the
   bundle directory, review `MANIFEST.json`, the permission file, the
   `lcart ingest` command).

### <a id="b33-ingest-agent-output-v1"></a>**[B-33 v1]** `lcart ingest`: output contract and annotated documents

Acceptance criteria: criteria 2 to 4 of [FR-008](../specs/requirements.md#fr008-generate-documentation-v2)
and criterion 2 of [FR-011](../specs/requirements.md#fr011-verification-report-v2),
plus:

1. `lcart ingest <dir> --bundle <bundle-dir> --agent "<name, model>" [--db] [--out]` checks the
   [output contract](../specs/handoff-contract.md#claim-agent-output-v1),
   lists every problem and ingests nothing if there is one. Recorded agent
   outputs, one valid and one per kind of problem, are committed test data.
2. The output directory is untrusted: symbolic links, files over a size
   limit, non-UTF-8 text and paths outside the directory are problems, not
   followed or read ([NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1)).
3. Annotated documents are written to the output directory: each marker
   `[c-NNNN]` becomes a visible verdict next to the sentence; contradicted
   sentences are marked in the text; each document starts with a notice
   that it is LLM-generated, names the agent and model, and links to the
   report. Golden files for the valid recorded output.
4. The marker scanner has a fuzz target in CI (arbitrary Markdown and
   claims), like the claim parser.
5. `lcart ingest` reads the run ID and options from the bundle's
   `MANIFEST.json` (B-30), recomputes the ID from the current fact database
   and those options, and rejects the output if it differs (the facts
   changed since the handoff) or if the output directory's name is not that
   ID. The handoff contract says so.

### <a id="b34-mermaid-check-in-ingest-v1"></a>**[B-34 v1]** Mermaid check of agent diagrams

Criterion 5 of [FR-008](../specs/requirements.md#fr008-generate-documentation-v2)
has a TODO: verify; B-22 showed that Mermaid parses under Node.js with jsdom.

Acceptance criteria:

1. An ADR decides how `lcart ingest` checks Mermaid blocks: a built-in
   subset check of flowchart syntax, Mermaid itself under Node.js
   (`tools/mermaid-check`), or both. The ADR keeps the report identical on
   every machine ([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1):
   an optional checker is chosen by a switch and named in the report, never
   by whether Node.js happens to be installed) and says how starting a
   program fits the deterministic core
   ([ADR-0006](../adr/0006-external-agent-handoff-for-llm-work.md#adr0006-handoff-is-deterministic-core-v1)).
   FR-008 criterion 5 is rewritten to the decision, with a version bump.
2. A broken diagram in a recorded agent output is a finding in the report,
   with document and line, not a breach of the output contract: the rest
   is ingested.
3. The open-questions row on Mermaid in `lcart ingest` is closed.

### <a id="b35-no-raw-html-in-generated-docs-v1"></a>**[B-35 v1]** No raw HTML from the agent in published documents

[T3](../security.md#security-prompt-injection-v2) has a TODO: verify on
the MkDocs and Material settings that keep raw HTML from running.
While this item was written (2026-10-17), `mkdocs build --strict` with
the project's `validation` settings stopped finding the anchors that follow
an inline code span holding a `script` start tag on the same page (B-36 to
B-40 were reported missing, though the built HTML has them); with the tag
spelled out in words the check passed. So even escaped HTML in documents
is worth a test.

Acceptance criteria:

1. `lcart ingest` escapes or rejects raw HTML in agent documents (outside
   code blocks), so an injected `script` element or an `img` element with
   an `onerror` attribute is shown as text; tests with both.
2. The site build that publishes agent documents is checked to render the
   escaped text (a built page contains no `script` element from the test data).
3. The security page is updated with what was checked, and the
   open-questions row is closed.

### <a id="b36-libmodbus-handoff-end-to-end-v1"></a>**[B-36 v1]** libmodbus handoff end to end without network

Acceptance criteria: criterion 1 of [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2)
for `handoff` and `ingest`, plus:

1. The `libmodbus baseline` CI job also runs `lcart handoff` (facts-only and
   `--include-source`) and `lcart ingest` of a recorded agent output for
   libmodbus under `unshare --net`.
2. The test executables of `claims`, `verify`, `report`, `handoff` and the
   M2 command-line tests also run under `unshare --net` in that job, so
   "all M2 tests pass with network blocked" (M2's Definition of Done) is
   checked, not assumed.
3. The recorded output is written by hand for the test, small, with true,
   false and undecidable claims, and its expected report is committed.
4. The facts-only libmodbus bundle contains no comment text of libmodbus
   (a check with strings taken from its comments).

### <a id="b37-real-agent-run-on-libmodbus-v1"></a>**[B-37 v1]** Real agent run on libmodbus, published

M2's last Definition of Done items. The agent is Claude Code, started in
the bundle directory in a session separate from the one implementing M2,
with no access to this repository or to the libmodbus source (assumption
A13); the model is the one that session reports.

Acceptance criteria:

1. A real coding agent runs the facts-only libmodbus bundle with the
   bundle's permission configuration; its output is ingested and the
   documents and report are published on the documentation site, with the
   agent and model named.
2. Its claims are labelled by hand and added to the labelled set (source 4
   of the [testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1));
   the accuracy gate still passes.
3. Failure modes seen (invented names, wrong direction, broken contract,
   instructions followed from the data) are recorded on the
   [libmodbus page](../targets/libmodbus.md), with counts.
4. M2 is marked `done` in the roadmap when B-25 to B-37 are done; B-38 to
   B-40 are carried over from the open questions and do not hold it.

### <a id="b38-resume-detects-edited-sources-v1"></a>**[B-38 v1]** `--resume` detects edited sources

From the open questions: a resumed run mixes old and new code
([ADR-0019](../adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1)).

Acceptance criteria:

`translation_unit.inputs_sha256` exists in the schema but nothing writes
it, and no list of the files each unit read is stored.

1. Each finished translation unit records the files it read (the files of
   its include facts and the unit itself) with their `FILE.sha256`, in the
   same transaction as its facts.
2. `lcart analyze --resume` hashes those files again; if any differ it
   stops with an error that names them and says to run without `--resume`.
3. A test edits a header between an interrupted run and `--resume` and
   expects the error naming it; without edits, the resumed export equals an
   uninterrupted run (existing tests stay green).
4. An ADR amends ADR-0019, which decided that edits are not detected
   ([resume](../adr/0019-parallel-resumable-analysis.md#adr0019-resume-v1));
   the schema version and data model are updated; the open-questions row is
   removed.

### <a id="b39-adrs-for-merged-decisions-v1"></a>**[B-39 v1]** ADRs for the decisions of #29, #30 and #31

The owner delegated three decisions on 2026-10-11, merged as #29 (Clang
configuration files ignored), #30 (the run lock on a separate file, which
also made the default database work on a WSL2 drive mount) and #31 (LLVM
targets registered). Code and docs cite all three as ADR-0023, which does
not exist (`docs/security.md`, `docs/how-to/msvc-under-wsl2.md`,
`src/cli/src/commands.cpp`, `src/analysis/src/clang_analyzer.cpp`).

Acceptance criteria:

1. Three ADRs, ADR-0023 to ADR-0025, record the decisions as merged
   (status Accepted, decided by delegation, with the date), in the index.
2. Every citation names the right one of them; the Decided rows of the
   open questions link to them.
3. A regression test exists for each decision (or the item names the
   existing one).

Result (2026-10-18): [ADR-0023](../adr/0023-clang-configuration-files-ignored.md)
records #29 (Clang configuration files ignored),
[ADR-0024](../adr/0024-run-lock-on-a-separate-file.md) #30 (the run lock on
`.<name>.partial.lock`) and [ADR-0025](../adr/0025-llvm-targets-registered.md)
#31 (LLVM targets registered), each Accepted, decided by delegation on
2026-10-11, with anchored decision rules, and in the ADR index and the
site navigation. The citations in `docs/security.md` and the argument filter
and recorder stay ADR-0023; those in `src/cli/src/commands.cpp`,
`tests/cli/lcart_cli_test.cpp` and three in the how-to became ADR-0024;
`src/analysis/src/clang_analyzer.cpp`, `tests/analysis/msvc_code_test.cpp`
and the how-to's Win32 paragraph became ADR-0025. The Decided rows of the
open questions link to the ADRs. Each decision already had regression
tests, named in its ADR; their `Verifies:` comments now also cite the ADR
anchors, so no test was added. ADR-0024 amends the run-lock file of
ADR-0019 and ADR-0025 the "no LLVM targets" statement of ADR-0021 (their
status lines and index rows say so): with targets registered, a target
prefix in a program name (`x86_64-w64-mingw32-clang`) is now taken as the
target, which no test covers; whether to keep it is a new open question.

### <a id="b40-msvc-delayed-template-parsing-v1"></a>**[B-40 v1]** MSVC delayed template parsing: decision

From the open questions: with an MSVC target before C++20, Clang delays
template parsing, and calls in never-instantiated templates are not
recorded (B-17 spike).

Acceptance criteria:

1. An ADR decides whether lcart appends `-fno-delayed-template-parsing` to
   MSVC commands, always, never or by a configuration option, with the
   cost measured on a new fixture `msvc_templates` (a never-instantiated
   function template with calls, and a template only MSVC's lenient
   parsing accepts): facts gained, units that stop parsing.
   `msvc_constructs` stays as it is, so its line and column expectations do
   not move.
2. Tests on the new fixture show the decided behaviour; the how-to says
   what users see.
3. The open-questions row is removed.
