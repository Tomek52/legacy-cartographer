# Fixture: msvc_constructs

C++ written for MSVC (B-17, NFR-013 criterion 1, ADR-0021). Self-contained:
no Windows SDK or MSVC headers. Clang in its Linux mode rejects it
(`__declspec` without `-fms-extensions`, `__int64`, `__forceinline`,
`__if_exists`); with the target `x86_64-pc-windows-msvc`, from the `clang`
driver or from `clang-cl`, it parses without diagnostics.

The sources end in `.cc`, not `.cpp`: they are analysed input written in
MSVC's dialect (`typedef`, `__int64`), not lcart's own code, and the
format and clang-tidy checks select `*.cpp` (clang-tidy would parse them
in Linux mode). Reformatting them would also move the lines and columns
below.

- `api.h`: `#pragma once` (included twice by `api.cc`),
  `__declspec(dllexport)` / `__declspec(dllimport)` behind a macro,
  `unsigned __int64`, `unsigned __int32`, `__stdcall` and `__cdecl`.
- `api.cc`: `checksum` (line 12) calls `mix` (line 15, column 15), a
  `__forceinline` function, and `finish` (line 17, column 12), a
  `__declspec(noinline)` one, both in an anonymous namespace; `report`
  (line 20) calls `finish` (line 21, column 86) inside `__if_exists`.
- `runtime.cc`: fails with an `#error` unless `_DLL` is defined, which cl
  does for `/MD` or `-MD`; `report_runtime` (line 8) calls `report`
  (column 43) and `checksum` (column 50). Analysed only with clang-cl
  commands.
