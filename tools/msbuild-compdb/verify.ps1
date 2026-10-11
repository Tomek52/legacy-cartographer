<#
.SYNOPSIS
  Builds CompDbLogger.cs and checks on a Windows machine that it still writes
  the database in tests/fixtures/msbuild_solution/compile_commands.golden.json
  (NFR-013 criterion 3, ADR-0022).

.DESCRIPTION
  Needs Visual Studio or Build Tools 2022 with the C++ workload (MSBuild, MSVC,
  Windows SDK). Nothing is installed and nothing in the repository is changed:
  the fixture is copied to a temporary directory and built there.

  Checks:
   1. the logger compiles with the Roslyn compiler that ships with MSBuild;
   2. -t:Rebuild of the fixture solution writes one entry per source file;
   3. after replacing this machine's paths with the placeholders of the golden
      file, directory, file and arguments equal the golden file's, except for
      the /imsvc directories (they depend on the installed components): the
      MSVC include directory and the SDK ucrt, um and shared directories must
      be there;
   4. a parallel build (-m:4) writes a byte-identical file (NFR-004);
   5. a Release|Win32 build names --target=i686-pc-windows-msvc.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\msbuild-compdb\verify.ps1
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$golden = Join-Path $repo 'tests\fixtures\msbuild_solution\compile_commands.golden.json'
$logger = Join-Path $PSScriptRoot 'CompDbLogger.cs'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio or Build Tools 2022 with the C++ workload.' }
$vs = & $vswhere -products * -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) { throw 'No Visual Studio with MSBuild found.' }
$bin = Join-Path $vs 'MSBuild\Current\Bin'
$msbuild = Join-Path $bin 'MSBuild.exe'
$csc = Join-Path $bin 'Roslyn\csc.exe'

