# 0014. Path globs in the configuration

- **Status:** Accepted
- **Date:** 2026-10-04
- **Requirements:** [FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1),
  [NFR-010](../specs/requirements.md#nfr010-untrusted-input-v1)
- **Supersedes / superseded by:** —

## Context

`lcart.toml` ([ADR-0008](0008-configuration-file-format.md)) maps files to
modules with "ordered path-glob rules" (FR-005) and, since the
[B-08 result](../plan/backlog.md#b08-file-and-include-facts-v1), excludes
translation units with the same glob syntax. FR-005 and ADR-0008 do not say
what a glob means. The common dialects differ in ways that change which
module a file lands in:

- shell (`fnmatch` without flags): `*` also crosses `/`;
- gitignore: a pattern without `/` matches a base name at any depth;
- Python `glob` / `pathlib`, Bash `globstar`: `*` stays in one directory and
  `**` spans directories.

The paths matched are root-relative with `/` separators
([file facts](../architecture/data-model.md#arch-data-model-file-facts-v1)),
the same on every machine. The patterns come from a file the user writes,
but a configuration can come with a code base, so matching must not be
exploitable (NFR-010): no exponential backtracking, no crash on malformed
patterns. LLVM's `GlobPattern` would bring LLVM into the configuration
parser, and its `*` "does not exclude path separators"
(`llvm/Support/GlobPattern.h`, LLVM 20).

## Options considered

1. **Shell `fnmatch`**: familiar from the command line; `src/*` would take
   every file below `src/`, so a rule cannot say "this directory only".
2. **gitignore**: familiar to every user of git; base-name matching and
   negation make the first-match order of FR-005 hard to read.
3. **Segment globs with `**`** (Python, Bash `globstar`): `*` within a
   directory, `**` across directories, anchored at the root. Explicit and
   order-friendly; slightly more to type for "any depth" (`**/*.h`).

## Decision

<a id="adr0014-path-glob-syntax-v1"></a>A **path glob** in `lcart.toml`
matches the whole root-relative path, case-sensitively:

| Pattern | Matches |
|---|---|
| `*` | any run of characters within one path segment (also none, also a leading `.`) |
| `?` | one character within a segment |
| `[abc]`, `[a-z]`, `[!a-z]` | one character of (or not of) the set; a `]` right after `[` or `[!` is a member |
| `**` as a whole segment | zero or more whole segments: `**/*.h`, `src/**/test.c`, `tests/**` |
| anything else | itself; there is no escaping (a literal `*` is `[*]`) |

A pattern is an error if it could never match a root-relative path or is
malformed: empty, starting with `/`, containing `\`, an empty, `.` or `..`
segment, an unclosed `[`, or `**` inside a segment (`a**`). The error names
the file, line and column. Matching costs at most the product of pattern
and path length.

<a id="adr0014-exclude-selects-translation-units-v1"></a>`[project]
exclude` is a list of path globs; a **translation unit** whose main file is
under the root and matches one is not analysed and not counted in the
coverage. Exclusion selects translation units, not files: a header that
matches is still recorded when an analysed translation unit includes it.

## Consequences

- Rules read the same as the paths in the fact database and in reports; a
  rule for one directory (`src/*`) cannot take files below it by accident.
- The matcher is our code (`src/config/path_glob.hpp`), without Clang, so
  the configuration parser and its fuzz target stay small.
- Users who expect gitignore behaviour must write `**/` for "any
  directory"; the error for `a**` points at the difference.
- Revisit if users need negation (`!pattern`) or case-insensitive matching
  for code bases from case-insensitive file systems (B-17).
