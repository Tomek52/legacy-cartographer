# Fixture: msvc_templates

Function templates in MSVC code before C++20 (B-40, ADR-0027). For an MSVC
target, Clang's driver turns on `-fdelayed-template-parsing` unless the
language is C++20 or later (LLVM 20.1.2
`clang/lib/Driver/ToolChains/Clang.cpp` lines 7418–7436). A template body is
then parsed only where the template is instantiated, so the calls in a
template that a translation unit never instantiates are not in its AST.
lcart parses with `-fno-delayed-template-parsing` unless the command asks
for delayed parsing. Self-contained: no Windows SDK or MSVC headers.

The sources end in `.cc` for the reasons given in the
[msvc_constructs README](../msvc_constructs/README.md); reformatting them
would move the lines and columns below. The analysis takes the three `.cc`
files in path order: `lenient.cc`, `library.cc`, `user.cc`.

- `helpers.h`: declares `helper`; the function template `relay` (line 5)
  calls `helper` (line 5, column 51).
- `library.cc`: defines `helper` (line 3); the function template
  `never_instantiated` (line 5) calls `helper` (line 5, column 64) and is
  instantiated nowhere. It includes `helpers.h` and does not instantiate
  `relay`; it is the first unit in path order that defines `relay`, so
  `relay` and its calls are recorded from this unit (ADR-0013).
- `user.cc`: `use_relay` (line 3) calls `relay(3)`, which instantiates it;
  calls to a template's instantiation are not recorded (FR-003).
- `lenient.cc`: declares `helper` itself (no `helpers.h`); the function
  template `width_of` (line 5) reads a member of `Widget` (line 5), a class completed only on line 7, and calls `helper`
  (line 5, column 85). With delayed parsing Clang accepts it, since the body
  is never parsed; with two-phase parsing it reports "member access into
  incomplete type 'const Widget'" (line 5) and the unit fails. Whether MSVC
  without `/permissive-` or `/Zc:twoPhase` accepts it was not tried on
  Windows; it is listed as not verified in `docs/open-questions.md`.
  `lenient_entry` (line 9) calls `helper` (line 9, column 30).

Measured with lcart (clang 20.1.2), each `.cc` file with `cl.exe /c
/std:c++17` or `clang --target=x86_64-pc-windows-msvc -std=c++17 -c`:

| Template parsing | Units parsed | Diagnostics | Calls recorded |
|---|---|---|---|
| delayed (Clang's MSVC default) | 3 of 3 | 0 | 1: `lenient_entry` → `helper` |
| two-phase (`/Zc:twoPhase`, `-fno-delayed-template-parsing`) | 2 of 3 | 1 (error, `lenient.cc:5`) | 4: also `never_instantiated`, `relay` and `width_of` → `helper` |

The six function facts are the same in both. With delayed parsing,
`relay` → `helper` is recorded when `user.cc` is analysed alone, but not
with `library.cc` before it: the calls of a template in a header depend on
which units are analysed.
