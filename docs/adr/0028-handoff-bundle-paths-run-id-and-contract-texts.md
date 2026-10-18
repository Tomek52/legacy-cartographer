# 0028. Handoff bundle: paths, run ID, command line and the texts of the contract

- **Status:** Accepted (decided by delegation)
- **Date:** 2026-10-18
- **Requirements:** [FR-008](../specs/requirements.md#fr008-generate-documentation-v2),
  [NFR-002](../specs/requirements.md#nfr002-no-source-egress-by-default-v2),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-30](../plan/backlog.md#b30-facts-only-handoff-bundle-v1),
  [handoff contract](../specs/handoff-contract.md)
- **Supersedes / superseded by:** — (fills in the rule for bundles that
  [ADR-0016](0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)
  left open)

## Context

B-30 implements `lcart handoff` without `--include-source` (B-31). The
[handoff contract](../specs/handoff-contract.md) gives the layout and the
prompt, but leaves five things open:

1. **Paths of files outside the analysis root.** The fact database stores
   them absolute: Clang's resource directory, `/usr/include`, a home
   directory. The facts export names such a file by the spelling of the
   first directive that includes it (ADR-0016), which is machine-independent
   for `<stddef.h>` but not for `#include "/home/alice/lib/x.h"`. A bundle
   may leave the machine with the agent (security T1), so it must not carry
   a user name or home directory; ADR-0016 left bundles their own rule.
2. **Where the binary gets the prompt, the output contract and the claim
   schema.** FR-008 criterion 1 says the bundle's `PROMPT.md` matches the
   prompt on the contract page; B-30 asks for a test that the page and the
   tool cannot drift.
3. **What the run ID hashes.** The layout says "SHA-256(facts export +
   options)". The export can be computed from any `FactQuery`; SHA-256 has
   so far been LLVM's (`llvm/Support/SHA256.h`), which only `analysis` may
   include (ADR-0002). The fact database has no project name; `lcart.toml`
   has one, but `handoff` does not read `lcart.toml`.
4. **How the JSON is written.** NFR-004: byte-identical bundles for
   identical inputs.
5. **The command line**: `lcart handoff [--db] [--out] [--module NAME]...`
   (B-30) with what `--out` names, what happens when the bundle exists, and
   what `--include-source` does before B-31.

## Options considered

Paths outside the root:

1. **The export's names as they are.** Simple and consistent with the
   export; an absolute spelling leaks the directory, user name included.
2. **Only the file name for every file outside the root.** Never leaks;
   `sys/types.h` and `types.h` become indistinguishable, and a reader loses
   what the directive said.
3. **The export's names, but an absolute spelling replaced by a fixed
   prefix and the file name** (`<outside-root>/x.h`), before names are
   handed out, so ADR-0016's `#2` suffixes keep two such files apart. Keeps
   what a reader recognises for `<stddef.h>`; a relative spelling that
   climbs out of the root (`../../home/alice/x.h`) stays as written.

Texts from the pages:

1. **Copies in the source**, checked by a test against the pages. Two
   places to edit; the test catches drift only when it runs.
2. **The pages read at run time.** The binary would need the docs tree
   next to it.
3. **Extracted at configure time by CMake** into a generated header; the
   page is a configure dependency, so editing it rebuilds `lcart`. One
   source of truth; a page that no longer has the expected blocks fails the
   configure step.

SHA-256:

1. **LLVM's**, through a function exported by `analysis`. `handoff` would
   link Clang and LLVM for one hash.
2. **A new dependency** (OpenSSL, a small library). One more package for
   about a hundred lines of code.
3. **Our own implementation** in `common` (FIPS 180-4), tested with the
   standard's examples and against hashes LLVM wrote into the golden facts
   export.

## Decision

<a id="adr0028-paths-outside-root-v1"></a>**Paths outside the root in a
bundle.** A file outside the analysis root is named as in the facts export
([ADR-0016](0016-facts-export-and-command-line.md#adr0016-paths-outside-root-v1)),
with one change made before the names are handed out: an include directive
spelled with an absolute path (starting with `/` or `\`, or a drive letter
and `:` followed by `/` or `\`) counts as spelled `<outside-root>/` followed
by its last path component (after the last `/` or `\`). The order, the
`#2`, `#3`, ... suffixes and the fallback to the file name are those of
ADR-0016, applied to the changed spellings. The `spelled` value of such a
directive in `facts/includes.json` is changed the same way, or, if the
directive resolves to a file under the root, shown as `<root>/` followed by
that file's root-relative path. No other value in a bundle holds a path
outside the root: diagnostics and translation unit paths are not part of a
bundle, and the analysis root itself is never written. Option 3.

<a id="adr0028-texts-from-the-pages-v1"></a>**Texts from the pages.** At
configure time, CMake reads `docs/specs/handoff-contract.md` and
`docs/specs/claim-schema.md`, extracts the prompt's `markdown` code block,
the "Agent output" section and the claim schema's `json` code block (found
by their anchor ID prefixes, so a version bump needs no change), and
writes them as raw string literals into a generated header; both pages are
configure dependencies. A missing block fails the configure step. `lcart`
fills in the prompt's values and its facts-only paragraph at run time; a
unit test extracts the same blocks from the pages on its own and compares
them with a bundle (option 3).

<a id="adr0028-run-id-v1"></a>**Run ID.** The first 12 hexadecimal digits
(lower case) of the SHA-256 of the facts export of ADR-0016 (as amended,
format 2) followed by the compact JSON
`{"contract_version":1,"options":{"include_source":false,"modules":[...]}}`
and `\n`, where `modules` is the selection sorted by name (empty: all). The
contract version and options are those `MANIFEST.json` records. The project
name and the tool version are not hashed: equal facts and options give one
run ID. SHA-256 is our own implementation in `common` (option 3).

<a id="adr0028-project-name-v1"></a>**Project name.** The last path
component of the analysis root stored in the fact database (`META`
`analysis_root`), or `project` if it has none. Control characters and
bytes that are not valid UTF-8 become U+FFFD wherever the name is written,
so the name cannot add a line to `PROMPT.md`.

<a id="adr0028-json-format-v1"></a>**JSON format.** `MANIFEST.json` and
`facts/*.json` are written with nlohmann_json: keys sorted byte-wise,
two-space indentation, UTF-8 copied as it is, a byte that is not valid
UTF-8 replaced by U+FFFD, `\n` at the end. Arrays are sorted as the
[handoff contract](../specs/handoff-contract.md#claim-handoff-facts-files-v1)
states. `claim-schema.json` is the page's block, byte for byte.

<a id="adr0028-command-line-v1"></a>**Command line.** `lcart handoff
[--db FILE] [--out DIR] [--module NAME]...`, with ADR-0016's option syntax.
`--out` is lcart's output directory (default `lcart-out`): the bundle goes
to `DIR/handoff/<run-id>/`, and the prompt tells the agent to write into
`DIR/agent/<run-id>/`, given relative to the bundle directory
(`../../agent/<run-id>/`), so no absolute path enters the prompt. `--module`
may be given several times, each name once; an unknown module exits with 3
(not found). An existing bundle directory is replaced only if it holds
nothing but the files a bundle consists of; anything else there is refused
(exit 1), so lcart never deletes a file it did not write. The new bundle
is written to a hidden directory next to it and renamed into place. The
T2 warning of ADR-0016 covers the bundle directory. Until B-31,
`--include-source` is rejected as a usage error (exit 2) saying it is not
available yet.

## Consequences

- The golden bundle of `two_modules` is the same on every machine with
  Clang 20, like the golden export. It holds the tool version
  (`facts/project.json`) and file hashes, so a version bump or a fixture
  edit changes it.
- A relative spelling that climbs out of the root can still name a user
  directory (`../../home/alice/x.h`); the bundle shows what the code wrote.
  Revisit if users analyse code that includes from their home directory by
  relative paths.
- Facts export diagnostics may hold a path outside the root inside a
  message (ADR-0016); the run ID hashes the export, so such a run ID can
  differ between machines although the bundle files do not.
- A project whose `lcart.toml` names it differently from its root directory
  gets the directory name in the bundle (open question).
- Two SHA-256 implementations exist (LLVM's in `analysis`, ours in
  `common`); the golden export test pins that they agree on file hashes.
- Changing the prompt or the agent output on the page changes `lcart` at
  the next build; the contract version in `handoff.hpp` must then be
  bumped by hand (the page says so).
  B-30 moves the prompt and agent output anchors to v2 yet
  keeps contract version 1: no bundle was written before B-30, so v1 is
  defined by the texts as they stand at this decision.
