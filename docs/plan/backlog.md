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

Acceptance criteria: those of [NFR-005](../specs/requirements.md#nfr005-performance-budget-v2)
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
target under an exclusive `flock`, opens it without following a symbolic
link (a new file with `O_EXCL`, an existing one checked to be the regular
file at that name) and keeps it when interrupted. Resume compares `META`
(`tool_version`, `analysis_root`, `compilation_database_sha256`,
`configuration_sha256`; the schema version is checked on open) and names
each difference. SIGINT and SIGTERM end the run with exit status 5 and the
summary; progress goes to standard error at least every 30 s. Tests:
`tests/analysis/parallel_analysis_test.cpp` (1 and 4 workers on every
fixture and on a project whose facts depend on unit order, interrupt,
resume, refusals, progress), the `LcartAnalyzeRun` and `LcartResume`
suites of `tests/cli/lcart_cli_test.cpp` (SIGINT and SIGTERM on the
running binary, then `--resume`; the 200-TU generated project with 1 and
4 workers, export and map compared byte for byte; lock, symbolic link,
leftover journal) and the command-line and fact-store tests.
`scripts/gen-many-tus.py` generates the fixture (criterion 1);
`scripts/measure-analysis.py` measures it. Measured with a Release build
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

Throughput with 4 workers is 2.30× that of one on 200 TUs and 2.58× on
800 TUs; on 200 TUs the run lasts under a second, and earlier runs of the
same build gave 2.30× to 2.49×. The exports of 1 and 4 workers were
identical in every run. Peak memory grows by 3 % (200 to 400 TUs) and 6 %
(800 to 1600 TUs) with twice the TUs, far from in proportion (NFR-005
criterion 5); the source of that small growth was not investigated. Whether
NFR-005's ≥ 2.5× target stands for the 200-TU fixture is an
[open question](../open-questions.md#open-technical-questions).

### <a id="b17-msvc-code-spike-v1"></a>**[B-17 v1]** MSVC code under Clang: spike and docs

Acceptance criteria: those of [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
plus:

1. The exact Clang 20 flags for MSVC-targeted code and how LibTooling
   handles `clang-cl`-style arguments are recorded in ADR-0009's TODO
   (resolved) or a superseding ADR.
2. A how-to page ("Analysing an MSVC code base under WSL2") lists the steps
   tried, including getting a compilation database for an MSBuild solution,
   with what worked and what did not.
