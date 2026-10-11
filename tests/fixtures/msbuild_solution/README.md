# Fixture: msbuild_solution

An MSBuild solution for NFR-013 criterion 3 (ADR-0022): the input of the
MSBuild logger `tools/msbuild-compdb/CompDbLogger.cs` and of
`tools/msbuild-compdb/verify.ps1`. It builds only on Windows with Visual
Studio or Build Tools 2022 (C++ workload, Windows SDK).

- `Fixture.sln`: projects `core` and `app`, Debug and Release, x64 and Win32.
- `core/core.vcxproj`: static library; `core.h` (`#pragma once`,
  `__stdcall`, `unsigned __int64`), `core.cc` (`<windows.h>`, `core_stamp`
  (line 13) calls `core_log` (line 14, column 3) and `core_mix` (line 15,
  column 10), a `__forceinline` function in an anonymous namespace).
- `app/app.vcxproj`: console application with a reference to `core`;
  `main.cc` (`<cstdio>`) fails with `#error` unless the project's
  `PreprocessorDefinitions` provide `APP_GREETING="hello from app"` (a
  quoted value with spaces), and `/I..\core` is relative to the project;
  `main` (line 9) calls `core_stamp` (line 11, column 10).
- `compile_commands.golden.json`: what the logger wrote for x64 Debug,
  with the machine's paths replaced by `@ROOT@` (this directory), `@MSVC@`
  (the MSVC tools directory), `@SDK@` (the Windows SDK `Include\<version>`
  directory) and `@VS@` (the Visual Studio installation). The test
  `MsbuildDatabaseTest` (`tests/analysis/msvc_code_test.cpp`) replaces them
  with this directory and stand-in headers; `verify.ps1` regenerates and
  compares it. To regenerate it after a change to the logger or to the
  projects, run the build with the `map=` parameters from the `$maps` line
  of `verify.ps1` and `out=` set to this file.

The sources end in `.cc`, not `.cpp`, for the reason given in the
[msvc_constructs README](../msvc_constructs/README.md); reformatting them
would move the lines and columns above. Do not build in this directory:
MSBuild writes `x64\`, `Win32\` and `*.pdb` next to the projects. Copy it
first (`verify.ps1` does).
