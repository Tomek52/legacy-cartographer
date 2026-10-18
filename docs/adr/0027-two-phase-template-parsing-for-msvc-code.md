# 0027. Two-phase template parsing for MSVC code

- **Status:** Accepted
- **Date:** 2026-10-18
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [FR-004](../specs/requirements.md#fr004-record-call-edges-v1),
  [B-40](../plan/backlog.md#b40-msvc-delayed-template-parsing-v1)
- **Supersedes / superseded by:** supersedes, in
  [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md#adr0021-msvc-flags-v1),
  only "lcart adds no MSVC flag itself" (lcart now adds
  `-fno-delayed-template-parsing`) and the consequence that calls in
  never-instantiated function templates are missing; the rest of ADR-0021
  stands

## Context

For an MSVC target before C++20, Clang's driver passes
`-fdelayed-template-parsing` to cc1 unless the command names
`-fno-delayed-template-parsing` or its clang-cl alias `/Zc:twoPhase`; the
last of the pair wins (LLVM 20.1.2 `clang/lib/Driver/ToolChains/Clang.cpp`
lines 7418–7436, `Args.hasFlag`). The comment there gives the reason:
"Many old Windows SDK versions require this to parse." clang-cl's
`/permissive` expands to `/Zc:twoPhase-` and `/permissive-` to
`/Zc:twoPhase` (`clang/lib/Driver/ToolChains/MSVC.cpp` lines 957–967 and
1011–1016). With delayed parsing a function template's body is parsed only
where the template is instantiated, so its calls are not in the AST of a
translation unit that does not instantiate it. B-17 found this
([ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md)
finding 7); the question stayed open.

Measured with lcart and clang 20.1.2 on the fixture
`tests/fixtures/msvc_templates` (three units: `library.cc` with a function
template instantiated nowhere; `user.cc`, which instantiates the template
`relay` of `helpers.h`; `lenient.cc`, with a template body that reads a
member of a class completed only after it), each with `cl.exe /c
/std:c++17` and with `clang --target=x86_64-pc-windows-msvc -std=c++17 -c`,
the same for both:

| Template parsing | Units parsed | Diagnostics | Function facts | Call facts |
|---|---|---|---|---|
| delayed (Clang's default for MSVC) | 3 of 3 | 0 | 6 | 1 |
| two-phase | 2 of 3 | 1 error (`lenient.cc`) | 6, the same | 4 |

- **Facts gained:** three calls, from `never_instantiated`, `relay` and
  `width_of` to `helper`.
- **Units that stop parsing:** one, `lenient.cc` ("member access into
  incomplete type 'const Widget'"). Clang recovers from the error: the
  unit's two functions and both its calls are still recorded, and its
  status is `failed`, with the diagnostic in the facts. Clang stops a unit
  after 20 errors (`-ferror-limit 19` in `clang-20 -###`), and what follows
  that point is lost.
- **Delayed parsing makes facts depend on the set of units.** `relay` is
  defined in a header; lcart records a definition from the first unit in
  path order that has it (ADR-0013). That is `library.cc`, which does not
  instantiate `relay`, so `relay` → `helper` is missing. With `user.cc`
  analysed alone the same call is recorded.

Not measured: the Windows SDK and MSVC headers that Clang's comment refers
to (none in the container or CI), and whether MSVC itself accepts
`lenient.cc` without `/permissive-` (`TODO: verify`,
[open questions](../open-questions.md#not-verified-yet-todo-verify)).

## Options considered

1. **Never: keep the driver's default.** No unit stops parsing, and the
   facts match what clang-cl, clang-tidy and clangd see. But calls are
   missing without any sign in the facts or the output, and which calls of
   a header template are recorded depends on which units are analysed and
   how their paths sort.
2. **Always: lcart adds `-fno-delayed-template-parsing` before the
   command's own arguments.** Calls in templates are recorded whether or
   not a unit instantiates them. Code that only delayed parsing accepts
   gives an error diagnostic and a failed unit, which the summary and the
   facts show; Clang recovers, so most of its facts stay. A command that
   names delayed parsing itself (`/Zc:twoPhase-`, `/permissive`,
   `-fdelayed-template-parsing`) keeps it, since its flag comes later. The
   compilation database stays the only place to choose otherwise.
3. **Always, appended after the command's arguments.** As option 2, but a
   database could not choose delayed parsing for code that needs it.
4. **A configuration option in `lcart.toml`** (for example an
   `[analysis]` key choosing the template parsing). It is not a consent
   switch, since nothing leaves the machine, and the configuration's hash
   already makes a resumed run refuse a changed value. But it is a second
   source of compiler flags next to the database, which ADR-0021 rejected
   for the MSVC flags; the commands already have standard spellings for the
   same choice; and it adds a key to the parser and its fuzzer. Its default
   would still have to be option 1 or 2.

## Decision

Option 2.

<a id="adr0027-two-phase-by-default-v1"></a>**Two-phase template parsing by
default.** lcart inserts `-fno-delayed-template-parsing` right after the
program name of every compile command it parses, next to
`--no-default-config`, except in a `-cc1` command (cc1 does not accept the
flag). The flag is accepted by the `clang` driver and in clang-cl mode, and
it does not change the cc1 command for other targets or for C++20 and
later (checked with `clang-20 -###`); for C under an MSVC target it drops
`-fdelayed-template-parsing`, which has no effect on C. lcart adds no configuration key for
it.

<a id="adr0027-command-chooses-delayed-v1"></a>**The command can choose
delayed parsing.** A command that names `-fdelayed-template-parsing`,
`/Zc:twoPhase-` or `/permissive` anywhere in its arguments is parsed with
delayed template parsing, as the compiler would parse it; lcart does not
remove or reorder those arguments.

The reasons: lcart's facts are what the verifier checks claims against, and
a missing call is silent, while a unit that stops parsing is reported by
`lcart analyze` and stored as diagnostics (coverage, ADR-0009); facts that
depend on which other units are analysed are worse for NFR-004's goal of
the same facts from the same code than a visible error. The cost measured on
the fixture is one failed unit whose facts are still recorded. The
unmeasured risk, old Windows SDK headers that need delayed parsing, shows
itself the same way, as errors in the SDK's headers, and has a remedy in
the database (option 2's second rule) that clang-cl and MSVC understand
too.

## Consequences

- Facts of MSVC code before C++20 change: calls in function templates that
  a unit does not instantiate are recorded, also from headers. A unit with
  code only delayed parsing accepts now fails with error diagnostics; its
  remaining facts are recorded.
- The how-to [Analysing an MSVC code base under
  WSL2](../how-to/msvc-under-wsl2.md) says what users see and how to choose
  delayed parsing in the database (for example with `jq`).
- Same database, same flags: the flag is constant, so the facts still
  depend only on the database and the code (NFR-004).
- `tests/analysis/msvc_code_test.cpp` (`MsvcTemplatesTest`) checks both
  rules on the fixture; `msvc_constructs` and its expectations are
  unchanged.
- Revisit if a real code base with an old Windows SDK fails in many units
  (then a per-unit fallback to delayed parsing could be weighed against its
  cost of a second parse), or if Clang changes its MSVC default.
