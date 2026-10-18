# 0016. Facts export, command line and the git exposure warning

- **Status:** Accepted; option syntax amended by [ADR-0017](0017-module-map-format-and-command-line.md#adr0017-command-line-v1) (a lone `-` value for `--output`); which file `query export --output` replaces amended by [ADR-0018](0018-facts-export-replaces-only-exports.md#adr0018-replace-only-exports-v1); exit status 5, `--jobs`, `--resume` and the partial database of `lcart analyze` amended by [ADR-0019](0019-parallel-resumable-analysis.md); the `may_call` record kind and `format_version` 2 of the export amended by [ADR-0026](0026-may-call-edges-from-function-pointer-tables.md#adr0026-queries-and-export-v1); exit status 6 and status 2 for an invalid claims file amended by [ADR-0030](0030-verification-report-and-lcart-verify.md#adr0030-exit-statuses-v1)
- **Date:** 2026-10-04
- **Requirements:** [FR-006](../specs/requirements.md#fr006-query-and-export-facts-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-12](../plan/backlog.md#b12-query-and-export-v2),
  [security T2](../security.md)
- **Supersedes / superseded by:** —

## Context

B-12 adds `lcart analyze` and `lcart query`, the first commands that do
work. Four things they need are not yet decided anywhere:

1. **The export format.** FR-006 asks for "all facts as JSON Lines", and a
   golden file compared byte for byte; NFR-004 asks for identical bytes for
   identical inputs, with no timestamps. The fact database's row ids follow
   insertion order, which B-16's parallel analysis must keep stable but
   which is an implementation detail; a reader of the export should not
   depend on them.
2. **Paths outside the analysis root.** File facts store them absolute
   ([file facts](../architecture/data-model.md#arch-data-model-file-facts-v1)):
   Clang's resource directory (`/usr/lib/llvm-20/lib/clang/20/include/stddef.h`),
   `/usr/include`, a home directory. A golden file with such paths fails
   on every other machine, and the content of system headers (so their
   hash) differs between distributions. Translation unit and diagnostic
   paths were stored as Clang reported them, absolute as well, and
   `DIAGNOSTIC.file_id` was never set
   ([open questions](../open-questions.md#open-technical-questions)).
3. **The command line.** FR-006 criterion 2 needs a distinct exit status for
   an ambiguous function name. Security T2 says outputs default to
   `lcart-out/`; this repository ignores `lcart-out/` and `*.lcart.db`. The
   owner decided on a small hand-written parser in `src/cli`, no new
   dependency.
4. **The T2 warning** "inside a git work tree and not ignored". The
   obvious check, `git check-ignore`, runs git in a repository that may be
   the analysed one, which is untrusted: its `.git/config` can name
   programs that git runs, such as `core.fsmonitor`, a hook command run
   by commands that read the index (git-config(1)), and a user who
   analyses a hostile checkout should not run code from it because they
   ran `lcart`. Which keys a given git command honours depends on the
   command and the git version.

## Options considered

Export format:

1. **One JSON document** with arrays per kind. Easy to read whole; a
   one-line change in the facts rewrites a large diff, and big code bases
   need the whole document in memory to parse it.
2. **JSON Lines, one record per fact, a fixed order, references by
   stable keys** (path, USR, module name). Diffs show the facts that
   changed; any line can be read on its own.
3. **JSON Lines with row ids.** Simplest to write; ids change whenever
   insertion order changes, so exports of equal facts would differ.

Paths outside the root:

1. **Absolute, as stored.** Machine-specific: no golden file.
2. **Absolute with a known prefix stripped** (resource directory,
   `/usr/include`). Needs a list of prefixes per platform and still leaks
   any other path, such as a home directory.
3. **Relative to the include directory that found the file** (the
   `RelativePath` argument of `PPCallbacks::InclusionDirective`,
   `clang/Lex/PPCallbacks.h`, LLVM 20). Needs a new column and a schema
   change for a value that header documents as "equal to FileName [the
   spelling] except for framework includes".
4. **The spelling of the first directive that includes the file.** No
   schema change, independent of the machine, and what a reader recognises
   (`stddef.h`, `sys/types.h`). Two different files spelled alike need a
   suffix to stay distinct.

Git exposure check:

1. **Run `git check-ignore`** with hardening (`-c core.fsmonitor=false`,
   empty `GIT_CONFIG_*`). Exact git semantics, including the global
   excludes file; depends on knowing every configuration key that can run a
   program for this command, now and in future git versions, and on a
   `git` binary being installed.
2. **Read the ignore files without git**, with a subset of gitignore
   semantics, and answer "not ignored" whenever in doubt. Runs nothing;
   may warn although a pattern it does not understand, or the user's global
   excludes file, ignores the output.
3. **Warn whenever the output is inside a work tree.** Simplest; a warning
   that is always shown is soon ignored.

## Decision

<a id="adr0016-export-format-v1"></a>**Export format.** `lcart query
export` writes JSON Lines (UTF-8, `\n` after every line, no spaces, keys in
the order below, every key always present, `null` for "none"). Strings
are JSON-escaped (`\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`, other
controls as `\u00XX`); a byte that is not part of valid UTF-8 becomes
U+FFFD, so any analysed name gives valid JSON. No row ids, timestamps,
absolute paths or tool version appear. Files are named by their export
path (below), functions by their stored USR
([ADR-0012](0012-function-identity-for-internal-linkage.md)), modules by
name. Records come in this order of kinds, each kind sorted by the listed
fields (text compared byte-wise, numbers numerically, `null` first):

| `record` | Keys | Sorted by |
|---|---|---|
| `header` (one, first) | `format` (`"lcart-facts"`), `format_version`, `translation_units_total`, `translation_units_parsed` | — |
| `module` | `name` | name |
| `file` | `path`, `external`, `header`, `module`, `sha256` (`null` if external) | files under the root first, then path |
| `function` | `usr`, `name`, `qualified_name`, `signature`, `file`, `line_begin`, `line_end`, `definition`, `linkage` | file, line_begin, usr |
| `include` | `file` (includer), `line`, `spelled`, `system`, `target` (`null` if unresolved), `target_external` | file, line, spelled, target |
| `call` | `caller`, `file`, `line`, `column`, `kind`, `callee` (`null` if indirect), `via` | file, line, column, caller, kind, callee, via |
| `module_dependency` | `from`, `to`, `via`, `file`, `line`, `column` (`null` for an include) | from, to, include before call, file, line, column |
| `diagnostic` | `translation_unit`, `file`, `line`, `severity`, `message` | translation_unit, file, line, severity, message |

`format_version` is `facts::export_format_version`, now 1; any change to
kinds, keys, their meaning or the order increments it. Records equal in
every sort field are equal lines (two calls written by one macro use), so
the order is total.

<a id="adr0016-paths-outside-root-v1"></a>**Paths outside the root.** In
the export, a file outside the analysis root is named by the spelling of
the first directive that includes it (by includer path, then line, then
spelling): `<stddef.h>` gives `stddef.h`. If an earlier external file
already has that name, `#2`, `#3`, ... is appended. "Earlier" orders the
external files by their first directive, then by the sorted list of all
directives naming them, then by content hash, never by row id: one
directive can reach two files when translation units use different `-I`,
and B-16 may record them in either order. Two such files with the same
directives and the same content are ordered by absolute path, so only that
case can name them differently on another machine.
`"external": true` (and `target_external` on includes) keeps these names
apart from root-relative paths, and their hash is left out. A diagnostic
or translation unit path outside the root that is no recorded file is
`null`. Diagnostic messages are Clang's text with the analysis root (as
given and canonical) and its `/` removed, so `no such file or directory:
'/home/me/proj/gone.c'` is stored as `... 'gone.c'`; a path outside the
root inside a message stays as Clang wrote it. In the database,
translation unit and diagnostic paths are now
spelled like file paths (root-relative, else absolute), and a diagnostic in
a recorded file gets its `file_id`; that changes column meaning, so the
schema version becomes 2. Handoff bundles (M2) decide their own rule.

<a id="adr0016-command-line-v1"></a>**Command line.** Subcommands and
options (`--name value` or `--name=value`; a value starting with `-`
needs `=`; `--` ends options; an unknown
or repeated option is an error):

- `lcart analyze [--config FILE] [--compilation-database FILE|DIR]
  [--root DIR] [--db FILE] [--force]`. lcart.toml is `--config`, else
  `./lcart.toml` if it exists, else none. The command line wins over
  lcart.toml; paths in lcart.toml are relative to its directory. Defaults:
  root = the directory of lcart.toml (else the working directory),
  compilation database = `<root>/compile_commands.json` (a directory gets
  `compile_commands.json` appended), database =
  `lcart-out/facts.lcart.db` in the working directory. An existing
  database is replaced only with `--force`, and only if it is an lcart
  fact database (SQLite header and a `meta` table with `schema_version`):
  `--db main.c --force` is refused. The new database is written to a
  hidden file next to the target and renamed over it when the analysis
  succeeds, so a failed run leaves the previous database in place (B-16
  adds `--resume`).
- `lcart query callers|callees <function>`, `includes|included-by <file>`,
  `module-deps [<module>]`, `export [--output FILE]`, each with
  `[--db FILE]`. `<function>` is a stored USR or a name; `<file>` a stored
  path. Results go to standard output, one per line, tab-separated:
  `file:line:column` and the caller (callers, direct calls only); site,
  `direct` or `indirect`, and the callee's name or the `via` member (`-`
  if none) (callees); `file:line`, the directive as written and the target
  path or `(unresolved)` (includes); `file:line` and the directive
  (included-by); `from` and `to` (module-deps). An ambiguous name prints
  its candidates (`file:line`, name, USR) to standard output. Messages and
  warnings go to standard error, prefixed `lcart:`.

<a id="adr0016-exit-status-v1"></a>**Exit statuses:** 0 success; 1
failure (no translation unit parsed, a file or database that cannot be
read or written, lcart.toml among them, an existing database without
`--force`, an existing file that is not a fact database); 2 invalid
command line or lcart.toml content; 3 the function, file or module named is not in
the database; 4 ambiguous function name. A partial analysis exits 0
([FR-001](../specs/requirements.md#fr001-ingest-compilation-database-v1)).
Values are never renumbered; B-16's "interrupted" gets a new one.

<a id="adr0016-git-exposure-check-v1"></a>**Git exposure warning without
git.** `lcart analyze` (for the database) and `lcart query export
--output` (for the file) warn on standard error, and go on, when the
output is inside a git work tree and that work tree's ignore files do
not exclude it. No git command is run. The work tree is the nearest
directory at or above the output that has a `.git` entry (directory or
file). The rules read are `.git/info/exclude` (when `.git` is a directory)
and every `.gitignore` from the work tree root down to the output, lowest
precedence first, last match wins, and nothing below an excluded directory
is re-included (gitignore(5)). Patterns are translated to the path globs
of [ADR-0014](0014-path-globs-in-the-configuration.md): `!` negates, a
trailing `/` matches directories only, a pattern with another `/` is
anchored at its file's directory, one without matches at any depth
(`**/` in front), and a trailing `/**` becomes `/*/**` (in git it matches
what is inside the directory, not the directory). A UTF-8 byte order mark
at the start of an ignore file is skipped, as git does. A pattern with
`\`, `[^`, a POSIX class (`[:`), a non-ASCII byte, or that ADR-0014 rejects
is skipped; if it negates, or an ignore file is unreadable, not a regular file
(git does not follow a symbolic link there either, since
2.32: its release notes) or over 1 MiB, the
output counts as not ignored. The user's global excludes file and the
index (an output that is already tracked) are not read.

## Consequences

- The golden export of the `two_modules` fixture is the same on every
  machine with Clang 20, so it can be committed and compared in CI.
  Clang's spelling of signatures and USRs is part of it: a Clang upgrade
  may change the golden file.
- Consumers of the export never see row ids, so B-16 may change insertion
  order without changing the export; the 1-vs-4-thread comparison there
  relies on this.
- The export reads all facts into memory before writing; fine for libmodbus
  and the fixtures, to be revisited (streaming per kind) if large code
  bases need it.
- The warning can be a false alarm when only the global excludes file or a
  skipped pattern ignores the output. It is meant never to stay silent
  because of a rule it did not understand, but the translation to ADR-0014
  globs is our reading of gitignore(5): review of B-12 found three rules it
  got wrong in the silent direction (a trailing `/**`, POSIX classes, a byte
  order mark before a negation), each now a test checked against
  `git check-ignore` (git 2.43). Another such difference would be a missed
  warning, not a crash. Revisit if users report noise, for example by
  reading `core.excludesFile` from the user's own git configuration.
- Fact databases from before B-12 (schema 1) are refused; re-run the
  analysis, as for any schema change in v1.
