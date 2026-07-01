#Requires -Version 7
<#
.SYNOPSIS
Comprehensive test suite: builds and runs all test applications with output
verification against baseline.

.DESCRIPTION
Builds all *.c files in tests/ folder using ma.ps1, executes each under the
emulator, and compares output against per-app baselines in tests/baselines/
(one <app>.txt per test). Comparison is keyed by app name, so test discovery
order does not matter. Uses tests/_test_overrides.json for test-specific arguments and
stack sizes.

Pass -Extended to also run scripts/runall-extended.ps1 after the main app suite,
verifying the imported c-testsuite single-exec corpus as part of the same run.

Supports per-test arguments (e.g., ttt with "10" as input) and custom stack
size overrides (e.g., cobint needs 1536 bytes, triangle needs 768).

Pass -Report to append per-app metrics to a CSV report while the suite runs; no
separate benchmark pass is required. Report mode disables stack checking and
forces serial execution so the measurements reflect normal builds without
parallel-run noise. When using ntvcm, normal app runs explicitly use full speed
(`-s:0`), while report mode runs apps at a fixed 400 MHz emulator clock by
default and passes -p so ntvcm emits its own performance data at app exit. The
report records, per optimisation mode, ntvcm's reported elapsed milliseconds and
Z80 cycle count (host-independent), the .COM size, and the ntvcm clock rate:
    machine,os,utc-timestamp,app,peep_ms,peep_cycles,peep_size,
    nopeep_ms,nopeep_cycles,nopeep_size,clock_hz

.PARAMETER Emulator
  Emulator command to run .COM files (default: "ntvcm").

.PARAMETER NoStackCheck
  Disable the lightweight stack-overflow guard (-fstack-check). The guard is
  ON by default for all apps; pass -NoStackCheck to build without it.

.PARAMETER BuildDir
  Working directory for build artifacts (default: "build").

.PARAMETER BaselineDir
  Directory of per-app baseline files for verification (default: "tests/baselines").
  Each file is named <app>.txt and holds that app's exact expected stdout.

.PARAMETER Mode
    Which optimization pass(es) to build and verify (default: "fast"):
    fast   - optimized (runs the dccpeep peephole optimizer)
    nopeep - unoptimized (skips dccpeep)
    full   - builds and verifies each app twice, once per mode, against the
             same baseline (two builds per app)

.PARAMETER Help
    Show this help text and exit without building or running tests.

.PARAMETER Extended
    Also run the extended c-testsuite single-exec corpus after the main app suite.

.PARAMETER Serial
  Build and verify apps sequentially in the shared build directory. By default
  the suite runs in parallel; use -Serial as a fallback (e.g. for debugging or
  on constrained machines).

.PARAMETER ThrottleLimit
  Max concurrent apps in parallel mode (default: CPU core count).

.PARAMETER Report
    Append per-app performance metrics to the report CSV. Implies -NoStackCheck and -Serial.

.PARAMETER ReportFile
    CSV path for -Report output (default: "perf_results.csv").

.PARAMETER ReportClockHz
    ntvcm clock speed used for measured app runs in -Report mode (default: 400000000).

.EXAMPLE
  pwsh ./scripts/runall.ps1
    pwsh ./scripts/runall.ps1 -Help
  pwsh ./scripts/runall.ps1 -NoStackCheck
  pwsh ./scripts/runall.ps1 -Mode nopeep
    pwsh ./scripts/runall.ps1 -Extended
  pwsh ./scripts/runall.ps1 -Serial
  pwsh ./scripts/runall.ps1 -ThrottleLimit 8
    pwsh ./scripts/runall.ps1 -Report

.NOTES
  App overrides are loaded from tests/_test_overrides.json:
    - args: command-line arguments for the app
        - stdin: text piped to app stdin during execution
    - stack_size: C stack reserve override (default 512)
    - ignore: set to true to skip building/running this app

  Exit codes:
    0 = all tests passed
    1 = one or more tests failed
#>

param(
    [string]$Emulator = "ntvcm",
    [switch]$NoStackCheck,
    [string]$BuildDir = "build",
    [string]$BaselineDir = "tests/baselines",
    [ValidateSet("fast", "nopeep", "full")]
    [string]$Mode = "fast",
    [switch]$Help,
    [switch]$Extended,
    [int]$RunTimeout = 60,
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [switch]$Report,
    [string]$ReportFile = "perf_results.csv",
    [long]$ReportClockHz = 400000000,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

foreach ($extraArg in $ExtraArgs) {
    if ($extraArg -match '^-mode=(fast|nopeep|full)$') {
        $Mode = $Matches[1]
    }
    elseif ($extraArg -match '^-mode=') {
        Write-Error "Invalid mode '$($extraArg.Substring(6))'. Valid modes are: fast, nopeep, full."
        exit 1
    }
    else {
        Write-Error "Unknown argument: $extraArg"
        exit 1
    }
}

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    return
}

