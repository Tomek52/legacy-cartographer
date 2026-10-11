# 0022. A compilation database from an MSBuild solution: an MSBuild logger

- **Status:** Accepted
- **Date:** 2026-10-11
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [FR-001](../specs/requirements.md#fr001-ingest-compilation-database-v1)
- **Supersedes / superseded by:** — (closes the open part of
  [ADR-0021](0021-clang-flags-and-clang-cl-databases-for-msvc-code.md) and
  [ADR-0009](0009-linux-and-wsl2-only.md#adr0009-compdb-is-input-v1) on how to
  get a database from an MSBuild solution; both decisions stand)

## Context

[ADR-0009](0009-linux-and-wsl2-only.md#adr0009-compdb-is-input-v1) keeps
producing `compile_commands.json` outside `lcart` and promises the docs one
tested way for an MSBuild solution (NFR-013 criterion 3). Neither the
development container nor CI has Windows, so this was tried by hand on one
machine: Windows 11 Pro (Polish), Visual Studio Build Tools 2022 17.14
(MSBuild 17.14, MSVC 14.44.35207, Windows SDK 10.0.26100.0), WSL2 with Ubuntu
24.04, and `lcart` from commit `41f6e4f` built there with the Ubuntu clang
20.1.2 packages. The sample is the solution
`tests/fixtures/msbuild_solution`: a static library `core` (includes
`<windows.h>`, uses `__stdcall`, `__int64`, `__forceinline`) and an
application `app` (needs a quoted `APP_GREETING="hello from app"` from its
`PreprocessorDefinitions`, includes `..\core`), each in Debug and Release,
x64 and Win32.

What was found:

1. **The compiler command is only in the build.** MSBuild does not write a
   command file. The `CL` task runs `CL.exe` from the project directory, and
   its command line is in the log at detailed verbosity, but with paths
   relative to the project (`/I..\core`, `main.cc`) and without the system
   include directories.
2. **System headers come through the environment.** The Microsoft.Cpp
   targets set `INCLUDE` and `EXTERNAL_INCLUDE` from the properties
   `IncludePath` and `ExternalIncludePath` before `CL.exe` runs
   (`MSBuild/Microsoft/VC/v170/Microsoft.Cpp.Current.targets`, `SetEnv`).
   They are not in the command line. On the test machine they name the MSVC
   `include` directory, the Windows SDK `ucrt`, `um`, `shared`, `winrt` and
   `cppwinrt` directories, and some Build Tools directories.
   With them missing from the database and `INCLUDE` unset, both
   translation units fail (`'cstdio' file not found`).
3. **Text logs are localized.** MSBuild printed Polish here, so a parser
   that looks for the words of the output depends on the locale. The command
   line itself is not localized.
4. **A logger sees the real command, locale-free.** `TaskCommandLineEventArgs`
   has the task name (`CL`), the project file and the command line; the
   evaluated properties are in `ProjectEvaluationFinishedEventArgs`, which
   needs `IEventSource4.IncludeEvaluationPropertiesAndItems()`. The two are
   linked through `ProjectStartedEventArgs` (the command-line event's
   `EvaluationId` is -1; its `ProjectContextId` matches the project's).
   The properties arrive as `ProjectPropertyInstance` objects, which the
   logger reads by reflection so that it needs only
   `Microsoft.Build.Framework.dll`.
5. **A compiler runs only when the project is out of date.** An up-to-date
   project has no `CL` task, so the build has to be `-t:Rebuild`.
6. **The same logger replays a binary log.** `MSBuild.exe build.binlog
   -logger:...` produced a database with the same entries as the live
   build (compared as JSON).
7. **Parallel builds.** With `-m:4` the file was byte-identical to the serial
   one (NFR-004). The fixture has two projects, one depending on the other,
   so this shows little about heavier parallelism; the logger sorts the
   entries to keep the file independent of the order of the events.
8. **On the `lcart` side** (WSL2, `lcart` at the commit above):
   - A database with Windows paths is unusable as it is: `Compile command not
     found`, 0 of 2 translation units, because `C:\...` is not absolute on
     Linux and `lcart` does not rewrite paths. Rewritten to `/mnt/c/...`
     it parses 2 of 2 with no diagnostics, with the real `windows.h` and MSVC
     headers read through `/mnt/c`.
   - A root on a drvfs mount (`/mnt/c/...`, file system type `v9fs` here) with
     the default `lcart-out/` fails: `preparing a fact query: database is
     locked`. The same run with `--db` on the Linux file system works.
   - The facts of a drvfs root and of a copy on the Linux file system were
     the same, line for line (sorted exports); stored paths are relative to
     the root, and `windows.h` and `cstdio` are external files.
   - Precompiled headers (`/Yc`, `/Yu`, `/Fp`) must be removed from the
     commands, or the driver looks for the `.pch` file. With them removed a
     project using a precompiled header parsed 3 of 3 translation units.
   - A Win32 (32-bit) configuration gives `i686` commands that parse in
     `clang-cl` but not in `lcart`: `winnt.h` uses MS-style inline assembly,
     which needs a registered LLVM target, and `lcart` registers none
     (ADR-0021). x64 is not affected. See the
     [open questions](../open-questions.md#open-technical-questions).

## Options considered

1. **Parse the text log** (`-v:detailed`). Tried. Relative paths, no system
   directories, localized text, and one command appears twice (the tracker's
   line repeats it). It would need the logger's knowledge anyway.
2. **An MSBuild logger** that writes the database. Tried; the decision below.
3. **Read a `.binlog` with the Structured Log Viewer library.** Not tried: it
   needs the .NET SDK and NuGet packages, which a Build Tools machine does
   not have. Option 2 already replays a `.binlog` (finding 6).
4. **The ClangCL toolset with `/clang:-MJ`.** Not tried: the component "C++
   Clang tools for Windows" was not installed, and it would compile with
   clang instead of the compiler that the product is built with.
5. **A tool from outside the repository** (a Visual Studio extension or a
   third-party logger). Not tried: it adds a dependency that the repository
   does not control.

## Decision

<a id="adr0022-msbuild-logger-v1"></a>**The tested way to get a compilation
database from an MSBuild solution is the logger
`tools/msbuild-compdb/CompDbLogger.cs`.** It is compiled with the C#
compiler that ships with MSBuild (`MSBuild/Current/Bin/Roslyn/csc.exe`),
needs nothing else, and is run as `MSBuild.exe Solution.sln -t:Rebuild
-logger:LcartMsbuildCompdb.CompDbLogger,CompDbLogger.dll;out=...`. The
how-to has the commands. `lcart` itself gets no MSBuild code (ADR-0009).

<a id="adr0022-database-content-v1"></a>**Content.** One entry per source
file of each `CL` task, in the `"arguments"` form (no shell splitting,
ADR-0021): `cl.exe` in lower case, `--target=i686-pc-windows-msvc` when
the directory of the compiler (`...\bin\HostX86\x86`) says 32-bit (the
`arm` and `arm64` branches of the logger are not tried), the options as
MSBuild passed them except for the precompiled-header options (`/Yc`, `/Yu`,
`/Yl`, `/Fp`) and the mapped paths, then one `/imsvc <dir>` for each
existing directory of `ExternalIncludePath` and `IncludePath`, then the
absolute source file. `directory` is the project directory. Entries are
unique and sorted, so that `-m` does not change the file.

<a id="adr0022-path-mapping-v1"></a>**Paths are rewritten by the logger, not
by `lcart`.** The parameter `map=<from>=<to>` (repeatable; the first match
wins, so list the most specific first) replaces a path prefix in
`directory`, `file`, path-valued options and `/imsvc` directories, and turns
`\` into `/` in every mapped path. `lcart` keeps reading databases as they
are.

<a id="adr0022-wsl-analysis-v1"></a>**Analysis under WSL2.** The database
names the headers it needs (`/imsvc`), so the facts do not depend on the
shell's environment (ADR-0021). The fact database goes on the Linux file
system (`--db`): SQLite does not work on drvfs. The root may be on `/mnt/c`
or a copy on the Linux file system.

<a id="adr0022-verification-v1"></a>**Verification.** Two checks stand for
NFR-013 criterion 3. `MsbuildDatabaseTest` (CI, Linux) analyses the database
that the logger wrote on Windows (`compile_commands.golden.json`, machine
paths replaced by placeholders), with stand-ins for the Windows headers.
`tools/msbuild-compdb/verify.ps1` (Windows) rebuilds the fixture, compares
the logger's output with that golden file, checks that a parallel build is
identical, and checks the Win32 target. When the logger or the fixture
changes, run the script and commit the golden file with them.

## Consequences

- NFR-013 criterion 3 is met by a way that was tried, with its limits
  written down. The CI part cannot show that the logger still works with a
  newer MSBuild; `verify.ps1` can, on a Windows machine.
- Not tried: Visual Studio 2017 to 2019 and Community or Enterprise
  editions (only Build Tools 2022), toolsets other than `v143`, C++/CLI,
  custom build steps, a build with compile errors, `.rc` and `.idl` files,
  and the case of `#include` names against file names on drvfs.
- A Win32 solution yields a database whose translation units that include
  `windows.h` fail in `lcart` for now (finding 8). Analyse the x64
  configuration, which compiles the same sources.
- The logger needs a build, so the solution must compile on the Windows
  machine, at least up to the `CL` tasks.
- Revisit if MSBuild changes the events (`TaskCommandLineEventArgs`,
  `ProjectPropertyInstance`), or when `lcart` registers LLVM targets
  (Win32 would then be worth trying again).
