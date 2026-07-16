<#
.SYNOPSIS
Build an app (peep-optimized), profile it under ntvcm, and correlate the
result against its .PRN/.SYM listings into a hot-function summary plus
per-line annotated listings.

.DESCRIPTION
Builds <Name> peep-optimized via ma.ps1 (the real, shipped build - not a
-g debug build, which would bypass peephole optimization and speculative
register allocation and so no longer reflect real hot spots), runs it
under ntvcm's per-PC execution-count profiler (-g:<file>), regenerates
RTLMIN.PRN (a normal build assembles RTLMIN.MAC without the /L listing
flag, since nothing else needs it), and invokes dccprof.py to correlate
the raw pc,count,asm CSV against the build's .PRN/.SYM listings.

dccprof.py's own module docstring documents the two correlation gotchas
this whole tool exists to get right once instead of by hand every time:
the app and RTLMIN.MAC are separate modules linked at addresses that
differ from each module's own standalone .PRN by a different offset, and
a .PRN listing's address column is the address AFTER each line's own
emitted bytes, not its start.

.PARAMETER Name
  App name (e.g. "tbig"). Searches tests/<name>.c by default, same as
  ma.ps1.

.PARAMETER SourcePath
  Optional explicit C source file path, forwarded to ma.ps1.

.PARAMETER BuildDir
  Build artifact directory (default: "build/dccprof/<name>").

.PARAMETER OutDir
  Where to write the report (default: same as BuildDir).

.PARAMETER Clock
  ntvcm clock speed in Hz for -s: (default: 0, as fast as possible -
  profiling counts executions, not wall time, so this only affects how
  long the run takes, not the results).

.PARAMETER ProgramArgs
  Everything after this is passed as argv to the profiled program itself.

.EXAMPLE
  pwsh ./scripts/dccprof.ps1 tbig
  pwsh ./scripts/dccprof.ps1 tbig -ProgramArgs 20000
  pwsh ./scripts/dccprof.ps1 mm -BuildDir /tmp/profmm

.NOTES
  Environment Variables:
    M80C     native assembler used to regenerate RTLMIN.PRN (default: "m80c")
    NTVCM    emulator (default: "ntvcm")
    PYTHON   Python launcher used for dccprof.py (default: "python3", falling
             back to "python" if python3 is not found)
#>

param(
    [Parameter(Position = 0)]
    [string]$Name,

    [string]$SourcePath = "",
    [string]$BuildDir = "",
    [string]$OutDir = "",
    [string]$Clock = "0",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ProgramArgs,
    [Alias("h")]
    [switch]$Help
)

function Show-DccProfHelp {
    @'
usage: dccprof.ps1 <name> [-SourcePath FILE] [-BuildDir DIR] [-OutDir DIR] [-Clock HZ] [-ProgramArgs ...]
    dccprof.ps1 -Help

Builds <name> peep-optimized, profiles it under ntvcm, and writes a
hot-function summary plus per-line annotated listings.

outputs (in -OutDir, default same as -BuildDir):
    <name>_profile_summary.md   ranked hot-function table
    <name>_profile_app.txt      the app's own .MAC, hit count + opcode bytes per line
    <name>_profile_rtl.txt      same, for RTL routines that were hit
    <name>_profile_app.html     same as _app.txt, with hit counts color-coded (open in a browser)
    <name>_profile_rtl.html     same as _rtl.txt, with hit counts color-coded (open in a browser)

examples:
    dccprof.ps1 tbig
    dccprof.ps1 tbig -ProgramArgs 20000
    dccprof.ps1 mm -BuildDir build/profmm
'@
}

if ($Help -or -not $Name) {
    Show-DccProfHelp
    if ($Help) { exit 0 }
    Write-Error "missing <name>"
    exit 1
}

$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).ProviderPath
Set-Location $repoRoot

$lowerBase = $Name.ToLowerInvariant()
$upperBase = $Name.ToUpperInvariant()

if (-not $BuildDir) { $BuildDir = Join-Path "build" (Join-Path "dccprof" $lowerBase) }
if (-not $OutDir) { $OutDir = $BuildDir }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$M80C = ($env:M80C -replace '^\s+|\s+$', '')
if (-not $M80C) { $M80C = "m80c" }
$NTVCM = ($env:NTVCM -replace '^\s+|\s+$', '')
if (-not $NTVCM) { $NTVCM = "ntvcm" }
$PYTHON = ($env:PYTHON -replace '^\s+|\s+$', '')
if (-not $PYTHON) {
    $PYTHON = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { "python" }
}

Write-Host "=== building $Name (peep-optimized) into $BuildDir ==="
# All-named, not positional: splatting an array that mixes a positional
# "fast" with "-BuildDir"/value pairs confuses ma.ps1's parameter binder
# (observed directly - "A positional parameter cannot be found that
# accepts argument '-BuildDir'" - even though -Mode's own ValidateSet
# accepts "fast" fine when passed positionally on its own).
$maArgs = @{ Name = $Name; Mode = "fast"; BuildDir = $BuildDir }
if ($SourcePath) { $maArgs["SourcePath"] = $SourcePath }
& (Join-Path $scriptDir "ma.ps1") @maArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "build failed (ma.ps1 exited $LASTEXITCODE)"
    exit 1
}

Write-Host "=== regenerating RTLMIN.PRN (not produced by a normal build) ==="
Push-Location $BuildDir
try {
    & $M80C "=RTLMIN.MAC" "/X" "/O" "/Z" "/L"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "RTLMIN.PRN regeneration failed (m80c exited $LASTEXITCODE)"
        exit 1
    }
} finally {
    Pop-Location
}

$profileCsvName = "${lowerBase}_profile.csv"
$profileCsv = Join-Path $BuildDir $profileCsvName
if (Test-Path -LiteralPath $profileCsv) { Remove-Item -LiteralPath $profileCsv }

Write-Host "=== running $lowerBase under the profiler ==="
Push-Location $BuildDir
try {
    # ma.ps1 (unlike ma.sh) only ever produces the upper-case .COM - no
    # lower-case copy - so reference it directly rather than assuming one.
    $ntvcmArgs = @("-p", "-s:$Clock", "-g:$profileCsvName", "${upperBase}.COM")
    if ($ProgramArgs) { $ntvcmArgs += $ProgramArgs }
    & $NTVCM @ntvcmArgs
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $profileCsv) -or (Get-Item -LiteralPath $profileCsv).Length -eq 0) {
    Write-Error "ntvcm did not produce a profile CSV at $profileCsv (the run may have crashed or been interrupted before exit)"
    exit 1
}

Write-Host "=== correlating profile against listings ==="
& $PYTHON (Join-Path $scriptDir "dccprof.py") `
    --app $lowerBase --build-dir $BuildDir --profile-csv $profileCsv --out-dir $OutDir
if ($LASTEXITCODE -ne 0) {
    Write-Error "dccprof.py failed (exited $LASTEXITCODE)"
    exit 1
}

Write-Host ""
Write-Host "done: open $(Join-Path $OutDir "${lowerBase}_profile_summary.md")"
