# 0008. Project configuration in TOML

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [FR-005](../specs/requirements.md#fr005-map-files-to-modules-v1)

## Context

Users describe a project to `lcart`: analysis root, compilation database,
module rules (ordered path globs), handoff defaults. The file is written and
reviewed by hand, often with comments explaining why a file belongs to a
module. It lives next to private code and must be easy to diff.

## Options considered

1. **JSON** (nlohmann_json is already a dependency). No new dependency; no
   comments, noisy to write by hand.
2. **YAML** (yaml-cpp). Comments, compact; implicit typing pitfalls
   (`no` → false, unquoted versions as numbers), indentation errors, a large
   specification.
3. **TOML** (toml++). Comments, explicit types, arrays of tables fit ordered
   rules, small specification. One new header-only dependency, packaged in
   Ubuntu 24.04 (`libtomlplusplus-dev` 3.4.0) and in Conan Center
   (`tomlplusplus`, TODO: verify the version on the first CI run).

## Decision

<a id="adr0008-toml-v1"></a>The project configuration is **TOML**, file name
`lcart.toml`, parsed with **toml++**. Unknown keys are errors, so typos do not
silently change behaviour. Machine-to-machine formats (claims, facts export,
reports) stay JSON.

<a id="adr0008-no-consent-in-config-v1"></a>Consent-like switches
(`--include-source`, and in M6 `--allow-remote-source`) are command-line
only and are rejected if they appear in the file.

Example:

```toml
[project]
name = "libmodbus"
root = "targets/libmodbus"
compilation_database = "build/targets/libmodbus/compile_commands.json"

# First matching rule wins; unmatched files fall back to their directory.
[[modules]]
name = "rtu"
paths = ["src/modbus-rtu*"]

[[modules]]
name = "tcp"
paths = ["src/modbus-tcp*"]

[[modules]]
name = "core"
paths = ["src/modbus.c", "src/modbus-data.c", "src/modbus*.h"]
```

## Consequences

- One more dependency (header-only), added with B-11; the configuration
  parser gets a fuzz target (NFR-010).
- The rules order matters and is visible in the file.