# Parallel is the default; -Serial or -Report forces the sequential fallback.
$Parallel = -not ($Serial -or $Report)

# The lightweight stack-overflow guard (-fstack-check) is ON by default; pass
# -NoStackCheck to build without it.
$StackCheck = -not ($NoStackCheck -or $Report)

$ErrorActionPreference = "Stop"

# On Unix-like hosts, preserve the caller terminal mode and restore it before
# script exit. Some emulator runs (especially keyboard-oriented tests) can
# leave the tty in non-echo/raw mode when many apps run in parallel.
$script:SavedSttyState = $null
if (($IsLinux -or $IsMacOS) -and (Get-Command stty -ErrorAction SilentlyContinue)) {
    try {
        $captured = (& stty -g 2>$null)
        if ($captured) { $script:SavedSttyState = $captured.Trim() }
    }
    catch { }
}

function Restore-TerminalState {
    if ($script:SavedSttyState) {
        try {
            & stty $script:SavedSttyState 2>$null | Out-Null
        }
        catch { }
    }
}

trap {
    Restore-TerminalState
    throw
}

# Dot-source the build driver once so Invoke-MaBuild runs in-process. This
# avoids spawning a fresh pwsh per build, which is the dominant cost over a
# full suite (hundreds of builds). ma.ps1 has its own param block, so preserve
# this script's parameter values before dot-sourcing it.
$requestedMode = $Mode
$requestedBuildDir = $BuildDir
$requestedEmulator = $Emulator
. (Join-Path $PSScriptRoot "ma.ps1")
$Mode = $requestedMode
$BuildDir = $requestedBuildDir
$Emulator = $requestedEmulator

function New-RunBuildId {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss-ffff"
    $rand = Get-Random -Minimum 100000 -Maximum 1000000
    return "run-$stamp-$rand"
}

if ($Parallel) {
    $BuildDir = Join-Path $BuildDir (New-RunBuildId)
}

# Get machine name for reporting (platform-specific)
$machineName = $null

if ($IsMacOS) {
    # macOS local host name (e.g. "MacBook-Pro-3")
    try { $machineName = (scutil --get LocalHostName 2>/dev/null).Trim() } catch { }
} elseif ($IsWindows) {
    # Windows computer name
    $machineName = $env:COMPUTERNAME
} elseif ($IsLinux) {
    # Linux static hostname
    try { $machineName = (hostnamectl --static 2>/dev/null).Trim() } catch { }
}

# Fallbacks for any platform
if (!$machineName) { $machineName = $env:COMPUTERNAME }
if (!$machineName) { $machineName = $env:HOSTNAME }
if (!$machineName) { 
    try { 
        $machineName = (hostname 2>/dev/null).Trim()
    } catch { }
}
if (!$machineName) { $machineName = "unknown" }

# Host OS label for the report (Windows, Linux, macOS).
$osName = if ($IsWindows) { "Windows" }
    elseif ($IsLinux) { "Linux" }
    elseif ($IsMacOS) { "macOS" }
    else { "unknown" }

# Load app overrides
$appOverridesPath = "tests/_test_overrides.json"
if (-not (Test-Path $appOverridesPath)) {
    Write-Warning "App overrides not found: $appOverridesPath"
    $appOverrides = @{}
} else {
    $config = Get-Content $appOverridesPath | ConvertFrom-Json
    $appOverrides = @{}
    foreach ($app in $config.apps) {
        $appOverrides[$app.name] = @{}
        if ($app.args) { $appOverrides[$app.name]['args'] = $app.args }
        if ($app.stdin) { $appOverrides[$app.name]['stdin'] = $app.stdin }
        if ($app.stack_size) { $appOverrides[$app.name]['stack_size'] = $app.stack_size }
        if ($app.ignore) { $appOverrides[$app.name]['ignore'] = $app.ignore }
    }
}

# Set environment variables
if ($StackCheck) {
    $env:DCC_FORCE_STACK_CHECK = "1"
    Write-Host "--- stack-check: building every app with -fstack-check (default; use -NoStackCheck to disable) ---" -ForegroundColor Cyan
} else {
    Remove-Item env:DCC_FORCE_STACK_CHECK -ErrorAction SilentlyContinue
    Write-Host "--- stack-check disabled (-NoStackCheck) ---" -ForegroundColor DarkGray
}

