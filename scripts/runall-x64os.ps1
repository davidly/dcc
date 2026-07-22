#Requires -Version 7
<#
.SYNOPSIS
Alternative test case for x64os: runs the real dcc/dccpeep/dccrtlstrip/m80c
pipeline for every single-file test app twice - once with each tool invoked
directly, once with each tool invoked through x64os - and verifies the two
runs produce byte-identical output.

.DESCRIPTION
This is not a variant of runall.ps1's own app-behavior verification (it
never links a final .COM or runs one under ntvcm/l80 - dccmake, l80 and
ntvcm are intentionally not part of this pipeline). It exists to validate
x64os (~/github/x64os) itself, using dcc's real-world test corpus as a
large, varied source of inputs to the four tools it wraps.

For each tests/*.c file not marked "ignore" in tests/_test_overrides.json,
runs:
    dcc         <src.c> -o OUT.MAC
    dccpeep     OUT.MAC OUT_PEEP.MAC
    dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC OUT_PEEP.MAC
    m80c        =OUT_PEEP.MAC /X /O /Z /L
once with each tool run natively, and once with each tool run as
"x64os <tool> <args>" instead - same inputs, same working directory layout,
same flags either way. The two runs' OUT.MAC, OUT_PEEP.MAC, RTLMIN.MAC and
OUT_PEEP.REL are then compared byte-for-byte; any difference is a real x64os
emulation bug, since the tool being emulated is identical in both runs.

dccmake itself is not part of the per-app pipeline (driving these four tools
directly doesn't need it), but this script separately runs one dccmake
build under x64os as an informational smoke test after the main suite. As
of this writing dccmake's own child-process spawning relies on the "clone3"
syscall, which x64os does not implement, so that smoke test is EXPECTED to
fail - it is reported on its own line and never affects the app pass/fail
count or this script's exit code.

.PARAMETER X64Os
  Path to the x64os emulator binary. Defaults to $env:X64OS, then
  ../x64os/bin/x64os relative to this repo (the conventional sibling-repo
  checkout layout), then a bare "x64os" lookup on PATH.

.PARAMETER TestDir
  Directory of single-file test sources (default: "tests").

.PARAMETER BuildDir
  Working directory for build artifacts (default: "build/x64os"). Removed
  and recreated at the start of each run; per-app subdirectories are removed
  again after a passing app to keep disk usage bounded during a full run.

.PARAMETER Filter
  Only run apps whose name matches this wildcard (e.g. "t*"). Default: all.

.PARAMETER Serial
  Run apps sequentially instead of in parallel (default: parallel, like
  runall.ps1).

.PARAMETER ThrottleLimit
  Max concurrent apps in parallel mode (default: CPU core count).

.PARAMETER NoStackCheck
  Build without -fstack-check (default: on, matching runall.ps1's default).
  Doesn't affect x64os validation either way; kept for parity with a normal
  build.

.PARAMETER NoDccMakeSmokeTest
  Skip the informational dccmake-under-x64os smoke test at the end.

.PARAMETER Help
  Show this help text and exit.
#>

param(
    [string]$X64Os = "",
    [string]$TestDir = "tests",
    [string]$BuildDir = "build/x64os",
    [string]$Filter = "*",
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [switch]$NoStackCheck,
    [switch]$NoDccMakeSmokeTest,
    [switch]$Help
)

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    return
}

$ErrorActionPreference = "Stop"
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
Set-Location $script:RepoRoot

$Parallel = -not $Serial
$StackCheck = -not $NoStackCheck

function Resolve-ToolCommand {
    param([string]$EnvVarName, [string]$LocalName)

    $envVal = ((Get-Item "env:$EnvVarName" -ErrorAction SilentlyContinue).Value) -replace '^\s+|\s+$', ''
    if ($envVal) { return $envVal }
    $local = Join-Path $script:RepoRoot ($(if ($IsWindows) { "$LocalName.exe" } else { $LocalName }))
    if (Test-Path -LiteralPath $local -PathType Leaf) { return $local }
    return $LocalName
}

function Resolve-X64Os {
    param([string]$Explicit)

    if ($Explicit) { return $Explicit }
    if ($env:X64OS) { return $env:X64OS }
    $sibling = Join-Path (Split-Path $script:RepoRoot -Parent) "x64os/bin/x64os"
    if (Test-Path -LiteralPath $sibling -PathType Leaf) { return $sibling }
    return "x64os"
}

$script:Dcc = Resolve-ToolCommand -EnvVarName "DCC" -LocalName "dcc"
$script:DccPeep = Resolve-ToolCommand -EnvVarName "DCCPEEP" -LocalName "dccpeep"
$script:DccRtlStrip = Resolve-ToolCommand -EnvVarName "DCCRTLSTRIP" -LocalName "dccrtlstrip"
$script:M80C = Resolve-ToolCommand -EnvVarName "M80C" -LocalName "m80c"
$script:DccMake = Resolve-ToolCommand -EnvVarName "DCCMAKE" -LocalName "dccmake"
$script:X64OsPath = Resolve-X64Os -Explicit $X64Os

foreach ($tool in @($script:Dcc, $script:DccPeep, $script:DccRtlStrip, $script:M80C)) {
    if (($tool -match '[\\/]') -and -not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        Write-Error "Tool not found: $tool" -ErrorAction Stop
    }
}
if (($script:X64OsPath -match '[\\/]') -and -not (Test-Path -LiteralPath $script:X64OsPath -PathType Leaf)) {
    Write-Error "x64os emulator not found: $script:X64OsPath (pass -X64Os, or set `$env:X64OS)" -ErrorAction Stop
}

$script:DccRtlSrc = Join-Path $script:RepoRoot "DCCRTL.MAC"
if (-not (Test-Path -LiteralPath $script:DccRtlSrc -PathType Leaf)) {
    Write-Error "runtime not found: $script:DccRtlSrc" -ErrorAction Stop
}

# ---- test overrides: only the fields relevant to a compile-only pipeline
# (this script never runs the resulting .COM, so "args"/"stdin"/"fixtures"
# don't apply) ----
$appOverridesPath = Join-Path $TestDir "_test_overrides.json"
$appOverrides = @{}
if (Test-Path -LiteralPath $appOverridesPath) {
    $config = Get-Content $appOverridesPath | ConvertFrom-Json
    foreach ($app in $config.apps) {
        $appOverrides[$app.name] = @{}
        if ($app.stack_size) { $appOverrides[$app.name]['stack_size'] = $app.stack_size }
        if ($app.dcc_args) { $appOverrides[$app.name]['dcc_args'] = $app.dcc_args }
        if ($null -ne $app.dcc_floatio) { $appOverrides[$app.name]['dcc_floatio'] = $app.dcc_floatio }
        if ($null -ne $app.dcc_longio) { $appOverrides[$app.name]['dcc_longio'] = $app.dcc_longio }
        if ($app.ignore) { $appOverrides[$app.name]['ignore'] = $app.ignore }
    }
}

function Get-OverrideStackSize {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['stack_size']) { return [int]$appOverrides[$app]['stack_size'] }
    return 512
}
function Get-OverrideDccArgs {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['dcc_args']) { return $appOverrides[$app]['dcc_args'] }
    return ""
}
function Get-OverrideDccFloatio {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and ($null -ne $appOverrides[$app]['dcc_floatio'])) { return [bool]$appOverrides[$app]['dcc_floatio'] }
    return $null
}
function Get-OverrideDccLongio {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and ($null -ne $appOverrides[$app]['dcc_longio'])) { return [bool]$appOverrides[$app]['dcc_longio'] }
    return $null
}
function Get-OverrideIgnore {
    param([string]$app)
    return ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['ignore'])
}

# Apps whose compiled output is intentionally non-reproducible across two
# separate compiler invocations, regardless of x64os - a byte-for-byte
# native-vs-emulated compare would flag them as "different" even between two
# purely native runs a second apart. Not part of tests/_test_overrides.json
# (that file's "ignore" is about the main app-behavior suite; this list is
# specific to this script's own byte-comparison methodology).
$script:NonDeterministicApps = @(
    "tstdc"  # embeds __DATE__/__TIME__ (the real wall-clock compile time) into its output
)

$testFiles = @(Get-ChildItem -Path (Join-Path $TestDir "*.c") -File | Where-Object { $_.BaseName -like $Filter } | ForEach-Object { $_.BaseName } | Sort-Object)
$testFiles = @($testFiles | Where-Object { -not (Get-OverrideIgnore $_) -and ($script:NonDeterministicApps -notcontains $_) })
if ($testFiles.Count -eq 0) {
    Write-Error "No test files found in $TestDir matching '$Filter'" -ErrorAction Stop
}

Write-Host "x64os      : $script:X64OsPath" -ForegroundColor Cyan
Write-Host "dcc        : $script:Dcc" -ForegroundColor Cyan
Write-Host "dccpeep    : $script:DccPeep" -ForegroundColor Cyan
Write-Host "dccrtlstrip: $script:DccRtlStrip" -ForegroundColor Cyan
Write-Host "m80c       : $script:M80C" -ForegroundColor Cyan
Write-Host "Found $($testFiles.Count) single-file test apps" -ForegroundColor Cyan

if (Test-Path -LiteralPath $BuildDir) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

# Runs one command either directly or, when -UseX64Os is set, as
# "<X64OsPath> <Exe> <ExeArgs...>" instead - same working directory either
# way, so a tool that resolves relative filenames against its cwd (m80c)
# behaves identically in both runs.
function Invoke-Wrapped {
    param(
        [string]$Exe,
        [string[]]$ExeArgs,
        [string]$WorkingDirectory = "",
        [bool]$UseX64Os,
        [string]$X64OsPath
    )

    $realExe = $Exe
    $realArgs = $ExeArgs
    if ($UseX64Os) {
        $realArgs = @($Exe) + $ExeArgs
        $realExe = $X64OsPath
    }
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $realExe
    foreach ($a in $realArgs) { [void]$psi.ArgumentList.Add($a) }
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    return [pscustomobject]@{ ExitCode = $proc.ExitCode; StdOut = $stdout; StdErr = $stderr }
}

# Runs dcc -> dccpeep -> dccrtlstrip -> m80c once in $WorkDir, either
# natively or fully through x64os, and returns which output files were
# produced (dcc's own exit code isn't reliable - like dccmake, this checks
# for the expected output file after each step instead).
function Invoke-ToolPipeline {
    param(
        [string]$WorkDir,
        [string]$SourceFile,
        [int]$StackSize,
        [string]$DccArgsExtra,
        [object]$Floatio,
        [object]$Longio,
        [bool]$StackCheck,
        [bool]$UseX64Os,
        [string]$X64OsPath,
        [string]$Dcc,
        [string]$DccPeep,
        [string]$DccRtlStrip,
        [string]$M80C,
        [string]$RepoRoot,
        [string]$RtlSrc
    )

    New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
    $outMac = Join-Path $WorkDir "OUT.MAC"
    $peepMac = Join-Path $WorkDir "OUT_PEEP.MAC"
    $rtlMac = Join-Path $WorkDir "DCCRTL.MAC"
    $rtlMin = Join-Path $WorkDir "RTLMIN.MAC"
    $relFile = Join-Path $WorkDir "OUT_PEEP.REL"

    $dccArgs = New-Object 'System.Collections.Generic.List[string]'
    if ($StackCheck) { $dccArgs.Add("-fstack-check") }
    $dccArgs.Add("-stack"); $dccArgs.Add("$StackSize")
    if ($null -ne $Floatio) { $dccArgs.Add($(if ($Floatio) { "-ffloatio" } else { "-fno-floatio" })) }
    if ($null -ne $Longio) { $dccArgs.Add($(if ($Longio) { "-flongio" } else { "-fno-longio" })) }
    $dccArgs.Add("-I"); $dccArgs.Add($RepoRoot)
    if ($DccArgsExtra) { foreach ($a in ($DccArgsExtra -split '\s+' | Where-Object { $_ })) { $dccArgs.Add($a) } }
    $dccArgs.Add($SourceFile); $dccArgs.Add("-o"); $dccArgs.Add($outMac)

    $r = Invoke-Wrapped -Exe $Dcc -ExeArgs $dccArgs -UseX64Os $UseX64Os -X64OsPath $X64OsPath
    if (-not (Test-Path -LiteralPath $outMac -PathType Leaf)) {
        return [pscustomobject]@{ Ok = $false; Step = "dcc"; StdOut = $r.StdOut; StdErr = $r.StdErr; Outputs = @{} }
    }

    $r = Invoke-Wrapped -Exe $DccPeep -ExeArgs @($outMac, $peepMac) -UseX64Os $UseX64Os -X64OsPath $X64OsPath
    if (-not (Test-Path -LiteralPath $peepMac -PathType Leaf)) {
        return [pscustomobject]@{ Ok = $false; Step = "dccpeep"; StdOut = $r.StdOut; StdErr = $r.StdErr; Outputs = @{ "OUT.MAC" = $outMac } }
    }

    Copy-Item -LiteralPath $RtlSrc -Destination $rtlMac -Force
    $stripArgs = New-Object 'System.Collections.Generic.List[string]'
    if ($Floatio -eq $true) { $stripArgs.Add("-k"); $stripArgs.Add("_pffio") }
    if ($Longio -eq $true) { $stripArgs.Add("-k"); $stripArgs.Add("_pflng") }
    $stripArgs.Add("-r"); $stripArgs.Add($rtlMac); $stripArgs.Add("-o"); $stripArgs.Add($rtlMin); $stripArgs.Add($peepMac)
    $r = Invoke-Wrapped -Exe $DccRtlStrip -ExeArgs $stripArgs -UseX64Os $UseX64Os -X64OsPath $X64OsPath
    if (-not (Test-Path -LiteralPath $rtlMin -PathType Leaf)) {
        return [pscustomobject]@{ Ok = $false; Step = "dccrtlstrip"; StdOut = $r.StdOut; StdErr = $r.StdErr; Outputs = @{ "OUT.MAC" = $outMac; "OUT_PEEP.MAC" = $peepMac } }
    }

    $r = Invoke-Wrapped -Exe $M80C -ExeArgs @("=OUT_PEEP.MAC", "/X", "/O", "/Z", "/L") -WorkingDirectory $WorkDir -UseX64Os $UseX64Os -X64OsPath $X64OsPath
    if (-not (Test-Path -LiteralPath $relFile -PathType Leaf)) {
        return [pscustomobject]@{ Ok = $false; Step = "m80c"; StdOut = $r.StdOut; StdErr = $r.StdErr; Outputs = @{ "OUT.MAC" = $outMac; "OUT_PEEP.MAC" = $peepMac; "RTLMIN.MAC" = $rtlMin } }
    }

    return [pscustomobject]@{ Ok = $true; Step = ""; StdOut = ""; StdErr = ""; Outputs = @{ "OUT.MAC" = $outMac; "OUT_PEEP.MAC" = $peepMac; "RTLMIN.MAC" = $rtlMin; "OUT_PEEP.REL" = $relFile } }
}

# Runs the pipeline natively and under x64os for one app, then compares
# every output file byte-for-byte. A native-side failure is reported
# separately from an x64os-side failure/mismatch, since only the latter is
# an x64os bug - the former just means this app isn't a usable case (e.g.
# missing include, needs a larger stack) for either run.
function Invoke-X64OsAppTest {
    param(
        [string]$App,
        [string]$SourceFile,
        [string]$AppBuildDir,
        [int]$StackSize,
        [string]$DccArgsExtra,
        [object]$Floatio,
        [object]$Longio,
        [bool]$StackCheck,
        [string]$X64OsPath,
        [string]$Dcc, [string]$DccPeep, [string]$DccRtlStrip, [string]$M80C,
        [string]$RepoRoot, [string]$RtlSrc
    )

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $nativeDir = Join-Path $AppBuildDir "native"
    $emuDir = Join-Path $AppBuildDir "emu"

    $native = Invoke-ToolPipeline -WorkDir $nativeDir -SourceFile $SourceFile -StackSize $StackSize `
        -DccArgsExtra $DccArgsExtra -Floatio $Floatio -Longio $Longio -StackCheck $StackCheck `
        -UseX64Os $false -X64OsPath $X64OsPath -Dcc $Dcc -DccPeep $DccPeep -DccRtlStrip $DccRtlStrip -M80C $M80C `
        -RepoRoot $RepoRoot -RtlSrc $RtlSrc

    if (-not $native.Ok) {
        $sw.Stop()
        return [pscustomobject]@{ App = $App; Passed = $false; Reason = "native $($native.Step) failed (not an x64os issue)"; Detail = ($native.StdErr + $native.StdOut).Trim(); Elapsed = $sw.Elapsed }
    }

    $emu = Invoke-ToolPipeline -WorkDir $emuDir -SourceFile $SourceFile -StackSize $StackSize `
        -DccArgsExtra $DccArgsExtra -Floatio $Floatio -Longio $Longio -StackCheck $StackCheck `
        -UseX64Os $true -X64OsPath $X64OsPath -Dcc $Dcc -DccPeep $DccPeep -DccRtlStrip $DccRtlStrip -M80C $M80C `
        -RepoRoot $RepoRoot -RtlSrc $RtlSrc

    if (-not $emu.Ok) {
        $sw.Stop()
        return [pscustomobject]@{ App = $App; Passed = $false; Reason = "x64os $($emu.Step) failed to produce output"; Detail = ($emu.StdErr + $emu.StdOut).Trim(); Elapsed = $sw.Elapsed }
    }

    foreach ($name in $native.Outputs.Keys) {
        $nativeBytes = [System.IO.File]::ReadAllBytes($native.Outputs[$name])
        $emuBytes = [System.IO.File]::ReadAllBytes($emu.Outputs[$name])
        if (-not [System.Linq.Enumerable]::SequenceEqual($nativeBytes, $emuBytes)) {
            $sw.Stop()
            return [pscustomobject]@{ App = $App; Passed = $false; Reason = "$name differs between native and x64os run"; Detail = ""; Elapsed = $sw.Elapsed }
        }
    }

    Remove-Item -LiteralPath $AppBuildDir -Recurse -Force -ErrorAction SilentlyContinue
    $sw.Stop()
    return [pscustomobject]@{ App = $App; Passed = $true; Reason = ""; Detail = ""; Elapsed = $sw.Elapsed }
}

$workItems = @($testFiles | ForEach-Object {
    $app = $_
    [pscustomobject]@{
        App = $app
        SourceFile = (Resolve-Path -LiteralPath (Join-Path $TestDir "$app.c")).ProviderPath
        StackSize = Get-OverrideStackSize $app
        DccArgsExtra = Get-OverrideDccArgs $app
        Floatio = Get-OverrideDccFloatio $app
        Longio = Get-OverrideDccLongio $app
    }
})

$results = @()
$totalToRun = $workItems.Count
$done = 0

if ($Parallel) {
    $repoRoot = $script:RepoRoot
    $iwDef = ${function:Invoke-Wrapped}.ToString()
    $itpDef = ${function:Invoke-ToolPipeline}.ToString()
    $ixatDef = ${function:Invoke-X64OsAppTest}.ToString()
    $x64OsPath = $script:X64OsPath
    $dcc = $script:Dcc; $dccPeep = $script:DccPeep; $dccRtlStrip = $script:DccRtlStrip; $m80c = $script:M80C
    $rtlSrc = $script:DccRtlSrc
    $stackCheckOn = [bool]$StackCheck
    $buildDirFull = (Resolve-Path -LiteralPath $BuildDir).ProviderPath

    $workItems | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        $item = $_
        Set-Location $using:repoRoot
        ${function:Invoke-Wrapped} = $using:iwDef
        ${function:Invoke-ToolPipeline} = $using:itpDef
        ${function:Invoke-X64OsAppTest} = $using:ixatDef

        $appBuildDir = Join-Path $using:buildDirFull $item.App
        Invoke-X64OsAppTest -App $item.App -SourceFile $item.SourceFile -AppBuildDir $appBuildDir `
            -StackSize $item.StackSize -DccArgsExtra $item.DccArgsExtra -Floatio $item.Floatio -Longio $item.Longio `
            -StackCheck $using:stackCheckOn -X64OsPath $using:x64OsPath `
            -Dcc $using:dcc -DccPeep $using:dccPeep -DccRtlStrip $using:dccRtlStrip -M80C $using:m80c `
            -RepoRoot $using:repoRoot -RtlSrc $using:rtlSrc
    } | ForEach-Object {
        $result = $_
        $results += $result
        $done++
        $elapsedStr = "{0:0.00}s" -f $result.Elapsed.TotalSeconds
        $counter = "[{0,3}/{1}]" -f $done, $totalToRun
        $status = if ($result.Passed) { "PASS" } else { "FAIL" }
        $line = "{0} {1}  {2,-14} {3,8}" -f $counter, $status, $result.App, $elapsedStr
        if ($result.Passed) {
            Write-Host $line -ForegroundColor Green
        } else {
            Write-Host $line -ForegroundColor Red
            Write-Host "         $($result.Reason)" -ForegroundColor Red
            if ($result.Detail) { Write-Host "         $($result.Detail)" -ForegroundColor DarkRed }
        }
    }
} else {
    $buildDirFull = (Resolve-Path -LiteralPath $BuildDir).ProviderPath
    foreach ($item in $workItems) {
        $appBuildDir = Join-Path $buildDirFull $item.App
        $result = Invoke-X64OsAppTest -App $item.App -SourceFile $item.SourceFile -AppBuildDir $appBuildDir `
            -StackSize $item.StackSize -DccArgsExtra $item.DccArgsExtra -Floatio $item.Floatio -Longio $item.Longio `
            -StackCheck $StackCheck -X64OsPath $script:X64OsPath `
            -Dcc $script:Dcc -DccPeep $script:DccPeep -DccRtlStrip $script:DccRtlStrip -M80C $script:M80C `
            -RepoRoot $script:RepoRoot -RtlSrc $script:DccRtlSrc
        $results += $result
        $done++
        $elapsedStr = "{0:0.00}s" -f $result.Elapsed.TotalSeconds
        $counter = "[{0,3}/{1}]" -f $done, $totalToRun
        $status = if ($result.Passed) { "PASS" } else { "FAIL" }
        $line = "{0} {1}  {2,-14} {3,8}" -f $counter, $status, $result.App, $elapsedStr
        if ($result.Passed) {
            Write-Host $line -ForegroundColor Green
        } else {
            Write-Host $line -ForegroundColor Red
            Write-Host "         $($result.Reason)" -ForegroundColor Red
            if ($result.Detail) { Write-Host "         $($result.Detail)" -ForegroundColor DarkRed }
        }
    }
}

$failed = @($results | Where-Object { -not $_.Passed })
$passed = @($results | Where-Object { $_.Passed })

Write-Host ""
Write-Host "=== x64os tool-pipeline results: $($passed.Count)/$($results.Count) passed ===" -ForegroundColor Cyan
if ($failed.Count -gt 0) {
    Write-Host "Failed apps:" -ForegroundColor Red
    foreach ($f in $failed) {
        Write-Host "  $($f.App): $($f.Reason)" -ForegroundColor Red
    }
}

# ---- informational dccmake-under-x64os smoke test ----
if (-not $NoDccMakeSmokeTest) {
    Write-Host ""
    Write-Host "--- dccmake-under-x64os smoke test (informational; not counted above) ---" -ForegroundColor Cyan
    $smokeDir = Join-Path $BuildDir "_dccmake_smoke"
    New-Item -ItemType Directory -Path $smokeDir -Force | Out-Null
    $smokeSrc = Join-Path $smokeDir "SMOKE.c"
    Copy-Item -LiteralPath (Join-Path $script:RepoRoot "tests/tprintf.c") -Destination $smokeSrc -Force

    $smokeArgs = @(
        "dcc-input=SMOKE.c",
        "dcc-runtime=$($script:DccRtlSrc)",
        "dcc-include=$($script:RepoRoot)",
        "dcc-tool=$($script:Dcc)",
        "dccpeep-tool=$($script:DccPeep)",
        "dccrtlstrip-tool=$($script:DccRtlStrip)",
        "m80c-tool=$($script:M80C)"
    )
    $r = Invoke-Wrapped -Exe $script:DccMake -ExeArgs $smokeArgs -WorkingDirectory $smokeDir -UseX64Os $true -X64OsPath $script:X64OsPath
    $combined = ($r.StdOut + $r.StdErr)
    if ($r.ExitCode -eq 0) {
        Write-Host "dccmake ran successfully under x64os (exit 0) - x64os may have gained clone3 support since this was last written." -ForegroundColor Green
    } elseif ($combined -match "clone3|X64 mapping for syscall 435") {
        Write-Host "dccmake failed under x64os as expected: its system()-based child spawning needs the unimplemented clone3 syscall." -ForegroundColor Yellow
    } else {
        Write-Host "dccmake failed under x64os for a DIFFERENT reason than the known clone3 gap - worth a look:" -ForegroundColor Red
        Write-Host $combined -ForegroundColor DarkRed
    }
}

if ($failed.Count -gt 0) { exit 1 }
exit 0
