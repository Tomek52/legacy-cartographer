# Analysing an MSVC code base under WSL2

`lcart` runs on Ubuntu 24.04, natively or under WSL2, and parses code
written for MSVC with Clang 20 in its MSVC compatibility mode
([ADR-0009](../adr/0009-linux-and-wsl2-only.md),
[NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1)). The flags
and the handling of clang-cl commands are fixed in
[ADR-0021](../adr/0021-clang-flags-and-clang-cl-databases-for-msvc-code.md).

Everything on this page marked "tried" was run with clang 20.1.2. Steps 3
and 5 were first tried on Linux with synthetic compilation databases over
the self-contained fixture `tests/fixtures/msvc_constructs` (B-17).
Steps 1, 2 and 4 were tried on 2026-10-11 on Windows 11 with Visual Studio
Build Tools 2022 (MSVC 14.44, Windows SDK 10.0.26100.0) and WSL2 with Ubuntu
24.04, over the MSBuild solution `tests/fixtures/msbuild_solution`
([ADR-0022](../adr/0022-msbuild-logger-for-compilation-databases.md)).
What is still unverified is marked `TODO: verify` and listed in the
[open questions](../open-questions.md#not-verified-yet-todo-verify).

## 1. Put the code where lcart can read it

The analysis root can be any directory, including one under
`/mnt/<drive>/` (tried). Stored paths are relative to the root, so facts
contain no drive letter. Two differences from a root on the Linux file
system:

- **The default fact database now works on the drive mount.** Earlier, with
  the root on `/mnt/c/...` and the default database in `lcart-out/` under it,
  `lcart analyze` stopped with `preparing a fact query: database is locked`,
  and the workaround was to give `--db` a path on the Linux file system. The
  cause was lcart's own run lock: it took `flock` on the partial database
  file, and on the drive mount (file system type v9fs) that clashed with
  SQLite's own POSIX locks on the same file (on the Linux file system they do
  not clash). lcart now takes the run lock on a separate file
  (`.<name>.partial.lock` next to the partial database;
  [ADR-0024](../adr/0024-run-lock-on-a-separate-file.md#adr0024-run-lock-on-separate-file-v1)),
  so the default database works on `/mnt/c` too. Tried (2026-10-11, clang 20.1.2): a
  one-file project under `/mnt/c` (confirmed v9fs) analysed with the default
  database — it failed with `database is locked` before the change and
  succeeded after it; `--db` on the Linux file system worked both times.
- The facts were the same for a root on `/mnt/c` and for a copy on the Linux
  file system (tried: sorted exports of the sample compared line by line).
  Reading is slower on the mount; ADR-0009 recommends copying the code base
  into the Linux file system for speed, and the Linux file system is still
  the faster place for the database.

Case-insensitive file names on drvfs against the case of `#include` lines
were not tried (`TODO: verify`).

The repository's `.gitattributes` keeps every text file LF, whatever
`core.autocrlf` says. A checkout made before that file existed has CRLF
files, and 11 of the tests of `lcart` itself fail (golden files, file
hashes): make a fresh clone.

## 2. Get a compilation database from an MSBuild solution

`lcart` reads `compile_commands.json`; producing it is outside the tool
([ADR-0009](../adr/0009-linux-and-wsl2-only.md#adr0009-compdb-is-input-v1)).
MSBuild writes none, so this repository has a small MSBuild logger that
records the `CL` tasks of a build
([ADR-0022](../adr/0022-msbuild-logger-for-compilation-databases.md#adr0022-msbuild-logger-v1)).
It needs only Visual Studio or Build Tools 2022 with the C++ workload, on
Windows. In a "Developer PowerShell", or with the full path of `MSBuild.exe`:

```powershell
# once: compile the logger with the compiler that ships with MSBuild
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$bin = "$(& $vswhere -latest -products * -property installationPath)\MSBuild\Current\Bin"
& "$bin\Roslyn\csc.exe" -nologo -target:library -out:CompDbLogger.dll `
    "-r:$bin\Microsoft.Build.Framework.dll" tools\msbuild-compdb\CompDbLogger.cs

# build the solution again from scratch, with the logger
& "$bin\MSBuild.exe" C:\src\project\Project.sln -p:Configuration=Debug -p:Platform=x64 `
    -t:Rebuild -nologo -v:q `
    "-logger:LcartMsbuildCompdb.CompDbLogger,CompDbLogger.dll;out=compile_commands.json|map=C:\src\project=/mnt/c/src/project|map=C:\Program Files (x86)=/mnt/c/Program Files (x86)"
```

- `-t:Rebuild` is required: an up-to-date project runs no compiler, so
  nothing is recorded (the logger says so).
- `out=` is the file to write. Parameters are separated by `|`, because
  Windows paths contain `;` and spaces.
- `map=<from>=<to>` rewrites a path prefix everywhere in the database and
  turns `\` into `/`. Give one for the source tree and one for the place
  that holds Visual Studio and the Windows SDK (`C:\Program Files (x86)` in
  the sample). The first match wins, so list the most specific first. Without
  `map=` the database keeps Windows paths and `lcart` cannot use it under
  WSL2 (tried: `Compile command not found`, 0 of 2 translation units).
- The build has to be one that compiles. Use the configuration and platform
  you want to analyse. A solution can be run with `-m`; the output does not
  depend on it (tried with `-m:4`).
- `MSBuild.exe build.binlog -logger:...` records the same database from a
  binary log (`-bl`) saved by an earlier build (tried), so a colleague's log
  can be turned into a database, if the Visual Studio and SDK directories
  exist on the machine that replays it (the logger names only existing
  directories).

What the logger writes for each source file is an `"arguments"` array:
`cl.exe`, the options as MSBuild passed them (precompiled-header options
removed), one `/imsvc <dir>` for each system include directory that MSBuild
gives the compiler through `INCLUDE` (the MSVC library, the Windows SDK
`ucrt`, `um`, `shared`, ...), and the absolute source file. The `/imsvc`
entries are needed: without them, and with `INCLUDE` unset, both files of the
sample failed with `'cstdio' file not found` (tried).

Tried with the sample: a static library and an application with a project
reference; `<windows.h>`; a definition with a quoted value and spaces; a
relative `/I..\core`; a precompiled header (`/Yc`, `/Yu`; 3 of 3 translation
units parsed); x64 and Win32 configurations. Not tried: older Visual Studio
versions, other platform toolsets than `v143`, C++/CLI, custom build steps,
a solution with compile errors (`TODO: verify`).

The Win32 configuration gives `--target=i686-pc-windows-msvc` commands. With
`lcart` as of 2026-10-11 (commit `41f6e4f`) the files that include
`<windows.h>` failed in it (`MS-style inline assembly is not available:
Unable to find target for this triple`), because `lcart` registered no LLVM
targets; `clang-cl` parsed the same files. `lcart` now registers the targets
that its LLVM provides before it parses
([ADR-0025](../adr/0025-llvm-targets-registered.md#adr0025-register-all-targets-once-v1)),
and the tests parse a function with MS-style inline assembly for the x86 and
x86_64 Windows targets. The real Windows SDK with the Win32 configuration was
not tried again (`TODO: verify`); until it is, check the Win32 result of a
first analysis for diagnostics. arm64 has no MS-style inline assembly at all
(`clang` says `unsupported architecture 'aarch64'`).

On Windows, `tools\msbuild-compdb\verify.ps1` rebuilds the sample solution
and compares the logger's output with
`tests/fixtures/msbuild_solution/compile_commands.golden.json`; run it after
changing the logger.

Then run `lcart` under WSL2 (step 5). The default database in `lcart-out/`
works now, including under `/mnt/c`; `--db` on the Linux file system is still
faster.

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

**Tried:** `/imsvc <dir>` for each system include directory, read from the
Windows installation through `/mnt/c` (no copy). That is what the logger of
step 2 writes. With it, the sample solution (`<windows.h>`, `<cstdio>`) parsed
with no diagnostics under clang 20.1.2, whether the root was on `/mnt/c` or
on the Linux file system. Without the `/imsvc` entries and with `INCLUDE`
unset, it failed with `'cstdio' file not found`.

**Not tried** (`TODO: verify`), alternatives for the same job:

| Option | Driver style | What it names |
|---|---|---|
| `/winsysroot <dir>` | clang-cl | a directory with `VC/Tools/MSVC/<version>` and `Windows Kits/10` |
| `/vctoolsdir <dir>`, `/winsdkdir <dir>`, `/winsdkversion <v>` | clang-cl | the MSVC tools and the SDK separately |
| `-Xmicrosoft-windows-sys-root <dir>`, `-Xmicrosoft-visualc-tools-root <dir>` | `clang` | as `/winsysroot`, `/vctoolsdir` |
| `-isystem <dir>` | `clang` | one system include directory |

Also not tried: headers copied into the Linux file system instead of read
through `/mnt/c` (expected to be faster), and how case differences between
`#include` lines and file names behave on drvfs.

Name the headers in the database. Without `/winsysroot` or `/vctoolsdir`,
Clang takes system include directories from the environment of the shell
running lcart (`INCLUDE`, `EXTERNAL_INCLUDE`, `VCToolsInstallDir`,
`VCINSTALLDIR`, and a `cl.exe` next to a `link.exe` on `PATH`), so two
shells could produce different facts. Whether a WSL2 `PATH` that includes
Windows directories makes Clang find an MSVC installation is
`TODO: verify`.

## 5. Run the analysis and check the coverage

```bash
lcart analyze --compilation-database compile_commands.json \
    --root /mnt/c/src/project --db ~/facts/project.lcart.db
lcart query export --db ~/facts/project.lcart.db | grep '"record":"diagnostic"'   # what did not parse
```

On the sample solution this printed `analysed 2/2 translation units, 0
diagnostics` (tried: root on `/mnt/c`, database on the Linux file system,
database from step 2 with `map=`). The default database under the root works
too since [ADR-0024](../adr/0024-run-lock-on-a-separate-file.md) (tried
with a one-file project under `/mnt/c`); `--db` on
the Linux file system is kept here because it is faster. Each translation
unit Clang cannot parse becomes diagnostics and lowers coverage (ADR-0009).

Templates: before C++20, Clang's default for the MSVC target is delayed
template parsing, which leaves out the calls of a function template that a
translation unit does not instantiate. lcart parses with two-phase template
parsing instead (`-fno-delayed-template-parsing`, the same as clang-cl's
`/Zc:twoPhase`;
[ADR-0027](../adr/0027-two-phase-template-parsing-for-msvc-code.md#adr0027-two-phase-by-default-v1)),
so those calls are recorded, also for templates in headers. What you see
(tried on `tests/fixtures/msvc_templates`):

- Code that only delayed parsing accepts, such as a template body that uses
  a class completed only after it, gives an error diagnostic, and its
  translation unit counts as failed: `analysed 2/3 translation units, 1
  diagnostics` on the fixture, where delayed parsing gave `3/3, 0`. Clang
  recovers, so the unit's other functions and calls are still recorded; it
  stops after 20 errors.
- To parse such code as the compiler does, name delayed parsing in the
  commands: `/Zc:twoPhase-` (clang-cl; `/permissive` implies it) or
  `-fdelayed-template-parsing` (`clang`). lcart keeps it
  ([ADR-0027](../adr/0027-two-phase-template-parsing-for-msvc-code.md#adr0027-command-chooses-delayed-v1)),
  and the calls of templates a unit does not instantiate are missing again.
  For an `"arguments"` database, for example:

    ```bash
    jq '[.[] | .arguments |= (.[0:1] + ["/Zc:twoPhase-"] + .[1:])]' \
        compile_commands.json > compile_commands.delayed.json
    ```

Clang's driver says that "many old Windows SDK versions" need delayed
parsing; which ones, and whether their headers then fail in lcart, was not
tried (`TODO: verify`). If the diagnostics of a first analysis point into
the Windows SDK's headers, name delayed parsing as above.

## Steps tried in B-17 and on Windows

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
| Root on a real drvfs mount (`/mnt/c`), database in the default `lcart-out/` | before ADR-0024: fails `database is locked`; after: 2 of 2 translation units, 0 diagnostics (tried with a one-file project) |
| Root on a real drvfs mount, `--db` on the Linux file system | 2 of 2 translation units, 0 diagnostics; facts equal to those of a root on the Linux file system |
| Compilation database from an MSBuild solution: text log (`-v:detailed`) | not usable: relative paths, no system include directories, localized text |
| Compilation database from an MSBuild solution: the logger of step 2 (Build Tools 2022, x64, `-m:4`, a replayed `.binlog`, a precompiled header) | works; output identical for `-m:4` and serial |
| The same, Win32 configuration | database fine; in `lcart` the files with `<windows.h>` fail (no LLVM targets) |
| The same, Win32 configuration, after `lcart` registers the LLVM targets | a function with `__asm` parses for x86 and x86_64 (tests, stand-in code); the real `<windows.h>` of the SDK with Win32 not tried again |
| Windows database as made (`C:\...`, backslashes), no `map=` | fails: `Compile command not found`, 0 of 2 |
| The same with `map=` to `/mnt/c/...` | 2 of 2, 0 diagnostics |
| Real Windows SDK and MSVC headers through `/imsvc` and `/mnt/c` | parse with no diagnostics |
| No `/imsvc` entries, `INCLUDE` unset | fails: `'cstdio' file not found` |
| Function templates before C++20 (`msvc_templates` fixture), lcart's default (two-phase) | 2 of 3 units parsed, 1 error diagnostic; 4 calls, including those of templates a unit does not instantiate |
| The same with `/Zc:twoPhase-`, `/permissive` or `-fdelayed-template-parsing` in the commands | 3 of 3, no diagnostics; 1 call |
| `/winsysroot`, `/vctoolsdir`, `-isystem`, headers copied to the Linux file system | `TODO: verify` |
