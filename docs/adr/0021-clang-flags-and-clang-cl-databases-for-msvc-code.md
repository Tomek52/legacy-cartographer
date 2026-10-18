# 0021. Clang flags and clang-cl compilation databases for MSVC code

- **Status:** Accepted; the statement that lcart registers no LLVM targets (in [clang-cl commands](#adr0021-clang-cl-commands-v1)) amended by [ADR-0025](0025-llvm-targets-registered.md); the consequence on Clang configuration files resolved by [ADR-0023](0023-clang-configuration-files-ignored.md)
- **Date:** 2026-10-09
- **Requirements:** [NFR-013](../specs/requirements.md#nfr013-linux-and-wsl2-v1),
  [B-17](../plan/backlog.md#b17-msvc-code-spike-v2),
  [security T5](../security.md#security-malicious-build-input-v1)
- **Supersedes / superseded by:** — (resolves the TODO in
  [ADR-0009](0009-linux-and-wsl2-only.md#adr0009-msvc-code-via-clang-v1)
  on the exact flags and on clang-cl arguments; ADR-0009's decision stands)

## Context

ADR-0009 decided that code written for MSVC is parsed by Clang 20 in its
MSVC compatibility mode, and left open which flags that takes and what
LibTooling does with `clang-cl`-style commands in a compilation database.
The development container and CI have no Windows, MSBuild, Windows SDK or
MSVC headers, so B-17 answered it on Linux with clang 20.1.2 (Ubuntu
package `20.1.2-0ubuntu1~24.04.3`), the pinned headers in
`/usr/lib/llvm-20/include`, the LLVM source at tag `llvmorg-20.1.2`
(read, not built) and synthetic compilation databases over the
`tests/fixtures/msvc_constructs` fixture. Source references below are
to that tag.

What the spike found:

1. **The target is the switch.** With `--target=x86_64-pc-windows-msvc`
   the driver itself adds `-fms-extensions` and `-fms-compatibility`
   (`clang/lib/Driver/ToolChains/Clang.cpp` lines 7267–7276),
   `-fms-compatibility-version=` from the MSVC installation it finds, else
   19.33 (`clang/lib/Driver/ToolChains/MSVC.cpp` lines 798–805), and,
   before C++20, `-fdelayed-template-parsing` (`Clang.cpp` lines
   7429–7431). `clang-20 -### --target=x86_64-pc-windows-msvc` shows the
   triple `x86_64-pc-windows-msvc19.33.0` and all four flags. Without the
   target, the fixture fails (Clang stops after 20 errors: `__declspec` not
   enabled, `__int64` and `__forceinline` unknown); with `-fms-extensions
   -fms-compatibility` but the Linux target it parses with 6 warnings
   (`__stdcall`, `dllexport` ignored); with the target, with or without
   the explicit flags, it parses with none.
2. **clang-cl commands work as they are.** The driver takes its mode from
   the last `--driver-mode=` argument, else from the program name
   (`clang/lib/Driver/Driver.cpp` `getDriverMode`, line 6994):
   `clang-cl`, `cl` and `cl.exe` select CL mode, also behind a Windows path
   such as `C:\VS\bin\cl.exe` (on Linux the whole string is the file name
   and still ends in `cl.exe`), but `CL.EXE` does not: names are lower-cased
   only on Windows (`clang/lib/Driver/ToolChain.cpp` lines 415–430 and
   447). CL mode sets the triple to `<arch>-pc-windows-msvc`
   (`Driver.cpp` lines 1525–1534), so a clang-cl command needs no target.
   lcart analysed `clang-cl /c /nologo /W4 /DAPI_EXPORTS /I. /std:c++17
   /EHsc /Foapi.obj api.cc`, `cl.exe /c ...` and `clang --driver-mode=cl
   ...` with no diagnostics and the same calls as the `clang` driver
   command.
3. **One LibTooling adjuster mishandles cl commands.** `ClangTool` always
   appends three adjusters (`clang/lib/Tooling/Tooling.cpp` lines 485–487).
   The dependency-file one drops every argument starting with `-M` (and the
   next one after `-MT`, `-MF`, `-MQ`) unless the command line contains
   `--driver-mode=cl`; it does not look at the program name
   (`clang/lib/Tooling/ArgumentsAdjusters.cpp` lines 24–31 and 92–110).
   cl accepts `-MD`/`-MT` for `/MD`/`/MT`, so `cl.exe /c -MD x.cpp` lost
   `-MD` and `_DLL` was not defined (an `#error` in the spike). Clang's
   own JSON database plugin, used by clang-tidy and clangd, wraps the
   database in `inferTargetAndDriverMode()`, which inserts
   `--driver-mode=cl` for such program names
   (`clang/lib/Tooling/JSONCompilationDatabase.cpp` lines 165–171,
   `Tooling.cpp` line 271); lcart, which calls `loadFromFile()` directly,
   did not.
4. **Plugin options have a clang-cl form the filter missed.** In CL mode
   the values of all `/clang:<arg>` and `-clang:<arg>` arguments are parsed,
   in order, as a separate list of `clang` driver arguments (`Driver.cpp`
   lines 1456–1474; the option takes the prefixes `/` and `-` in
   `clang/Driver/Options.inc`). `clang-20 --driver-mode=cl -###
   /clang:-fplugin=/x/evil.so` produces `"-load" "/x/evil.so"` in the cc1
   command; `remove_code_loading_arguments()` did not remove it. A plain
   `-fplugin=` is ignored in CL mode ("unknown argument ignored in
   clang-cl"); `-Xclang` works in CL mode and was already filtered. In
   lcart the plugin was nevertheless **not loaded** (a shared object
   writing a marker file from a constructor, named by `/clang:-fplugin=`,
   `-clang:-fplugin=` and `/clang:-Xclang /clang:-load ...`, wrote
   nothing): only `clang::ExecuteCompilerInvocation()` calls
   `LoadRequestedPlugins()`
   (`clang/lib/FrontendTool/ExecuteCompilerInvocation.cpp` line 241), and
   LibTooling runs the action through `FrontendActionFactory::runInvocation`
   (`Tooling.cpp` lines 445–471). The filter is defence in depth, and must
   not depend on that.
5. **The command string is split the GNU way on Linux.** A `"command"`
   string is tokenized with Windows rules only when lcart itself runs on
   Windows (`JSONCompilationDatabase.cpp` lines 137–144); response files
   likewise (`clang/lib/Tooling/ExpandResponseFilesCompilationDatabase.cpp`
   lines 69–71). Backslashes in a `"command"` string are escape characters
   there; `"arguments"` arrays are taken as they are.
6. **The environment can add system headers.** For an MSVC target without
   `/winsysroot` or `/vctoolsdir`, the driver adds the directories in
   `INCLUDE` and `EXTERNAL_INCLUDE` (`MSVC.cpp` lines 693–704), and looks
   for an MSVC installation through `VCToolsInstallDir`, `VCINSTALLDIR` and
   a `cl.exe` next to a `link.exe` on `PATH`
   (`llvm/lib/WindowsDriver/MSVCPaths.cpp` lines 508–550); the found
   installation also sets the default `-fms-compatibility-version`.
7. **Delayed template parsing loses calls.** With the MSVC target before
   C++20, lcart recorded no call from the body of a function template that
   is never instantiated (`helper(1)` in such a template: recorded on the
   Linux target, with `-fno-delayed-template-parsing` and with
   `/std:c++20`; missing otherwise).

## Options considered

1. **lcart adds the MSVC flags itself** (a configuration switch that
   appends `--target=x86_64-pc-windows-msvc` and the MS flags). A database
   made for MSVC must say so anyway (its compiler is `cl.exe`, or it names
   the target), and a switch that changes how every TU is parsed is a
   second source of truth next to the database.
2. **The compilation database is the only source of compiler flags;
   lcart makes LibTooling read clang-cl commands as Clang's own tools do,
   and filters clang-cl's pass-through arguments.** No new configuration;
   behaviour matches clang-tidy and clangd on the same database.
3. **Reject clang-cl commands** and require `clang` driver commands. Every
   database produced from an MSBuild solution would need rewriting.

## Decision

Option 2.

<a id="adr0021-msvc-flags-v1"></a>**Flags for MSVC code.** A `clang`
driver command for MSVC code needs `--target=x86_64-pc-windows-msvc` (or
another `*-windows-msvc` triple); the driver then implies
`-fms-extensions`, `-fms-compatibility` and
`-fms-compatibility-version=19.33` unless an MSVC installation or an
explicit flag says otherwise. Code bases built with an older MSVC should
pass `-fms-compatibility-version=` with their compiler's version (19.29 is
Visual Studio 2019 16.11, for example), since it changes what Clang
accepts. lcart adds no MSVC flag itself: the compilation database is the
only source of compiler flags; lcart adds only what it adds to every
command (`-fsyntax-only` and the adjusters of `ClangTool`, its own
`-resource-dir`).

<a id="adr0021-clang-cl-commands-v1"></a>**clang-cl commands.**
Commands run as `clang-cl`, `cl` or `cl.exe` (lower case), or naming
`--driver-mode=cl`, are analysed in CL mode with their arguments
unchanged. lcart wraps the database in `inferTargetAndDriverMode()` after
expanding response files, so such commands carry an explicit
`--driver-mode=cl` before the adjusters run. lcart registers no LLVM
targets, so the wrapper adds only the driver mode, never a target taken
from a program name prefix (`ToolChain.cpp` line 510 checks the prefix
against the target registry).

<a id="adr0021-pass-through-filtered-v1"></a>**Pass-through arguments are
filtered.** `remove_code_loading_arguments()` also reads the values of the
`/clang:` and `-clang:` arguments, in order, as one argument list, and
removes the arguments that load code there by the same rules, whatever
the driver mode. In both lists the filter also treats `-Xpreprocessor` as a
forwarder like `-Xclang`, and removes a whole `-Wp,<a>,<b>` argument if any
of its comma-separated values is a cc1 option that loads code: the driver
copies the values of both into the cc1 command unchanged
(`Clang.cpp` line 1236; `clang-20 -### -c a.c -Wp,-load,/p/a.so` and
`clang-cl /clang:-Wp,-load,/p/a.so` both emit `"-load" "/p/a.so"`). An
`-Xclang` or `-Xpreprocessor` right before a removed argument is removed
with it, so it never forwards the argument that followed; removal is
repeated until nothing is left to remove. Each removal is a warning, as
before (security T5).

<a id="adr0021-windows-headers-v1"></a>**Windows SDK and MSVC headers**
come from the compilation database. The candidates, none tried with real
headers (TODO: verify, [open questions](../open-questions.md)): clang-cl's
`/winsysroot <dir>` (a directory laid out as `VC/Tools/MSVC/<version>` and
`Windows Kits/10`), `/vctoolsdir` with `/winsdkdir` and `/winsdkversion`,
`/imsvc <dir>` per directory; for the `clang` driver,
`-Xmicrosoft-windows-sys-root <dir>`, `-Xmicrosoft-visualc-tools-root <dir>`
(both listed in `clang/Driver/Options.inc`) or `-isystem` per directory.
With none of them, `INCLUDE`, `EXTERNAL_INCLUDE`, `VCToolsInstallDir`,
`VCINSTALLDIR` and `PATH` of the shell running lcart decide the system
headers; the how-to therefore recommends naming the headers in the
database so that the facts do not depend on the environment (NFR-004).

## Consequences

- NFR-013 criterion 1 is verified on Linux by
  `tests/analysis/msvc_code_test.cpp` (both database styles); the how-to
  [Analysing an MSVC code base under WSL2](../how-to/msvc-under-wsl2.md)
  lists the steps and what is still unverified. Criterion 3 (MSBuild)
  stays open until it is tried on Windows.
- Database authors should prefer `"arguments"` arrays, or forward slashes,
  for Windows-made databases, because of the GNU tokenizing (finding 5);
  `CL.EXE` in upper case is not recognised as cl on Linux. Both are
  documented; lcart does not rewrite commands.
- Calls in never-instantiated function templates are missing for MSVC
  targets before C++20 unless the database adds
  `-fno-delayed-template-parsing` (clang) or `/Zc:twoPhase` (clang-cl).
  Whether lcart should append it is an
  [open question](../open-questions.md#open-technical-questions): it
  changes the facts, and code that only MSVC's lenient template parsing
  accepts would then fail to parse.
- ClangTool's syntax-only adjuster removes `-c` but not `/c`, so CL-mode
  commands print the driver warning "argument unused during compilation:
  '/c'" and "1 warning generated." on standard error; it is not recorded
  as a diagnostic and does not affect the facts.
- Clang configuration files (`--config=`, and default files such as
  `<triple>-clang.cfg` next to the program named in the command,
  `Driver.cpp` lines 1240–1307) are read by the driver inside lcart and
  their arguments bypass the filter: the spike set the MSVC target from
  both. That is not specific to clang-cl and is an
  [open question](../open-questions.md#open-technical-questions) for T5.
- Revisit if LLVM changes the adjusters or the pass-through parsing, or
  when lcart registers LLVM targets (the wrapper would then add targets
  from program-name prefixes).