# Discover test files
$testFiles = @()
$testDir = "tests"
if (Test-Path $testDir -PathType Container) {
    $testFiles = @(Get-ChildItem -Path (Join-Path $testDir "*.c") -File | ForEach-Object { $_.BaseName } | Sort-Object)
}

if ($testFiles.Count -eq 0) {
    Write-Error "No test files found in $testDir" -ErrorAction Stop
}

Write-Host "Found $($testFiles.Count) test applications" -ForegroundColor Cyan

# Helper functions
function Get-AppArgs {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['args']) {
        return $appOverrides[$app]['args']
    }
    return ""
}

function Get-StackSize {
    param([string]$app)
    $globalStack = $env:STACK_SIZE
    if ($globalStack) { return $globalStack }
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['stack_size']) {
        return $appOverrides[$app]['stack_size']
    }
    return ""
}

function Get-AppStdin {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['stdin']) {
        return $appOverrides[$app]['stdin']
    }
    return ""
}

function Get-IgnoreApp {
    param([string]$app)
    return ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['ignore'])
}

function Test-IsNtvcmEmulator {
    param([string]$Command)
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($Command)
    return ($leaf -ieq "ntvcm")
}

# CP/M data fixtures are every file in tests/ that is not a C source or repo
# metadata (.c, .json, .md, dotfiles). Each fixture is resolved once to its
# source path so staging does not repeatedly rescan tests/ for every app.
function Get-FixtureFiles {
    if (-not (Test-Path "tests" -PathType Container)) { return @() }
    return @(Get-ChildItem -Path "tests" -File |
        Where-Object { $_.Name -notlike '.*' -and $_.Extension -notin '.c', '.json', '.md' } |
        ForEach-Object {
            [pscustomobject]@{
                Name   = $_.Name
                Source = $_.FullName
            }
        })
}

# Stage every CP/M data fixture into the shared build dir (serial mode). Some
# interpreters read these files from the current working directory, and apps are
# run from the build dir (below), so the fixtures must live there too.
function Stage-FixtureInputs {
    foreach ($f in $fixtureList) {
        Copy-FixtureUpper -Fixture $f -DestDir $BuildDir
    }
}

# Stage one CP/M data fixture into a run directory.
#
# CP/M (and the ntvcm emulator) uppercases every filename a program opens, so a
# fixture must be present under its UPPERCASE name in the run directory to be
# found on case-sensitive filesystems (Linux). macOS and Windows filesystems are
# case-insensitive, so the uppercase name resolves there too. The source file is
# resolved case-insensitively, so a single canonical copy in tests/ (in any
# case, e.g. lowercase eu.c) is sufficient on every platform -- no need to commit
# duplicate uppercase copies (which also collide in git on case-insensitive
# checkouts).
function Copy-FixtureUpper {
    param([object]$Fixture, [string]$DestDir)
    $name = if ($Fixture -is [string]) { $Fixture } else { $Fixture.Name }
    $source = if ($Fixture -is [string]) { $null } else { $Fixture.Source }
    if (-not $name) { return }

    $dest = Join-Path $DestDir ($name.ToUpper())
    if ($source -and (Test-Path $source -PathType Leaf)) {
        Copy-Item -LiteralPath $source -Destination $dest -Force -ErrorAction SilentlyContinue
        return
    }

    foreach ($dir in @("tests", ".")) {
        if (-not (Test-Path $dir -PathType Container)) { continue }
        $src = Get-ChildItem -LiteralPath $dir -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -ieq $name } | Select-Object -First 1
        if ($src) {
            Copy-Item -LiteralPath $src.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
            return
        }
    }
}