$work = Join-Path ([IO.Path]::GetTempPath()) ('lcart-msbuild-compdb-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
try {
    # 1. compile the logger
    $dll = Join-Path $work 'CompDbLogger.dll'
    & $csc -nologo -target:library -warn:4 -warnaserror "-out:$dll" "-r:$bin\Microsoft.Build.Framework.dll" $logger
    if ($LASTEXITCODE -ne 0) { throw 'csc failed' }

    # The fixture is built in a copy: MSBuild writes x64\ next to the projects.
    $solution = Join-Path $work 'solution'
    Copy-Item (Join-Path $repo 'tests\fixtures\msbuild_solution') $solution -Recurse
    Remove-Item (Join-Path $solution 'compile_commands.golden.json')

    # Where this machine keeps the headers: from the evaluated IncludePath.
    # One -getProperty prints the value itself, not JSON.
    $includePath = (& $msbuild (Join-Path $solution 'core\core.vcxproj') -p:Configuration=Debug -p:Platform=x64 -getProperty:IncludePath -nologo | Out-String).Trim()
    $dirs = $includePath -split ';' | Where-Object { $_ }
    $um = $dirs | Where-Object { $_ -match '\\Windows Kits\\10\\Include\\[^\\]+\\um$' } | Select-Object -First 1
    $msvcInclude = $dirs | Where-Object { $_ -match '\\VC\\Tools\\MSVC\\[^\\]+\\include$' } | Select-Object -First 1
    if (-not $um -or -not $msvcInclude) { throw "IncludePath has no Windows SDK 'um' or MSVC 'include' directory: $includePath" }
    $sdk = Split-Path $um -Parent
    $msvc = Split-Path $msvcInclude -Parent

    # Most specific prefix first: the first map that matches is used.
    $maps = "map=$solution=@ROOT@|map=$msvc=@MSVC@|map=$sdk=@SDK@|map=$vs=@VS@"

    function Invoke-Build([string]$out, [string[]]$extra) {
        & $msbuild (Join-Path $solution 'Fixture.sln') -p:Configuration=Debug -p:Platform=x64 -t:Rebuild -nologo -v:q @extra `
            "-logger:LcartMsbuildCompdb.CompDbLogger,$dll;out=$out|$maps" 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($extra)" }
    }

    # 2. serial build
    $serial = Join-Path $work 'serial.json'
    Invoke-Build $serial @()
    # 4. parallel build
    $parallel = Join-Path $work 'parallel.json'
    Invoke-Build $parallel @('-m:4')

    # 3. compare with the golden file
    # Windows PowerShell 5.1 returns a JSON array as one object: flatten it.
    function Read-Db([string]$path) {
        $parsed = Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
        $parsed | ForEach-Object { $_ }
    }
    function Split-Arguments($entry) {
        $rest = New-Object System.Collections.Generic.List[string]
        $system = New-Object System.Collections.Generic.List[string]
        $a = @($entry.arguments)
        for ($i = 0; $i -lt $a.Count; $i++) {
            if ($a[$i] -ceq '/imsvc') { $system.Add($a[++$i]) } else { $rest.Add($a[$i]) }
        }
        [pscustomobject]@{ Rest = $rest.ToArray(); System = $system.ToArray() }
    }
    $expected = @(Read-Db $golden)
    $actual = @(Read-Db $serial)
    $failures = @()
    if ($expected.Count -ne $actual.Count) { $failures += "entries: expected $($expected.Count), got $($actual.Count)" }
    for ($i = 0; $i -lt [Math]::Min($expected.Count, $actual.Count); $i++) {
        $e = $expected[$i]; $a = $actual[$i]
        if ($e.directory -cne $a.directory) { $failures += "entry ${i}: directory '$($a.directory)' != '$($e.directory)'" }
        if ($e.file -cne $a.file) { $failures += "entry ${i}: file '$($a.file)' != '$($e.file)'" }
        $es = Split-Arguments $e; $as = Split-Arguments $a
        if (($es.Rest -join "`n") -cne ($as.Rest -join "`n")) {
            $failures += "entry ${i}: arguments differ`n  expected: $($es.Rest -join ' ')`n  actual:   $($as.Rest -join ' ')"
        }
        foreach ($needed in '@MSVC@/include', '@SDK@/ucrt', '@SDK@/um', '@SDK@/shared') {
            if ($as.System -cnotcontains $needed) { $failures += "entry ${i}: no /imsvc $needed (got: $($as.System -join ', '))" }
        }
        # A path that still names this machine would be of no use elsewhere.
        if ($a.arguments -match '^[A-Za-z]:\\|\\') { $failures += "entry ${i}: backslash or drive letter left in the arguments" }
    }
    if ((Get-FileHash $serial).Hash -ne (Get-FileHash $parallel).Hash) { $failures += 'a -m:4 build wrote a different file than the serial build' }

    # 5. a Win32 (32-bit) configuration names its target; x64 needs none
    $win32 = Join-Path $work 'win32.json'
    & $msbuild (Join-Path $solution 'Fixture.sln') -p:Configuration=Release -p:Platform=Win32 -t:Rebuild -nologo -v:q `
        "-logger:LcartMsbuildCompdb.CompDbLogger,$dll;out=$win32|$maps" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'MSBuild failed (Release|Win32)' }
    foreach ($entry in @(Read-Db $win32)) {
        if (@($entry.arguments)[1] -cne '--target=i686-pc-windows-msvc') { $failures += "Win32 entry $($entry.file): no --target=i686-pc-windows-msvc after cl.exe" }
        if (@($entry.arguments) -ccontains '/D' -and @($entry.arguments) -cnotcontains 'NDEBUG') { $failures += "Win32 entry $($entry.file): Release definitions missing" }
    }

    if ($failures) {
        $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }
        throw "$($failures.Count) check(s) failed. Output kept in $work"
    }
    Write-Host "OK: $($actual.Count) entries equal the golden file; /imsvc has the MSVC and SDK directories; -m:4 output is identical; Win32 names its target."
    Remove-Item $work -Recurse -Force
}
catch {
    Write-Host "FAILED: $($_.Exception.Message)"
    Write-Host $_.InvocationInfo.PositionMessage
    Write-Host "Work directory: $work"
    exit 1
}
