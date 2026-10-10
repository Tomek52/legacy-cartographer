# Analysing an MSVC code base under WSL2

`lcart` runs on Ubuntu 24.04, natively or under WSL2, and parses code
written for MSVC with Clang 20 in its MSVC compatibility mode
([ADR-0009](../adr/0009-linux-and-wsl2-only.md),
[NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1)). The flags
and the handling of clang-cl commands are fixed in
[ADR-0021](../adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md).

Everything on this page marked "tried" was run on Linux with clang 20.1.2
and synthetic compilation databases over the self-contained fixture
`tests/fixtures/msvc_constructs` (B-17). Nothing was tried on Windows, with
MSBuild, or with real Windows SDK or MSVC headers: those steps are marked
`TODO: verify` and listed in the
[open questions](../open-questions.md#not-verified-yet-todo-verify).

## 1. Put the code where lcart can read it

The analysis root can be any directory, including one under
`/mnt/<drive>/`. Stored paths are relative to the root, so facts contain no
drive letter: tried with a root at `<tmp>/mnt/c/src/project` on the Linux
file system, which shows only the path handling. A root on a real WSL2
drive mount (drvfs: case-insensitive names, Windows permissions, slower
file access) is `TODO: verify`. ADR-0009 recommends copying the code base
into the Linux file system for speed.

## 2. Get a compilation database

`lcart` reads `compile_commands.json`; producing it for an MSBuild
solution is outside the tool
([ADR-0009](../adr/0009-linux-and-wsl2-only.md#adr0009-compdb-is-input-v1)).
`TODO: verify` a tested way to get one from an MSBuild solution
(NFR-013 criterion 3; needs a Windows machine).

A database made on Windows will name Windows paths (`C:\src\...`) in
`directory`, `file` and the include options. On Linux these are not
absolute paths, so they have to be rewritten to the WSL2 paths
(`/mnt/c/src/...`) or to the copy from step 1. `TODO: verify` on a real
database; lcart does not rewrite them.

## 3. Write the commands so that Clang reads them as MSVC code

Two styles work (tried; `tests/analysis/msvc_code_test.cpp`):

- **clang-cl commands**, as an MSBuild-derived database would have them:

    ```json
    {"directory": "/mnt/c/src/project",
     "arguments": ["cl.exe", "/c", "/nologo", "/W4", "/DAPI_EXPORTS", "/I.",
                   "/std:c++17", "/EHsc", "/Foapi.obj", "api.cpp"],
     "file": "api.cpp"}
    ```

    The program name selects clang-cl's mode: `clang-cl`, `cl` or
    `cl.exe`, also behind a Windows path such as `C:\VS\bin\cl.exe`
    (tried). `CL.EXE` in upper case is not recognised on Linux (tried: it
    is then parsed as GNU-style code and fails); rename it, or add
    `--driver-mode=cl` after the program name. No target flag is needed.

- **`clang` driver commands** with the MSVC target:

    ```json
    {"directory": "/mnt/c/src/project",
     "arguments": ["clang", "--target=x86_64-pc-windows-msvc",
                   "-fms-extensions", "-fms-compatibility",
                   "-fms-compatibility-version=19.33", "-c",
                   "-DAPI_EXPORTS", "-I.", "api.cpp"],
     "file": "api.cpp"}
    ```

    The target alone implies the three MS flags; give
    `-fms-compatibility-version` the version of the MSVC the code base is
    built with (`_MSC_VER` 1929 is 19.29). Without the target, even with
    the MS flags, `__declspec(dllexport)` and `__stdcall` are ignored with
    warnings; without either, the fixture does not parse (tried).

Prefer `"arguments"` arrays: a `"command"` string is split by GNU shell
rules on Linux, where a backslash is an escape character, so
`/IC:\src\inc` in a `"command"` string loses its backslashes (Clang source,
see ADR-0021). Response files (`@file`) are split the same way.

Tried and harmless: clang-cl commands print `argument unused during
compilation: '/c'` and `1 warning generated.` on standard error. That is a
driver warning about lcart's own `-fsyntax-only`; it is not a diagnostic in
the facts.

lcart removes options that would load code into it (`/clang:-fplugin=`,
`-Xclang -load`, ...) and warns for each
([security T5](../security.md#security-malicious-build-input-v1)).

## 4. Make the Windows SDK and MSVC headers available

Self-contained code needs nothing more (tried). Real code includes
`<windows.h>` and the MSVC C++ library, and the database must name them.
None of these has been tried with real headers (`TODO: verify`):

| Option | Driver style | What it names |
|---|---|---|
| `/winsysroot <dir>` | clang-cl | a directory with `VC/Tools/MSVC/<version>` and `Windows Kits/10` |
| `/vctoolsdir <dir>`, `/winsdkdir <dir>`, `/winsdkversion <v>` | clang-cl | the MSVC tools and the SDK separately |
| `/imsvc <dir>` | clang-cl | one system include directory |
| `-Xmicrosoft-windows-sys-root <dir>`, `-Xmicrosoft-visualc-tools-root <dir>` | `clang` | as `/winsysroot`, `/vctoolsdir` |
| `-isystem <dir>` | `clang` | one system include directory |

The headers can be copied from a Windows machine into the Linux file
system or read through `/mnt/c` (`TODO: verify` which works, and how case
differences between `#include` lines and file names behave).

Name the headers in the database. Without `/winsysroot` or `/vctoolsdir`,
Clang takes system include directories from the environment of the shell
running lcart (`INCLUDE`, `EXTERNAL_INCLUDE`, `VCToolsInstallDir`,
`VCINSTALLDIR`, and a `cl.exe` next to a `link.exe` on `PATH`), so two
shells could produce different facts. Whether a WSL2 `PATH` that includes
Windows directories makes Clang find an MSVC installation is
`TODO: verify`.

## 5. Run the analysis and check the coverage

```bash
lcart analyze --compilation-database compile_commands.json --root /mnt/c/src/project
lcart query export | grep '"record":"diagnostic"'   # what did not parse
```

Each translation unit Clang cannot parse becomes diagnostics and lowers
coverage (ADR-0009).

Templates: before C++20, the MSVC target turns on
`-fdelayed-template-parsing`, and calls in the body of a function template
that is never instantiated are not recorded (tried). Adding
`-fno-delayed-template-parsing` (`clang`) or `/Zc:twoPhase` (clang-cl) to
the commands records them, but code that only MSVC's lenient template
parsing accepts may then fail to parse
([open question](../open-questions.md#open-technical-questions)).

## Steps tried in B-17

| Step | Result |
|---|---|
| `clang` driver, Linux target, fixture | fails: Clang stops after 20 errors |
| `clang` driver, Linux target, `-fms-extensions -fms-compatibility` | parses; 6 warnings (`__stdcall`, `dllexport` ignored) |
| `clang` driver, `--target=x86_64-pc-windows-msvc`, with or without the MS flags | parses, no diagnostics, expected calls |
| `clang-cl /c ... /std:c++17 /EHsc /Fo...`, `cl.exe /c ...`, `C:\VS\bin\cl.exe`, `clang --driver-mode=cl` | parse, no diagnostics, expected calls |
| `CL.EXE /c ...` | fails: not recognised as clang-cl on Linux |
| `cl.exe /c -MD ...` | parses since B-17 (`-MD` used to be dropped by a LibTooling adjuster) |
| `/clang:-fplugin=...`, `-clang:-fplugin=...`, `/clang:-Xclang /clang:-load ...` | removed with a warning since B-17; the plugin was not loaded before either |
| Root at a path like `/mnt/c/...` on the Linux file system | stored paths relative to the root |
| Root on a real drvfs mount | `TODO: verify` on WSL2 |
| Compilation database from an MSBuild solution | `TODO: verify` on Windows |
| Windows SDK and MSVC headers (`/winsysroot`, `/imsvc`, ...) | `TODO: verify` with real headers |
| Windows paths and backslashes in a database made on Windows | `TODO: verify` on a real database |
