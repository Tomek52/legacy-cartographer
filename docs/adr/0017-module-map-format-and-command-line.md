# 0017. Module map: command line, content and escaping

- **Status:** Accepted
- **Date:** 2026-10-04
- **Requirements:** [FR-007](../specs/requirements.md#fr007-module-map-without-llm-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-13](../plan/backlog.md#b13-module-map-v1),
  [security T2](../security.md)
- **Supersedes / superseded by:** — (amends the option syntax of
  [ADR-0016](0016-facts-export-and-command-line.md#adr0016-command-line-v1):
  a lone `-` is a value for `--output`, below)

## Context

B-13 adds `lcart map`. FR-007 asks for a Markdown document with the module
list (files, function counts) and a Mermaid graph of module dependencies,
from the fact database only, identical bytes on every run, and rendering
with MkDocs + Mermaid (checked for libmodbus in
[B-14](../plan/backlog.md#b14-libmodbus-baseline-v1)). Open before this
ADR:

1. **The command line**: options, default output, whether an existing
   file at the output is replaced, and how to ask for standard output.
   ADR-0016's syntax says a value starting with `-` needs `=`, so
   `--output -` would be a usage error. ADR-0016 also holds that lcart
   replaces only its own files (`--force` refuses a file that is not a fact
   database); a map written over the database, or over a source file given
   by mistake, would destroy it.
2. **What the map shows** beyond FR-007's minimum: how strong each
   dependency is, files that belong to no module, files outside the root,
   coverage, and whether "function count" counts declarations.
3. **Untrusted names.** Module names come from `lcart.toml`, paths from the
   analysed tree; both can hold any byte. A `"` ends a Mermaid label, `#`
   starts a Mermaid entity code, `<` and `&` become HTML in Mermaid's
   labels, a backtick starts a Markdown code span or Mermaid's Markdown
   string, `|` splits a Markdown table cell, a line break ends a list item
   or a Mermaid statement. A name must not change the document's structure.

**Decided by the owner** for B-13:

- the command `lcart map [--db FILE] [--output FILE]`, with the default
  output `lcart-out/map.md` and `--output -` for standard output;
- edge labels that count the include and call sites behind each module
  dependency;
- files under the root without a module listed under `(unassigned)`;
- external files left out;
- one coverage line (translation units parsed out of the total).

**Chosen in the implementation, for the owner to approve:** which existing
files the map may replace, the lone `-` value for `--output`, counting
function definitions only, the order and the generated Mermaid ids, and the
escaping rules. Each is marked "(implementation)" below.

## Options considered

Standard output:

1. **`--output=-` only**, as ADR-0016's syntax implies. No parser change;
   `--output -`, the usual spelling, would be a usage error.
2. **Accept a lone `-` as the next argument's value** for every option.
   A lone `-` is never an option, so this cannot swallow a forgotten value;
   but `analyze --db -` would then create a database named `-`.
3. **Accept a lone `-` only after `--output`**, where it means standard
   output. Every other option keeps ADR-0016's rule.

Replacing an existing file at `--output`:

1. **Replace anything** (derived output). Simple; `--output
   lcart-out/facts.lcart.db` destroys the database being read, and a typo
   can replace a source file.
2. **Replace only a module map**, recognised by its first line, and never
   the database being read. Follows ADR-0016's "lcart replaces only its
   own files"; a hand-made file that starts with `# Module map` can still
   be replaced.
3. **Never replace without `--force`.** Safe; regenerating a map, the
   normal case, would always need the switch.

Escaping in Mermaid:

1. **Module names as node ids.** Readable source; breaks on any name that
   is not a Mermaid identifier, and on names such as `end`.
2. **Generated ids with quoted, escaped labels.** Ids are always valid;
   only the label needs escaping.

Names in Markdown:

1. **Backslash escapes** of Markdown punctuation. Which characters need
   escaping differs between CommonMark and Python-Markdown, and inside table
   cells `|` needs its own rule.
2. **Code spans in list items.** Nothing inside a code span is Markdown or
   HTML; list items have no cell separator; only backticks and line breaks
   in the name need handling.

## Decision

<a id="adr0017-command-line-v1"></a>**Command line.** `lcart map [--db
FILE] [--output FILE]`, with ADR-0016's option syntax and exit statuses.
The database defaults to `lcart-out/facts.lcart.db`, the output to
`lcart-out/map.md`, both relative to the working directory. `--output -`
(or `--output=-`) writes the map to standard output. (implementation) Only
after `--output` is a lone `-` taken as the next argument's value, and for
`--output` (`map` and `query export`) it means standard output; every other
option given `-` as its next argument is a missing value, as in ADR-0016.
A missing database, or a file that is not a readable fact database, exits
1 and writes nothing; an invalid command line exits 2. The parent
directory of the output is created if missing, as `lcart analyze` does for
the database.

<a id="adr0017-replace-only-maps-v1"></a>**Which file is replaced**
(implementation). lcart replaces only its own files, as in ADR-0016. The
map is written to a hidden file next to the output and renamed over it,
so a failed run leaves an earlier map as it was, and:

- the output must not be the database being read: the same lexically
  normal absolute path, or the same file (`std::filesystem::equivalent`)
  when it exists, so a link or another spelling is caught too;
- an existing output is replaced without `--force` only if it is a
  regular file whose first line is exactly `# Module map` (the first line
  of every map). Any other file, an empty one, a directory or a symbolic
  link at the output is refused;
- each refusal exits 1 and leaves the file unchanged. `--output -` writes
  no file and is not checked.

On success, standard output names the file (`module map in <path>`). The
security T2 warning of
[ADR-0016](0016-facts-export-and-command-line.md#adr0016-git-exposure-check-v1)
applies to the map file (not to standard output).

<a id="adr0017-map-content-v1"></a>**Content**, in this order:

1. `# Module map` and one sentence saying it was generated by `lcart map`
   from the fact database, without an LLM.
2. One coverage line: `Translation units parsed: P of T.`
3. `## Module dependencies`: a sentence on how to read the graph, then a
   ` ```mermaid ` block with `flowchart LR`, one node per module, and one
   edge per pair of modules with at least one `MODULE_DEPENDENCY` row. The
   edge label counts the rows by `via`: `1 include`, `2 includes`, `1 call`,
   joined by `, ` (include first), a zero part left out. Include rows are
   include directives, call rows direct call sites
   ([FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1)). With
   no modules, `No modules.` replaces the sentence and the block.
4. `## Modules`: one list item per module, `` - `name`: N files, M function
   definitions ``, then, if any file under the root has no module,
   `- (unassigned): ...`. Then one section per module, headed
   `` ### Module `name` ``, and `### (unassigned)` last, each listing its files as
   `` - `path`: K function definitions `` (`No files.` if none).
   `(unassigned)` is plain text, never a code span, so a module named
   `(unassigned)` stays distinct. The schema (version 2) gives every file
   under the root a module, so the section appears only for a `FactQuery`
   that does not; it is kept so that no file is ever silently dropped.
5. Files outside the analysis root (external files, such as system
   headers) are left out: they belong to no module and their paths are
   machine-specific.

(implementation) Function counts count **definitions** (`FUNCTION.is_definition`), in the
file of the definition: a function declared in a header and defined in a
`.c` file counts once, for the `.c` file. Counts use singular and plural
(`1 file`, `0 files`).

<a id="adr0017-deterministic-order-v1"></a>**Order and stability**
(implementation). Modules are sorted by name, files by path, edges by source then target
module name, all compared byte-wise. Node ids are `m0`, `m1`, ... in that
module order. The map holds no timestamp, absolute path, row id or tool
version, and ends with exactly one newline, so the same facts give the same
bytes in every run and on every machine
([NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1)).
Inserting the same facts in another order gives the same map. Adding,
removing or renaming a module renumbers the ids of the modules after it;
the map is regenerated as a whole, not edited.

<a id="adr0017-escaping-v1"></a>**Escaping** (implementation). First, in every name and
path, each byte that is not part of valid UTF-8 (the rule of
[ADR-0016](0016-facts-export-and-command-line.md#adr0016-export-format-v1))
and each control character (U+0000 to U+001F, U+007F) becomes U+FFFD. Then:

- **Mermaid labels** are written as `mN["label"]`. Letters `A-Z a-z`,
  digits, space and `_ . / -` are copied; every other character, ASCII or
  not, becomes Mermaid's entity code `#<decimal code point>;` (Mermaid
  flowchart syntax, "Entity codes to escape characters"; TODO: verify with
  the Mermaid version MkDocs Material loads, [open
  questions](../open-questions.md#not-verified-yet-todo-verify)) (`"` →
  `#34;`, `#` → `#35;`, `;` → `#59;`, `<` → `#60;`, `` ` `` → `#96;`,
  `|` → `#124;`, `é` → `#233;`). The label is therefore plain ASCII with
  no character that can end the label, start an entity or HTML, or start
  a Markdown string. Edge labels are generated text and need no escaping.
- **Markdown**: names and paths appear only as code spans, in list items
  and headings, never in tables. The fence is one backtick longer than the
  longest run of backticks in the text; a text that starts or ends with a
  backtick (or is empty) is padded with one space on each side, which
  CommonMark and Python-Markdown strip. ``a`b`` is written as ``` ``a`b`` ```,
  `` `x` `` as ``` `` `x` `` ```.

## Consequences

- The `two_modules` map is a golden file
  (`tests/golden/two_modules.map.md`), written by hand and compared byte
  for byte by a unit test and by the CLI test of a real analysis.
- A module named `(root)` shows as `#40;root#41;` in the Mermaid source;
  the rendered graph shows `(root)`. Readable source was traded for one
  rule that is safe for every name.
- Leading and trailing spaces of a name are not visible in a code span.
- Mermaid's parser is not run here: that the map renders is checked in CI
  for libmodbus by B-14. If Mermaid stops decoding `#NNN;` in quoted labels,
  the escaping must change and the golden files with it.
- `query export --output -` now writes to standard output; before, it was a
  usage error, and `--output=-` wrote a file named `-`. `query export
  --output` still replaces any file; applying the rule above to it is a
  follow-up ([open questions](../open-questions.md#open-technical-questions)).
- Regenerating a map over a file a user edited by hand replaces it, as
  long as its first line is still `# Module map`.
- A large code base gives one long list per module; revisit (for example a
  table of modules, or one page per module) when B-14 shows the libmodbus
  map.
