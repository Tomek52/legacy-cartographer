# legacy-cartographer

`lcart` maps large C/C++ code bases. It builds a fact database (files,
modules, functions, call graph, includes) with Clang, and can have an LLM
write documentation from it. Every structural statement the LLM makes is
recorded as a claim and checked against the facts: **confirmed**,
**unverified** or **contradicted**. The verification report is part of the
output.

Demonstrated on [libmodbus](docs/targets/libmodbus.md) (C, LGPL-2.1), a
pinned and unmodified submodule.

## Why

Legacy code often has no reliable documentation, and LLM summaries cannot be
trusted as they are. This tool keeps the useful part (fast, readable
summaries) and makes the risky part (wrong statements) visible. It works
without any LLM, and fully offline with a local one; source code is never
sent to a remote API without an explicit per-run flag
([ADR-0004](docs/adr/0004-llm-provider-abstraction-and-code-privacy.md)).

## Status

**M0: foundations.** Specs, architecture, ADRs and an empty but building
skeleton. No analysis yet. Next: M1, the fact database and module map for
libmodbus without LLM. See the [roadmap](docs/plan/roadmap.md).

## Build

Ubuntu 24.04, GCC 13 or Clang 18, CMake ≥ 3.25, Ninja, Conan 2.

```bash
# With Conan (presets: debug, clang-debug, release, asan-ubsan)
scripts/conan-install.sh debug
cmake --workflow --preset debug

# Without Conan, from Ubuntu packages (offline machines)
sudo apt-get install ninja-build libsqlite3-dev nlohmann-json3-dev libgtest-dev
cmake --workflow --preset debug-system

build/debug-system/src/cli/lcart --help
```

Quality gates: `scripts/check-format.sh`, `scripts/run-clang-tidy.sh <preset>`,
`scripts/docs-check.sh`.

## Documentation

All documentation is in [docs/](docs/index.md) and builds with MkDocs:

```bash
python3 -m venv .venv-docs && .venv-docs/bin/pip install -r requirements-docs.txt
.venv-docs/bin/mkdocs serve
```

Start with the [vision](docs/vision.md), the
[requirements](docs/specs/requirements.md) and the
[architecture](docs/architecture/index.md).

## Licence

Apache-2.0 ([LICENSE](LICENSE), [ADR-0003](docs/adr/0003-licensing.md)).
Code under `targets/` keeps its upstream licence (see [NOTICE](NOTICE)).
