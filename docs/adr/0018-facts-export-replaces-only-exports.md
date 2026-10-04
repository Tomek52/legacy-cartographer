# 0018. Facts export replaces only exports

- **Status:** Proposed
- **Date:** 2026-10-04
- **Requirements:** [FR-006](../specs/requirements.md#fr006-query-and-export-facts-v1),
  [security T2](../security.md)
- **Supersedes / superseded by:** — (amends the export part of
  [ADR-0016](0016-facts-export-and-command-line.md#adr0016-command-line-v1))

## Context

`lcart query export --output FILE` ([ADR-0016](0016-facts-export-and-command-line.md#adr0016-command-line-v1))
truncates and rewrites `FILE` whatever it is: a source file, a
document, or the fact database the command is reading (which leaves an
empty database and a failed export). ADR-0016 says lcart replaces only
its own files, but gives the rule only for the database of `lcart
analyze`. `lcart map` got such a rule in
[ADR-0017](0017-module-map-format-and-command-line.md#adr0017-replace-only-maps-v1);
the export, older, did not. A failed export also leaves a truncated
file behind.

## Options considered

1. **Keep replacing any file.** Simplest; one mistyped path destroys
   a file, the database being read included.
2. **Require `--force` for any existing file**, as `lcart analyze` does
   for the database. Re-exporting is the normal use, so every run would
   need `--force`, and `--force` would again replace any file.
3. **The map rule, with the export's header as the signature.** An
   earlier export is replaced without a flag; anything else is refused.
   Consistent with `lcart map`; the header record is fixed by ADR-0016.

## Decision

<a id="adr0018-replace-only-exports-v1"></a>**Which file is replaced**
(implementation). `lcart query export --output FILE` follows the rule of
[ADR-0017](0017-module-map-format-and-command-line.md#adr0017-replace-only-maps-v1)
with the export as the file kind:

- the output must not be the database being read: the same lexically
  normal absolute path, or the same file (`std::filesystem::equivalent`)
  when it exists;
- an existing output is replaced only if it is a regular file whose first
  line starts with `{"record":"header","format":"lcart-facts",` (the
  start of the header record of every export, ADR-0016; the rest of the
  line holds the format version and coverage, which vary). Any other
  file, an empty one, a directory or a symbolic link is refused;
- an output that names a directory (ends in `/`) is refused before
  anything is created;
- each refusal exits 1 and leaves the file unchanged; `--output -` writes
  no file and is not checked;
- the export is written to a hidden file next to the output
  (`.<name>.partial-<pid>`) and renamed over it, so a failed run leaves an
  earlier export as it was; the parent directory is created if missing,
  as for the map;
- the new file gets the permissions of the file it replaces, so an export
  the user restricted (security T2: as sensitive as the code) is not
  widened by the next run; a new file gets the default permissions.

The hidden file, the permissions and the directory-name check apply to
`lcart map` as well: both commands share one implementation.

Standard output stays empty on success (unchanged from ADR-0016); the
security T2 warning still applies to the file.

## Consequences

- `lcart map` and `lcart query export` share one implementation of the
  rule (`check_own_output`, `replace_file` in `src/cli/src/commands.cpp`),
  so a later output command gets it by naming its first line.
- An export written by a future format version is still recognised, as
  long as the header keeps `record` and `format` as its first two keys;
  changing them needs a new ADR anyway (ADR-0016 export format).
- A user who wants to overwrite another file deletes it first; there is no
  `--force` for exports.
- Devices and pipes are refused as outputs: `--output /dev/null` or
  `--output /dev/stdout`, accepted before, now exit 1 ("not a facts
  export"); `--output -` writes to standard output.
- Replacing by rename gives the output a new inode: hard links to an
  earlier export keep the old content, and its owner becomes the user
  running lcart. Only the permission bits are carried over.
- A directory created for the output stays when the run then fails
  (`create_directories` comes before the write); it is empty.
- The export is streamed to the hidden file, not held in memory, as it
  was streamed to the output before.
