# 0003. Apache-2.0 for own code; LGPL-2.1 obligations for libmodbus

- **Status:** Accepted
- **Date:** 2026-10-03
- **Requirements:** [NFR-008](../specs/requirements.md#nfr008-upstream-unmodified-v1), [NFR-011](../specs/requirements.md#nfr011-licensing-v1)

This ADR is an engineering decision, not legal advice. Before distributing
binaries commercially, have the obligations confirmed by someone qualified.

## Context

- The tool is meant to be used inside companies on proprietary code, and
  possibly adapted there. Its licence must be easy for a company's legal
  review to accept.
- The demonstration target, libmodbus, is licensed **LGPL-2.1-or-later**
  (stated in its `README.md`; licence text in `COPYING.LESSER`). Its test
  programs under `tests/` carry a separate BSD-style licence
  (`tests/LICENSE`, TODO: verify the exact terms before reusing any test
  code).
- Two parts of this repository interact with libmodbus differently:
  - the **tool** (`lcart`) only *reads* libmodbus sources as data; it does
    not link or include them;
  - the **M3–M5 artefacts** (characterization tests, fuzz harness, C++20
    wrapper) *compile against and link* libmodbus.

## Options considered

1. **MIT.** Short, very permissive, well understood. No explicit patent
   grant; no explicit terms for contributions.
2. **Apache-2.0.** Permissive; explicit patent licence and patent
   retaliation clause; explicit contribution terms (section 5); `NOTICE`
   mechanism. Longer text; compatible with LGPL-2.1-or-later libraries when
   linking (the combination is distributed under each part's licence).
3. **LGPL/GPL for the tool.** Would keep improvements open, but discourages
   the in-company use the tool is designed for.

## Decision

<a id="adr0003-apache-2-v1"></a>All code and documentation written for this
repository is licensed under **Apache-2.0**. Reason: the explicit patent
grant and contribution terms matter to the companies that would adopt it;
the cost (a longer licence, a `NOTICE` file) is small.

<a id="adr0003-targets-unmodified-v1"></a>libmodbus is included only as a
git submodule under `targets/libmodbus`, pinned to a tag, and never
modified. Build scripts, harnesses, tests and the wrapper live outside the
submodule. The libmodbus licence applies to the submodule; `NOTICE` says so.

<a id="adr0003-lgpl-wrapper-obligations-v1"></a>For the C++20 wrapper (M5),
and anything else that links libmodbus and is **distributed as a binary**
(this repository itself contains only a submodule reference to upstream
libmodbus, not a copy of it):

1. **Link dynamically** against libmodbus (`libmodbus.so`). This is the
   default build. The wrapper is then a "work that uses the Library"
   (LGPL-2.1 §5–6) and can carry its own licence (Apache-2.0).
2. If a **static** build is ever distributed, also distribute the wrapper's
   object files (or source) so a user can relink with a modified libmodbus
   (LGPL-2.1 §6(a)).
3. Ship the LGPL-2.1 text and a notice that the product uses libmodbus, with
   a pointer to its source (the exact upstream tag), when distributing
   binaries that include or link it.
4. Do not restrict reverse engineering for debugging such modifications in
   any terms that accompany a binary (LGPL-2.1 §6).
5. Header use: the wrapper includes `modbus.h`. LGPL-2.1 §5 allows the use of
   numerical parameters, data structure layouts, accessors and small macros
   and inline functions (ten lines or less) from headers; the wrapper must
   not copy larger parts of libmodbus code into its own sources.
6. Changes to libmodbus itself, if ever needed, go upstream as pull requests
   or into a fork that remains LGPL; never into this repository.

## Consequences

- Companies can adopt and adapt the tool without copyleft obligations.
- The wrapper's build defaults to shared linking; a CI check (M5) verifies
  `ldd` shows `libmodbus.so`.
- Contributions need no CLA; Apache-2.0 §5 covers inbound licensing.
- If a GPL-only target is ever added, its harnesses need a separate licence
  analysis (new ADR).
