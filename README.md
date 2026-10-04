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
summaries) and makes the risky part (wrong statements) visible. Static
analysis works without any LLM. For documentation, `lcart` writes a bundle
with a ready prompt for a coding agent of your choice (e.g. Claude Code) and
verifies what the agent returns; `lcart` itself makes no network requests
([ADR-0006](docs/adr/0006-external-agent-handoff-for-llm-work.md)).
Built-in providers and local models are planned for later (M6).

## Status

**M1 in progress: fact database and module map, without LLM.** Done so
far: Clang LibTooling driver (B-07) and the SQLite fact store (B-06). See the [roadmap](docs/plan/roadmap.md).

## Build

Ubuntu 24.04, GCC 14 or Clang 20 (C++23), CMake ≥ 3.25, Ninja, Conan 2.

```bash
# With Conan (presets: debug, clang-debug, release, asan-ubsan)
scripts/conan-install.sh debug
cmake --workflow --preset debug

# Without Conan, from Ubuntu packages (offline machines;
# presets: debug-system, asan-ubsan-system)
sudo apt-get install ninja-build g++-14 clang-20 libclang-rt-20-dev \
    llvm-20-dev libclang-20-dev libclang-cpp20-dev \
    libsqlite3-dev nlohmann-json3-dev libtomlplusplus-dev libgtest-dev
cmake --workflow --preset debug-system

build/debug-system/src/cli/lcart --help
```

Quality gates: `scripts/dod.sh` runs all local checks (format, clang-tidy,
docs, both offline presets); `scripts/quick-check.sh` is the fast inner loop.

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