# Build, run, and verify a single app across the requested modes. Returns a
# result object with a collected log (Lines) so callers can print output in a
# deterministic, grouped order (important under parallel execution). This is
# self-contained: it only depends on Invoke-MaBuild / ConvertTo-CRLF (from
# ma.ps1) and Test-MatchesBaseline, all of which are made available in both
# serial and parallel contexts.
function Invoke-AppTest {
    param(
        [string]$AppName,
        [string[]]$Modes,
        [string]$BuildDir,
        [string]$BaselineDir,
        [string]$Emulator,
        [System.Collections.IDictionary]$Placeholders,
        [string]$RunArgs,
        [string]$RunStdin,
        [string]$StackSize,
        [string[]]$EmulatorRunArgs,
        [object[]]$Fixtures,
        [bool]$StageFixtures = $true
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $appPassed = $true
    $modeMetrics = @{}

    # Ensure the (possibly per-app) build dir exists and stage CP/M fixtures.
    if (-not (Test-Path $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    if ($StageFixtures) {
        foreach ($f in $Fixtures) {
            Copy-FixtureUpper -Fixture $f -DestDir $BuildDir
        }
    }

    # Load and normalize this app baseline once; when running both modes we
    # compare twice against the same expected output.
    $blPath = Join-Path $BaselineDir "$AppName.txt"
    $hasBaseline = Test-Path $blPath -PathType Leaf
    $expected = $null
    if ($hasBaseline) {
        $expected = (((Get-Content -Path $blPath -Raw) -replace "`r`n", "`n")).TrimEnd("`n")
    }

    foreach ($buildMode in $Modes) {
        $displayMode = if ($buildMode -eq "peep") { "fast" } else { $buildMode }
        $stackSizeInt = if ($StackSize) { [int]$StackSize } else { 0 }
        $ok = $false
        try {
            $ok = Invoke-MaBuild -Name $AppName -Mode $buildMode -BuildDir $BuildDir -Emulator $Emulator -StackSize $stackSizeInt -Quiet
        }
        catch { $ok = $false }

        if (-not $ok) {
            $lines.Add("  Building $AppName ($displayMode)... FAILED")
            $appPassed = $false
            break
        }
        $lines.Add("  Building $AppName ($displayMode)... done")

        # Run from the build dir so interpreters find their staged data fixtures.
        $upper = $AppName.ToUpper()
        $comFile = Join-Path $BuildDir "$upper.COM"
        if (-not (Test-Path $comFile)) {
            $lines.Add("    WARNING: $comFile not found, skipping execution")
            $appPassed = $false
            continue
        }
        $comSize = (Get-Item $comFile).Length

        Push-Location $BuildDir
        $runSw = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            $appArgs = if ($RunArgs) { @($RunArgs -split '\s+') } else { @() }
            $nativeArgs = @($EmulatorRunArgs) + @("$upper.COM") + $appArgs
            if ($RunStdin) {
                $out = $RunStdin | & $Emulator @nativeArgs 2>&1
            }
            else {
                $out = & $Emulator @nativeArgs 2>&1
            }
            $output = ($out -join "`n")
        }
        catch {
            $output = ""
            $lines.Add("    ERROR running $AppName : $_")
        }
        finally {
            $runSw.Stop()
            Pop-Location
        }

        # In report mode ntvcm is run with -p, which appends a performance block
        # to stdout at app exit, e.g.:
        #     elapsed milliseconds:               10
        #     Z80  cycles:                    79,093
        #     clock rate:                400,000,000 Hz
        # Parse those values (commas stripped) for the report, then remove the
        # block from the captured output so it doesn't break baseline matching.
        $ntvcmMs = ""
        $ntvcmCycles = ""
        $ntvcmClockHz = ""
        if ($output -match '(?m)^\s*elapsed milliseconds:\s*([\d,]+)') {
            $ntvcmMs = $matches[1] -replace ',', ''
        }
        if ($output -match '(?m)^\s*Z80\s+cycles:\s*([\d,]+)') {
            $ntvcmCycles = $matches[1] -replace ',', ''
        }
        if ($output -match '(?m)^\s*clock rate:\s*([\d,]+)') {
            $ntvcmClockHz = $matches[1] -replace ',', ''
        }
        # Strip the ntvcm performance block (and any blank separator before it).
        $output = [regex]::Replace($output, '(?s)\r?\n\s*elapsed milliseconds:.*$', '')

        $modeMetrics[$buildMode] = [pscustomobject]@{
            Ms      = if ($ntvcmMs -ne "") { $ntvcmMs } else { $runSw.ElapsedMilliseconds }
            Cycles  = $ntvcmCycles
            ClockHz = $ntvcmClockHz
            Size    = $comSize
        }

        # Compare against the per-app baseline (placeholder-aware).
        if ($hasBaseline) {
            $actual = ($output -replace "`r`n", "`n").TrimEnd("`n")
            if (-not (Test-MatchesBaseline -Actual $actual -Baseline $expected -Placeholders $Placeholders)) {
                $lines.Add("    OUTPUT MISMATCH (vs $BaselineDir/$AppName.txt)")
                $expLines = if ($expected) { @($expected -split "`n") } else { @() }
                $actLines = if ($actual)   { @($actual   -split "`n") } else { @() }
                $maxLen = [Math]::Max($expLines.Count, $actLines.Count)
                for ($i = 0; $i -lt $maxLen; $i++) {
                    $e = if ($i -lt $expLines.Count) { $expLines[$i] } else { $null }
                    $a = if ($i -lt $actLines.Count) { $actLines[$i] } else { $null }
                    if ($null -eq $a) {
                        $lines.Add("    DIFF- $e")
                    } elseif ($null -eq $e) {
                        $lines.Add("    DIFF+ $a")
                    } elseif ($e -ceq $a) {
                        $lines.Add("    DIFF  $e")
                    } else {
                        $lines.Add("    DIFF- $e")
                        $lines.Add("    DIFF+ $a")
                    }
                }
                $appPassed = $false
            }
            else {
                $lines.Add("    Output matches baseline")
            }
        }
        else {
            $lines.Add("    ERROR: no baseline at $BaselineDir/$AppName.txt (every app must have one)")
            $appPassed = $false
            break
        }
    }

    $sw.Stop()
    return [pscustomobject]@{
        App     = $AppName
        Passed  = $appPassed
        Elapsed = $sw.Elapsed
        Lines   = $lines.ToArray()
        Metrics = $modeMetrics
    }
}

# Per-app baseline files live in $BaselineDir (one <app>.txt per test, holding
# that app's exact expected stdout). Comparison is by app name, so the order in
# which tests are discovered/run does not matter. See tests/README.md.
$baselineAvailable = Test-Path $BaselineDir -PathType Container
if ($baselineAvailable) {
    $baselineCount = @(Get-ChildItem -Path "$BaselineDir/*.txt" -File -ErrorAction SilentlyContinue).Count
    Write-Host "Using per-app baselines from $BaselineDir ($baselineCount files)" -ForegroundColor Cyan
} else {
    Write-Host "No baseline directory at $BaselineDir; running without verification" -ForegroundColor Yellow
}

function Get-Baseline {
    param([string]$app)
    $path = Join-Path $BaselineDir "$app.txt"
    if (Test-Path $path -PathType Leaf) {
        # Return raw expected output, normalized to LF.
        return ((Get-Content -Path $path -Raw) -replace "`r`n", "`n")
    }
    return $null
}

# Global placeholder definitions for volatile output. A baseline file may embed
# any of these tokens; at compare time the baseline is turned into a regex
# template and matched against the actual output. This keeps ALL expected
# content in the single baseline file (no per-app rules elsewhere) while still
# tolerating values that legitimately change between builds/platforms.
#
#   {{DATE}}  - C __DATE__ value, e.g. "Jun 16 2026"
#   {{TIME}}  - C __TIME__ value, e.g. "20:01:50"
#   {{SEP}}   - path separator, "/" (Unix) or "\" (Windows)
#   {{HEX4}}  - four uppercase hex digits, e.g. "0040"
$Placeholders = [ordered]@{
    '{{DATE}}' = '[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}'
    '{{TIME}}' = '\d{2}:\d{2}:\d{2}'
    '{{SEP}}'  = '[/\\]'
    '{{HEX4}}' = '[0-9A-F]{4}'
}

# Compare actual output against a baseline that may contain placeholder tokens.
# If the baseline has no placeholders, this is a plain string equality check.
# Placeholders are passed explicitly so this works inside parallel runspaces
# (which do not inherit the parent's script-scope variables).
function Test-MatchesBaseline {
    param(
        [string]$Actual,
        [string]$Baseline,
        [System.Collections.IDictionary]$Placeholders
    )
    if (-not $Placeholders) { $Placeholders = $script:Placeholders }
    if ($Baseline -notmatch '\{\{[A-Z][A-Z0-9]*\}\}') {
        return ($Actual -ceq $Baseline)
    }
    # Build a regex from the baseline: escape literal text segments, and insert
    # each placeholder's pattern in place of its token. Escaping only the
    # literal parts avoids double-escaping the tokens themselves.
    $tokenRegex = [regex]'\{\{([A-Z][A-Z0-9]*)\}\}'
    $sb = [System.Text.StringBuilder]::new()
    $last = 0
    foreach ($m in $tokenRegex.Matches($Baseline)) {
        $literal = $Baseline.Substring($last, $m.Index - $last)
        [void]$sb.Append([regex]::Escape($literal))
        $token = '{{' + $m.Groups[1].Value + '}}'
        if ($Placeholders.Contains($token)) {
            [void]$sb.Append($Placeholders[$token])
        }
        else {
            # Unknown token: treat it literally.
            [void]$sb.Append([regex]::Escape($m.Value))
        }
        $last = $m.Index + $m.Length
    }
    [void]$sb.Append([regex]::Escape($Baseline.Substring($last)))
    return ($Actual -cmatch ('^' + $sb.ToString() + '$'))
}

# Main build and run loop
$passed = 0
$failed = 0
$skipped = 0
$failedApps = @()

$modes = switch ($Mode) {
    "fast"   { @("peep") }
    "nopeep" { @("nopeep") }
    "full"   { @("peep", "nopeep") }
}

$optimisationSummary = switch ($Mode) {
    "fast"   { "fast" }
    "nopeep" { "nopeep" }
    "full"   { "full (fast + nopeep)" }
}

$emulatorRunArgs = @()
if (Test-IsNtvcmEmulator $Emulator) {
    if ($Report) {
        # -p makes ntvcm print performance info (elapsed ms, Z80 cycles, clock
        # rate) at app exit; the report parses those values from stdout.
        $emulatorRunArgs = @("-p", "-s:$ReportClockHz")
    }
    else {
        $emulatorRunArgs = @("-s:0")
    }
}

# The CP/M data fixtures are derived once from the tests/ directory (see
# Get-FixtureFiles). Every app gets the same set staged into its run dir.
$fixtureList = @(Get-FixtureFiles)

# Build the list of work items up front (resolving per-app args/stack in the
# parent), so parallel workers don't need the $appOverrides table.
$workItems = [System.Collections.Generic.List[object]]::new()
foreach ($app in $testFiles) {
    if (Get-IgnoreApp $app) {
        $skipped++
        continue
    }
    $workItems.Add([pscustomobject]@{
        App          = $app
        RunArgs      = (Get-AppArgs $app)
        RunStdin     = (Get-AppStdin $app)
        StackSize    = (Get-StackSize $app)
    })
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "STARTING BUILD AND RUN SUITE" -ForegroundColor Cyan
Write-Host "Mode: $optimisationSummary" -ForegroundColor Cyan
if ($Parallel) {
    Write-Host "(parallel, throttle = $ThrottleLimit)" -ForegroundColor Cyan
    Write-Host "Build root: $BuildDir" -ForegroundColor Cyan
}
Write-Host "========================================" -ForegroundColor Cyan

# Measure total suite time
$suiteStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# Helper to render one app's collected result.
function Show-AppResult {
    param($result)
    Write-Host ""
    Write-Host "Testing $($result.App)..." -ForegroundColor Yellow
    foreach ($line in $result.Lines) {
        $color = if ($line -match 'FAILED|MISMATCH|WARNING|ERROR') { 'Red' }
                 elseif ($line -match 'matches baseline') { 'Green' }
                 elseif ($line -match '^    DIFF-') { 'Red' }
                 elseif ($line -match '^    DIFF\+') { 'Green' }
                 elseif ($line -match '^    DIFF  ') { 'DarkGray' }
                 else { 'Gray' }
        Write-Host $line -ForegroundColor $color
    }
    $elapsed = $result.Elapsed
    $elapsedStr = if ($elapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $elapsed } else { "{0:0.00}s" -f $elapsed.TotalSeconds }
    $scTag = if ($StackCheck) { "Stack Check Enabled" } else { "No Stack Check" }
    $status = if ($result.Passed) { "PASS" } else { "FAIL" }
    $line = "  {0}  {1,-12} {2,8} | {3}" -f $status, $result.App, $elapsedStr, $scTag
    Write-Host $line -ForegroundColor $(if ($result.Passed) { "Green" } else { "Red" })
}

function Write-PerformanceReport {
    param(
        [object[]]$Results,
        [string[]]$AppOrder,
        [string]$OutputFile,
        [string]$MachineName,
        [string]$OsName,
        [string]$Emulator,
        [long]$ReportClockHz
    )

    $byApp = @{}
    foreach ($result in $Results) {
        $byApp[$result.App] = $result
    }

    if (-not (Test-Path $OutputFile)) {
        Add-Content -Path $OutputFile -Value "machine,os,utc-timestamp,app,peep_ms,peep_cycles,peep_size,nopeep_ms,nopeep_cycles,nopeep_size,clock_hz"
    }

    $utcTimestamp = [System.DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    $rows = 0

    foreach ($app in $AppOrder) {
        if (-not $byApp.ContainsKey($app)) { continue }
        $metrics = $byApp[$app].Metrics
        if (-not $metrics) { continue }
        if (-not ($metrics.ContainsKey("peep") -or $metrics.ContainsKey("nopeep"))) { continue }

        $peepMs = ""
        $peepCycles = ""
        $peepSize = ""
        $nopeepMs = ""
        $nopeepCycles = ""
        $nopeepSize = ""
        # clock_hz is the ntvcm-reported clock rate; identical for both modes.
        $clockHz = ""

        if ($metrics.ContainsKey("peep")) {
            $peepMs = $metrics["peep"].Ms
            $peepCycles = $metrics["peep"].Cycles
            $peepSize = $metrics["peep"].Size
            if (-not $clockHz) { $clockHz = $metrics["peep"].ClockHz }
        }
        if ($metrics.ContainsKey("nopeep")) {
            $nopeepMs = $metrics["nopeep"].Ms
            $nopeepCycles = $metrics["nopeep"].Cycles
            $nopeepSize = $metrics["nopeep"].Size
            if (-not $clockHz) { $clockHz = $metrics["nopeep"].ClockHz }
        }

        Add-Content -Path $OutputFile -Value "$MachineName,$OsName,$utcTimestamp,$app,$peepMs,$peepCycles,$peepSize,$nopeepMs,$nopeepCycles,$nopeepSize,$clockHz"
        $rows++
    }

    Write-Host "  Report:       $OutputFile ($rows apps)"
    if (Test-IsNtvcmEmulator $Emulator) {
        $clockLabel = if (($ReportClockHz % 1000000000) -eq 0) {
            "$($ReportClockHz / 1000000000)GHz"
        } elseif (($ReportClockHz % 1000000) -eq 0) {
            "$($ReportClockHz / 1000000)MHz"
        } else {
            "$ReportClockHz Hz"
        }
        Write-Host "  Clock:        ntvcm clock speed normalised to $clockLabel"
    }
}

function Invoke-ExtendedSuite {
    param(
        [string]$Mode,
        [string]$Emulator,
        [int]$RunTimeout,
        [string]$BuildDir,
        [bool]$StackCheck,
        [bool]$Serial,
        [int]$ThrottleLimit
    )

    $extendedScript = Join-Path $PSScriptRoot "runall-extended.ps1"
    $extendedBuildDir = Join-Path $BuildDir "extended-tests"
    $extendedArgs = @(
        "-NoProfile",
        "-File", $extendedScript,
        "-Mode", $Mode,
        "-Emulator", $Emulator,
        "-RunTimeout", $RunTimeout,
        "-BuildDir", $extendedBuildDir,
        "-ThrottleLimit", $ThrottleLimit
    )
    if (-not $StackCheck) { $extendedArgs += "-NoStackCheck" }
    if ($Serial) { $extendedArgs += "-Serial" }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "STARTING EXTENDED C-TESTSUITE" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    & pwsh @extendedArgs
    $script:ExtendedSuiteExitCode = $LASTEXITCODE
}

$results = @()
$totalToRun = $workItems.Count

if ($Parallel) {
    # Each worker runs in its own runspace with its own filesystem location, so
    # per-app build dirs (build/<app>) prevent the shared-file clobbering that
    # would otherwise occur (DCCRTL.MAC, RTLMIN.MAC, tool COMs, .COM outputs).
    $repoRoot     = (Get-Location).Path
    $maPath       = (Join-Path $PSScriptRoot "ma.ps1")
    $tmbDef       = ${function:Test-MatchesBaseline}.ToString()
    $iatDef       = ${function:Invoke-AppTest}.ToString()
    $cfuDef       = ${function:Copy-FixtureUpper}.ToString()
    $stackCheckOn = [bool]$StackCheck
    $runArgs      = @($emulatorRunArgs)

    # ForEach-Object -Parallel streams each worker's result as it completes, so
    # pipe straight into a loop that prints a live status line per app. Results
    # arrive in completion order (not sorted); we collect them for the summary.
    $done = 0
    $workItems | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        $item = $_
        Set-Location $using:repoRoot
        if ($using:stackCheckOn) { $env:DCC_FORCE_STACK_CHECK = "1" }
        # Bring the needed functions into this runspace.
        . $using:maPath                                   # Invoke-MaBuild, ConvertTo-CRLF
        ${function:Test-MatchesBaseline} = $using:tmbDef
        ${function:Copy-FixtureUpper}    = $using:cfuDef
        ${function:Invoke-AppTest}       = $using:iatDef

        $appBuildDir = Join-Path $using:BuildDir $item.App
        Invoke-AppTest -AppName $item.App -Modes $using:modes -BuildDir $appBuildDir `
            -BaselineDir $using:BaselineDir -Emulator $using:Emulator `
            -Placeholders $using:Placeholders -RunArgs $item.RunArgs `
            -RunStdin $item.RunStdin `
            -StackSize $item.StackSize -EmulatorRunArgs $using:runArgs `
            -Fixtures $using:fixtureList -StageFixtures $true
    } | ForEach-Object {
        $result = $_
        $results += $result
        $done++
        $elapsed = $result.Elapsed
        $elapsedStr = if ($elapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $elapsed } else { "{0:0.00}s" -f $elapsed.TotalSeconds }
        $counter = "[{0,3}/{1}]" -f $done, $totalToRun
        $scTag = if ($StackCheck) { "Stack Check Enabled" } else { "No Stack Check" }
        $status = if ($result.Passed) { "PASS" } else { "FAIL" }
        # Columns: counter | status | app | time | run-wide stack-check tag
        $line = "{0} {1}  {2,-12} {3,8} | {4}" -f $counter, $status, $result.App, $elapsedStr, $scTag
        if ($result.Passed) {
            Write-Host $line -ForegroundColor Green
        } else {
            Write-Host $line -ForegroundColor Red
            # Show the failing detail lines inline so problems are visible live.
            foreach ($detail in $result.Lines) {
                if ($detail -match 'FAILED|MISMATCH|WARNING|ERROR') {
                    Write-Host "        $($detail.Trim())" -ForegroundColor Red
                } elseif ($detail -match '^    DIFF-') {
                    Write-Host "        $($detail.Trim())" -ForegroundColor Red
                } elseif ($detail -match '^    DIFF\+') {
                    Write-Host "        $($detail.Trim())" -ForegroundColor Green
                }
            }
        }
    }
}
else {
    # Serial: stage fixtures once into the shared build dir and reuse it.
    Stage-FixtureInputs
    foreach ($item in $workItems) {
        $result = Invoke-AppTest -AppName $item.App -Modes $modes -BuildDir $BuildDir `
            -BaselineDir $BaselineDir -Emulator $Emulator -Placeholders $Placeholders `
            -RunArgs $item.RunArgs -RunStdin $item.RunStdin `
            -StackSize $item.StackSize -EmulatorRunArgs $emulatorRunArgs `
            -Fixtures $fixtureList -StageFixtures $false
        Show-AppResult $result
        $results += $result
    }
}

# Tally results.
foreach ($result in $results) {
    if ($result.Passed) {
        $passed++
    } else {
        $failed++
        $failedApps += $result.App
    }
}

$extendedPassed = $null
if ($Extended) {
    Invoke-ExtendedSuite -Mode $Mode -Emulator $Emulator -RunTimeout $RunTimeout `
        -BuildDir $BuildDir -StackCheck $StackCheck -Serial (-not $Parallel) -ThrottleLimit $ThrottleLimit
    $extendedExitCode = $script:ExtendedSuiteExitCode
    $extendedPassed = ($extendedExitCode -eq 0)
    if (-not $extendedPassed) {
        $failed++
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST SUITE SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
$suiteStopwatch.Stop()
$suiteElapsed = $suiteStopwatch.Elapsed
$suiteElapsedStr = if ($suiteElapsed.TotalSeconds -ge 60) {
    "{0:m\m\ s\.f\s}" -f $suiteElapsed
} else {
    "{0:0.00}s" -f $suiteElapsed.TotalSeconds
}
Write-Host "  Total apps:   $($testFiles.Count)"
Write-Host "  Passed:       $passed" -ForegroundColor Green
Write-Host "  Failed:       $failed" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
Write-Host "  Skipped:      $skipped"
if ($Extended) {
    Write-Host "  Extended:     $(if ($extendedPassed) { 'passed' } else { 'failed' })" -ForegroundColor $(if ($extendedPassed) { "Green" } else { "Red" })
}
Write-Host "  Total time:   $suiteElapsedStr"
Write-Host "  Optimisation: $optimisationSummary"

if ($failedApps.Count -gt 0) {
    Write-Host ""
    Write-Host "Failed apps:" -ForegroundColor Red
    foreach ($app in $failedApps) {
        Write-Host "  - $app" -ForegroundColor Red
    }
}
if ($Extended -and -not $extendedPassed) {
    Write-Host ""
    Write-Host "Extended c-testsuite failed" -ForegroundColor Red
}

if ($Report) {
    Write-PerformanceReport -Results $results -AppOrder $testFiles -OutputFile $ReportFile -MachineName $machineName `
        -OsName $osName -Emulator $Emulator -ReportClockHz $ReportClockHz
}

Write-Host ""
if ($null -ne $_savedStty) { stty $_savedStty 2>$null }
if ($failed -eq 0) {
    Write-Host ">>> SUCCESS: All tests passed <<<" -ForegroundColor Green
    Restore-TerminalState
    exit 0
} else {
    Write-Host ">>> FAILURE: $failed test(s) failed <<<" -ForegroundColor Red
    Restore-TerminalState
    exit 1
}
