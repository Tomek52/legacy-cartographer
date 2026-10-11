# msbuild-compdb

An MSBuild logger that writes `compile_commands.json` for `lcart` from the
`CL` tasks of a Visual Studio / MSBuild build
([ADR-0022](../../docs/adr/0022-msbuild-logger-for-compilation-databases.md)).
Windows only, no dependency beyond MSBuild itself.

- `CompDbLogger.cs`: the logger. Compile it with the C# compiler that ships
  with MSBuild (`MSBuild\Current\Bin\Roslyn\csc.exe`).
- `verify.ps1`: compiles it, builds
  `tests/fixtures/msbuild_solution` in a temporary copy, and compares the
  result with the golden file. Run it after any change:
  `powershell -ExecutionPolicy Bypass -File tools\msbuild-compdb\verify.ps1`.

How to use it, parameters and limits:
[Analysing an MSVC code base under WSL2](../../docs/how-to/msvc-under-wsl2.md#2-get-a-compilation-database-from-an-msbuild-solution).
