#Requires -Version 7
<#
.SYNOPSIS
Comprehensive test suite: builds and runs all test applications with output
verification against baseline.

.DESCRIPTION
Builds all *.c files in tests/ folder using dccmake, executes each under the
emulator, and compares output against per-app baselines in tests/baselines/
(one <app>.txt per test). Comparison is keyed by app name, so test discovery
order does not matter. Uses tests/_test_overrides.json for test-specific arguments and
stack sizes. After the app suite, also runs the compile-fail diagnostics suite
via scripts/run-diagnostics.ps1 and fails the run if any diagnostic baseline
mismatches.

Pass -Extended to also run scripts/runall-extended.ps1 after the main app suite,
verifying the imported c-testsuite single-exec corpus as part of the same run.

Supports per-test arguments (e.g., ttt with "10" as input) and custom stack
size overrides (e.g., cobint needs 1536 bytes, triangle needs 768).

Pass -Report to append per-app metrics to a CSV report while the suite runs; no
separate benchmark pass is required, and it runs in parallel like a normal
suite run. Report mode disables stack checking (so the measurements reflect a
normal, non-debug build), but every run - Report or not - always uses ntvcm's
full, unthrottled speed (`-s:0`) and passes -p so ntvcm emits its own Z80 cycle
count at app exit. The report records, per optimisation mode, that cycle count
(host-independent), the .COM size, and an "ms at ReportClockHz" figure computed
by simple arithmetic from the cycle count (cycles / ReportClockHz * 1000) - not
measured by actually pacing the emulator down to that clock speed, which for a
cycle-heavy app at a nominal historical clock rate could turn a sub-second run
into many minutes of real wall-clock time for no benefit, since the exact same
number is one division away from the cycle count ntvcm already reports at full
speed:
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
    Append per-app performance metrics to the historical, multi-machine report
    CSV (see ReportFile). Implies -NoStackCheck (so the measurements reflect a
    normal, non-debug build); runs in parallel like a normal suite run, and at
    ntvcm's full unthrottled speed (-s:0) like a normal run too - the reported
    "ms" figure is computed from the Z80 cycle count (see ReportClockHz), not
    measured by actually pacing the emulator down to a slower clock. This is a
    separate, opt-in, cross-machine performance log - it is NOT what backs the
    on-by-default cycle-count regression check below (see PerfBaselineFile),
    which needs no separate pass at all.

.PARAMETER ReportFile
    CSV path for -Report output (default: "perf_results.csv").

.PARAMETER ReportClockHz
    Nominal clock speed (Hz) used to compute the "ms" figure recorded in
    -Report's CSV from ntvcm's reported Z80 cycle count (ms = cycles / this *
    1000). Default: 400000000. Purely arithmetic - does not affect how fast
    ntvcm actually runs the app (always -s:0, full speed).

.PARAMETER NoPerfCheck
    Skip the Z80 cycle-count regression check against PerfBaselineFile. The
    check is ON by default and adds no separate run: every app already passes
    -p to ntvcm, so its cycle count is parsed out of the same execution
    already done for output verification, in whichever mode(s) this invocation
    builds (-Mode fast/nopeep/full). The Z80 cycle count ntvcm reports is a
    pure instruction-count total from the emulated CPU, independent of host
    scheduling, so it is exactly as reliable measured under -ThrottleLimit
    parallelism as it would be serially - no need to force -Serial for it.

    The check itself is skipped (regardless of this switch) whenever
    -NoStackCheck or -Report is in effect: the stack-check guard adds a
    prologue check to every function, so a build without it is not
    cycle-count-comparable to the baseline (captured under a plain default
    run) at all - every app would show some spurious difference having
    nothing to do with a real codegen change.

.PARAMETER UpdatePerfBaseline
    Instead of comparing against PerfBaselineFile, (re)write it from this run's
    measured cycle counts. Only the column(s) for the mode(s) this invocation
    actually built are touched (peep_cycles for -Mode fast, nopeep_cycles for
    -Mode nopeep, both for -Mode full) - any other app/column already in the
    file is left untouched. Use after an intentional, verified codegen change
    changes cycle counts (an improvement, or an accepted tradeoff).

.PARAMETER PerfBaselineFile
    CSV path for the cycle-count regression baseline (default:
    "tests/perf_baselines.csv"). One row per app, columns
    app,peep_cycles,nopeep_cycles. Checked into the repo like the output
    baselines in tests/baselines/, so a regression shows up as a diff in code
    review the same way an output-baseline change would.

.PARAMETER KeepBuild
    In parallel mode the suite builds into a per-invocation folder
    (build/run-<pid>) so concurrent runs stay isolated, and removes it on exit to
    keep build/ from accumulating run-* folders. Pass -KeepBuild to retain that
    folder (e.g. to inspect failing build artifacts).

.EXAMPLE
  pwsh ./scripts/runall.ps1
    pwsh ./scripts/runall.ps1 -Help
  pwsh ./scripts/runall.ps1 -NoStackCheck
  pwsh ./scripts/runall.ps1 -Mode nopeep
    pwsh ./scripts/runall.ps1 -Extended
  pwsh ./scripts/runall.ps1 -Serial
  pwsh ./scripts/runall.ps1 -ThrottleLimit 8
    pwsh ./scripts/runall.ps1 -Report
  pwsh ./scripts/runall.ps1 -Mode full -UpdatePerfBaseline

.NOTES
  App overrides are loaded from tests/_test_overrides.json:
    - args: command-line arguments for the app
        - stdin: text piped to app stdin during execution
    - stack_size: C stack reserve override (default 512)
    - ignore: set to true to skip building/running this app
    - perf_ignore: set to true to exclude this app from the cycle-count
      regression check entirely (never a regression, improvement, or new
      baseline) - for a test whose cycle count is known to vary run-to-run
      for reasons unrelated to codegen, e.g. tkbd (a keyboard-poll loop whose
      iteration count depends on real host timing) or tdirent/cpmenumd
      (directory enumeration order depends on real filesystem state).

  Cycle-count regression checking (on by default, see NoPerfCheck above) only
  compares/updates the mode(s) actually built this run - run with -Mode full
  (or run both -Mode fast and -Mode nopeep separately) to keep both columns of
  tests/perf_baselines.csv current.

  Exit codes:
    0 = all tests passed (including no cycle-count regression)
    1 = one or more tests failed, or a performance regression was found
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
    [switch]$NoPerfCheck,
    [switch]$UpdatePerfBaseline,
    [string]$PerfBaselineFile = "tests/perf_baselines.csv",
    [switch]$KeepBuild,
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

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
Set-Location $script:RepoRoot

# Parallel is the default; -Serial forces the sequential fallback. -Report no
# longer needs to force serial: its "ms" figure is computed from ntvcm's own
# reported Z80 cycle count divided by ReportClockHz (see Write-PerformanceReport
# below) rather than measured via real-time clock pacing, so it is exactly as
# reproducible under parallelism as the cycle count itself.
$Parallel = -not $Serial

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

# Per-run isolation directory (parallel mode only). Recorded here so it can be
# removed on exit, keeping the build/ tree from accumulating one run-* folder
# per invocation. Pass -KeepBuild to retain it for debugging.
$script:RunBuildRoot = $null

function Remove-RunBuildDir {
    if ($KeepBuild) { return }
    if ($script:RunBuildRoot -and (Test-Path $script:RunBuildRoot -PathType Container)) {
        try {
            Remove-Item -LiteralPath $script:RunBuildRoot -Recurse -Force -ErrorAction Stop
        }
        catch { }
    }
}

trap {
    Restore-TerminalState
    Remove-RunBuildDir
    # Re-throw the original error record so its message/position survive rather
    # than surfacing a generic "ScriptHalted" from a bare throw.
    throw $_
}

function New-RunBuildId {
    return "run-$PID"
}

if ($Parallel) {
    # Isolate this invocation by process id so concurrent runs never clobber
    # each other's per-app build dirs. The folder is removed on exit (see
    # Remove-RunBuildDir) unless -KeepBuild is set, so build/ does not fill up
    # with run-* folders.
    $BuildDir = Join-Path $BuildDir (New-RunBuildId)
    $script:RunBuildRoot = $BuildDir
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
        if ($app.dcc_args) { $appOverrides[$app.name]['dcc_args'] = $app.dcc_args }
        if ($null -ne $app.dcc_floatio) { $appOverrides[$app.name]['dcc_floatio'] = $app.dcc_floatio }
        if ($null -ne $app.dcc_longio) { $appOverrides[$app.name]['dcc_longio'] = $app.dcc_longio }
        if ($app.ignore) { $appOverrides[$app.name]['ignore'] = $app.ignore }
        if ($app.perf_ignore) { $appOverrides[$app.name]['perf_ignore'] = $app.perf_ignore }
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

function Get-DccArgs {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['dcc_args']) {
        return $appOverrides[$app]['dcc_args']
    }
    return ""
}

function Get-DccFloatio {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app].ContainsKey('dcc_floatio')) {
        return $appOverrides[$app]['dcc_floatio']
    }
    return $true
}

function Get-DccLongio {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app].ContainsKey('dcc_longio')) {
        return $appOverrides[$app]['dcc_longio']
    }
    return $true
}

function ConvertTo-BooleanSetting {
    param([object]$Value)
    if ($null -eq $Value) { return $null }
    if ($Value -is [bool]) { return $Value }
    $text = $Value.ToString().Trim().ToLowerInvariant()
    if ($text -in @("1", "true", "yes", "on")) { return $true }
    if ($text -in @("0", "false", "no", "off")) { return $false }
    throw "Invalid boolean setting: $Value"
}

function Get-IgnoreApp {
    param([string]$app)
    return ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['ignore'])
}

# True for an app whose cycle count is known to vary run-to-run for reasons
# unrelated to codegen (e.g. a keyboard-poll loop whose iteration count
# depends on real host timing, or directory enumeration whose order depends
# on real filesystem state) - excluded from the perf-regression check
# entirely (never counted as a regression, improvement, or new baseline)
# rather than compared with a numeric tolerance, since the amount of noise
# for these specific tests isn't a fixed percentage.
function Get-PerfIgnoreApp {
    param([string]$app)
    return ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['perf_ignore'])
}

function Test-IsNtvcmEmulator {
    param([string]$Command)
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($Command)
    return ($leaf -ieq "ntvcm")
}

function Get-DccMakeCommand {
    $override = $env:DCCMAKE -replace '^\s+|\s+$', ''
    if ($override) { return $override }
    $local = Join-Path (Get-Location).Path ($(if ($IsWindows) { "dccmake.exe" } else { "dccmake" }))
    if (Test-Path -LiteralPath $local -PathType Leaf) { return $local }
    return "dccmake"
}

function Invoke-DccMakeBuild {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [string]$Mode = "fast",
        [string]$BuildDir = "build",
        [string]$Emulator = "ntvcm",
        [string]$SourcePath = "",
        [int]$StackSize = 0,
        [string]$DccArgs = "",
        [object]$DccFloatio = $null,
        [object]$DccLongio = $null,
        [switch]$Quiet
    )

    $modeLower = $Mode.ToLowerInvariant()
    if ($modeLower -eq "full") {
        $fastOk = Invoke-DccMakeBuild -Name $Name -Mode fast -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -Quiet:$Quiet
        $nopeepOk = Invoke-DccMakeBuild -Name $Name -Mode nopeep -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -Quiet:$Quiet
        return ($fastOk -and $nopeepOk)
    }
    $usePeep = @("fast", "peep", "opt", "optimized", "o", "1", "yes", "true") -contains $modeLower

    $base = [System.IO.Path]::GetFileNameWithoutExtension($Name)
    $lowerBase = $base.ToLowerInvariant()
    $upperBase = $base.ToUpperInvariant()

    $sourceFile = ""
    if ($SourcePath) {
        if (Test-Path -LiteralPath $SourcePath -PathType Leaf) {
            $sourceFile = (Resolve-Path -LiteralPath $SourcePath).ProviderPath
        }
    }
    else {
        foreach ($candidate in @(
            (Join-Path "tests" "$base.c"),
            (Join-Path "tests" "$base.C"),
            (Join-Path "tests" "$lowerBase.c"),
            (Join-Path "tests" "$upperBase.C"),
            "$base.c",
            "$base.C",
            "$lowerBase.c",
            "$upperBase.C"
        )) {
            if (Test-Path $candidate -PathType Leaf) {
                $sourceFile = $candidate
                break
            }
        }
    }

    if (-not $sourceFile) {
        Write-Error "Source file not found for: $Name" -ErrorAction Continue
        return $false
    }

    if (-not (Test-Path $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    $dccmake = Get-DccMakeCommand
    $args = @(
        "dcc-input=$sourceFile",
        "dcc-output=$base",
        "dcc-build-dir=$BuildDir",
        "dcc-peep=$([string]$usePeep)",
        "ntvcm-tool=$Emulator"
    )
    if ($null -ne $DccFloatio) {
        $args += "dcc-floatio=$([string](ConvertTo-BooleanSetting $DccFloatio))"
    }
    if ($null -ne $DccLongio) {
        $args += "dcc-flongio=$([string](ConvertTo-BooleanSetting $DccLongio))"
    }
    if ($StackSize -gt 0) {
        $args += @("-s", "$StackSize")
    }
    if ($DccArgs) {
        $args += @($DccArgs -split '\s+' | Where-Object { $_ })
    }

    $buildOut = & $dccmake @args 2>&1
    $exitCode = $LASTEXITCODE
    if (-not $Quiet) { $buildOut | Write-Host }
    if ($exitCode -ne 0) {
        Write-Error "dccmake failed for $Name ($Mode) with exit code $exitCode" -ErrorAction Continue
        return $false
    }

    $buildWarnings = @($buildOut | Where-Object { $_ -match '%Mult\. Def\.|%Phase error|%Undefined' })
    if ($buildWarnings) {
        $buildWarnings | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
        Write-Error "Build warnings treated as errors for $Name" -ErrorAction Continue
        return $false
    }

    $appCom = Join-Path $BuildDir "$upperBase.COM"
    if (Test-Path -LiteralPath $appCom -PathType Leaf) {
        return $true
    }

    Write-Error "Build failed: .COM file not produced for $Name" -ErrorAction Continue
    return $false
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
    if (-not (Test-Path $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
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
# self-contained: it depends on Invoke-DccMakeBuild and Test-MatchesBaseline,
# both of which are made available in both serial and parallel contexts.
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
        [string]$DccArgs,
        [object]$DccFloatio,
        [object]$DccLongio,
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
            $ok = Invoke-DccMakeBuild -Name $AppName -Mode $buildMode -BuildDir $BuildDir -Emulator $Emulator -StackSize $stackSizeInt -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -Quiet
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
#   {{UINT}}  - unsigned decimal integer
#   {{HEX4}}  - four uppercase hex digits, e.g. "0040"
$Placeholders = [ordered]@{
    '{{DATE}}' = '[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}'
    '{{TIME}}' = '\d{2}:\d{2}:\d{2}'
    '{{SEP}}'  = '[/\\]'
    '{{UINT}}' = '\d+'
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
    # -p makes ntvcm print performance info (elapsed ms, Z80 cycles, clock
    # rate) at app exit; Invoke-AppTest always parses those values out of the
    # captured stdout (and strips the block before baseline comparison), so
    # this is passed unconditionally, not just under -Report.
    #
    # -s:0 ("as fast as possible", no throttling) is used in EVERY mode,
    # including -Report. ntvcm's own -s:X (X > 0) does not just label its
    # output as if it ran at that clock speed - it actually paces execution
    # in real time to simulate it, which for a slower nominal rate like the
    # previous default (400 MHz) can make a single cycle-heavy app take many
    # minutes of real wall-clock time it would otherwise finish in a fraction
    # of a second. -Report's "ms at ReportClockHz" figure is pure arithmetic
    # (cycles / ReportClockHz * 1000), computed from the same Z80 cycle count
    # ntvcm already reports under -s:0 - see Write-PerformanceReport - so
    # there was never a need to actually run the emulator throttled to get it.
    $emulatorRunArgs = @("-p", "-s:0")
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
        DccArgs      = (Get-DccArgs $app)
        DccFloatio   = (Get-DccFloatio $app)
        DccLongio    = (Get-DccLongio $app)
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

    # ms is computed from ntvcm's own reported Z80 cycle count (cycles /
    # ReportClockHz * 1000) rather than read from ntvcm's own "elapsed
    # milliseconds" line - the run itself is always at -s:0 (unthrottled) now,
    # so that line reports real (fast, host-speed-dependent) wall-clock time,
    # not a meaningful cross-machine figure. This is exactly the number
    # ntvcm's own real-time pacing at -s:$ReportClockHz used to produce, just
    # computed directly instead of by actually running that slowly.
    function Get-MsAtClockHz {
        param([string]$Cycles, [long]$ClockHz)
        if (-not $Cycles -or $ClockHz -le 0) { return "" }
        return [math]::Round(([double][long]$Cycles / $ClockHz) * 1000.0, 3)
    }

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

        if ($metrics.ContainsKey("peep")) {
            $peepCycles = $metrics["peep"].Cycles
            $peepSize = $metrics["peep"].Size
            $peepMs = Get-MsAtClockHz -Cycles $peepCycles -ClockHz $ReportClockHz
        }
        if ($metrics.ContainsKey("nopeep")) {
            $nopeepCycles = $metrics["nopeep"].Cycles
            $nopeepSize = $metrics["nopeep"].Size
            $nopeepMs = Get-MsAtClockHz -Cycles $nopeepCycles -ClockHz $ReportClockHz
        }

        Add-Content -Path $OutputFile -Value "$MachineName,$OsName,$utcTimestamp,$app,$peepMs,$peepCycles,$peepSize,$nopeepMs,$nopeepCycles,$nopeepSize,$ReportClockHz"
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

# Loads tests/perf_baselines.csv (one row per app: app,peep_cycles,nopeep_cycles)
# into a hashtable keyed by app name. Returns an empty hashtable if the file
# does not exist yet (e.g. before the first -UpdatePerfBaseline run).
function Get-PerfBaselines {
    param([string]$Path)
    $result = @{}
    if (-not (Test-Path $Path -PathType Leaf)) { return $result }
    foreach ($row in (Import-Csv -Path $Path)) {
        $result[$row.app] = @{
            peep_cycles   = $row.peep_cycles
            nopeep_cycles = $row.nopeep_cycles
        }
    }
    return $result
}

# Writes a perf-baseline hashtable (same shape as Get-PerfBaselines returns)
# back out as tests/perf_baselines.csv, one row per app, sorted by app name so
# the file diffs cleanly in code review.
function Set-PerfBaselines {
    param(
        [string]$Path,
        [System.Collections.IDictionary]$Baselines
    )
    $rows = foreach ($app in ($Baselines.Keys | Sort-Object)) {
        [pscustomobject]@{
            app           = $app
            peep_cycles   = $Baselines[$app].peep_cycles
            nopeep_cycles = $Baselines[$app].nopeep_cycles
        }
    }
    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $rows | Export-Csv -Path $Path -NoTypeInformation
}

# Compares this run's measured Z80 cycle counts (already collected as a side
# effect of the normal build+run+verify pass - see the -p note above
# $emulatorRunArgs) against tests/perf_baselines.csv, for whichever mode(s)
# ("peep" and/or "nopeep") this invocation actually built. Only a passing
# app's cycle count is trusted - a broken build/run's count means nothing.
# Returns a hashtable with Regressions/Improvements/New arrays; the caller
# decides how that affects the exit code and, under -UpdatePerfBaseline,
# whether to write the file instead of comparing against it.
function Test-PerfRegressions {
    param(
        [object[]]$Results,
        [string[]]$ModesRun,
        [string]$BaselineFile,
        [string[]]$IgnoreApps = @(),
        [switch]$UpdateBaseline
    )
    $ignoreSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$IgnoreApps)

    $existing = Get-PerfBaselines -Path $BaselineFile
    $updated = @{}
    foreach ($app in $existing.Keys) {
        $updated[$app] = @{ peep_cycles = $existing[$app].peep_cycles; nopeep_cycles = $existing[$app].nopeep_cycles }
    }

    $regressions = [System.Collections.Generic.List[object]]::new()
    $improvements = [System.Collections.Generic.List[object]]::new()
    $newEntries = [System.Collections.Generic.List[object]]::new()

    foreach ($result in $Results) {
        if (-not $result.Passed) { continue }
        if (-not $result.Metrics) { continue }
        if ($ignoreSet.Contains($result.App)) { continue }
        foreach ($modeKey in $ModesRun) {
            if (-not $result.Metrics.ContainsKey($modeKey)) { continue }
            $cyclesRaw = $result.Metrics[$modeKey].Cycles
            if (-not $cyclesRaw) { continue }
            $cycles = [long]$cyclesRaw
            $col = "${modeKey}_cycles"

            if ($UpdateBaseline) {
                if (-not $updated.ContainsKey($result.App)) {
                    $updated[$result.App] = @{ peep_cycles = ""; nopeep_cycles = "" }
                }
                $updated[$result.App][$col] = $cycles
                continue
            }

            $hasBaseline = $existing.ContainsKey($result.App) -and $existing[$result.App][$col]
            if (-not $hasBaseline) {
                $newEntries.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Cycles = $cycles })
                continue
            }

            $baseline = [long]$existing[$result.App][$col]
            if ($cycles -gt $baseline) {
                $regressions.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Baseline = $baseline; Actual = $cycles })
            }
            elseif ($cycles -lt $baseline) {
                $improvements.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Baseline = $baseline; Actual = $cycles })
            }
        }
    }

    if ($UpdateBaseline) {
        Set-PerfBaselines -Path $BaselineFile -Baselines $updated
    }

    return [pscustomobject]@{
        Regressions  = $regressions.ToArray()
        Improvements = $improvements.ToArray()
        New          = $newEntries.ToArray()
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
        "-All",
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
    $tmbDef       = ${function:Test-MatchesBaseline}.ToString()
    $iatDef       = ${function:Invoke-AppTest}.ToString()
    $cfuDef       = ${function:Copy-FixtureUpper}.ToString()
    $ctbsDef      = ${function:ConvertTo-BooleanSetting}.ToString()
    $gdmDef       = ${function:Get-DccMakeCommand}.ToString()
    $idmbDef      = ${function:Invoke-DccMakeBuild}.ToString()
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
        ${function:Test-MatchesBaseline} = $using:tmbDef
        ${function:Copy-FixtureUpper}    = $using:cfuDef
        ${function:ConvertTo-BooleanSetting} = $using:ctbsDef
        ${function:Get-DccMakeCommand}   = $using:gdmDef
        ${function:Invoke-DccMakeBuild}  = $using:idmbDef
        ${function:Invoke-AppTest}       = $using:iatDef

        $appBuildDir = Join-Path $using:BuildDir $item.App
        Invoke-AppTest -AppName $item.App -Modes $using:modes -BuildDir $appBuildDir `
            -BaselineDir $using:BaselineDir -Emulator $using:Emulator `
            -Placeholders $using:Placeholders -RunArgs $item.RunArgs `
            -RunStdin $item.RunStdin `
            -StackSize $item.StackSize -DccArgs $item.DccArgs `
            -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
            -EmulatorRunArgs $using:runArgs `
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
            -StackSize $item.StackSize -DccArgs $item.DccArgs `
            -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
            -EmulatorRunArgs $emulatorRunArgs `
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

# Cycle-count regression check: on by default, needs no separate run (see the
# -p note above $emulatorRunArgs) - it just compares/updates
# tests/perf_baselines.csv using the Z80 cycle counts already captured during
# the pass above, for whichever mode(s) this invocation built.
#
# Requires $StackCheck to be on, matching the build configuration the
# baseline file is captured under (a plain default run). The stack-check
# guard adds a prologue check to every function, so a -NoStackCheck (or
# -Report, which implies it) build's cycle counts are not comparable to a
# stack-checked baseline at all - every app would show some spurious
# difference having nothing to do with a real codegen change (found via
# -Report incorrectly flagging sieve as regressed by a tiny, build-config-
# only amount immediately after this feature's first parallel/no-throttle
# fix, since -Report was made to run the check too once it stopped forcing
# -Serial).
$perfCheck = $null
if ((Test-IsNtvcmEmulator $Emulator) -and $StackCheck -and (-not $NoPerfCheck -or $UpdatePerfBaseline)) {
    $perfIgnoreApps = @($testFiles | Where-Object { Get-PerfIgnoreApp $_ })
    $perfCheck = Test-PerfRegressions -Results $results -ModesRun $modes `
        -BaselineFile $PerfBaselineFile -IgnoreApps $perfIgnoreApps -UpdateBaseline:$UpdatePerfBaseline

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "PERFORMANCE (Z80 CYCLE COUNT) CHECK" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    if ($UpdatePerfBaseline) {
        Write-Host "  Updated $PerfBaselineFile for mode(s): $($modes -join ', ')" -ForegroundColor Cyan
    }
    else {
        if ($perfCheck.Regressions.Count -eq 0) {
            Write-Host "  Regressions:  0" -ForegroundColor Green
        }
        else {
            Write-Host "  Regressions:  $($perfCheck.Regressions.Count)" -ForegroundColor Red
            foreach ($r in $perfCheck.Regressions) {
                $pct = if ($r.Baseline -gt 0) { [math]::Round((($r.Actual - $r.Baseline) / $r.Baseline) * 100, 2) } else { 0 }
                Write-Host ("    - {0,-12} ({1,-6}) {2:N0} -> {3:N0} cycles (+{4}%)" -f $r.App, $r.Mode, $r.Baseline, $r.Actual, $pct) -ForegroundColor Red
            }
            $failed += $perfCheck.Regressions.Count
        }
        if ($perfCheck.Improvements.Count -gt 0) {
            Write-Host "  Improvements: $($perfCheck.Improvements.Count)" -ForegroundColor Green
            foreach ($i in $perfCheck.Improvements) {
                $pct = if ($i.Baseline -gt 0) { [math]::Round((($i.Baseline - $i.Actual) / $i.Baseline) * 100, 2) } else { 0 }
                Write-Host ("    - {0,-12} ({1,-6}) {2:N0} -> {3:N0} cycles (-{4}%)" -f $i.App, $i.Mode, $i.Baseline, $i.Actual, $pct) -ForegroundColor Green
            }
            Write-Host "  (run -UpdatePerfBaseline to accept improved cycle counts)" -ForegroundColor DarkGray
        }
        if ($perfCheck.New.Count -gt 0) {
            Write-Host "  No baseline yet for $($perfCheck.New.Count) app/mode pair(s); run -UpdatePerfBaseline to capture" -ForegroundColor DarkGray
        }
    }
}

$diagnosticsPassed = $null
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RUNNING DIAGNOSTICS SUITE" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
& pwsh (Join-Path $PSScriptRoot "run-diagnostics.ps1") -Dcc (Join-Path $script:RepoRoot "dcc")
$diagnosticsExitCode = $LASTEXITCODE
$diagnosticsPassed = ($diagnosticsExitCode -eq 0)
if (-not $diagnosticsPassed) {
    $failed++
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
Write-Host "  Diagnostics:  $(if ($diagnosticsPassed) { 'passed' } else { 'failed' })" -ForegroundColor $(if ($diagnosticsPassed) { "Green" } else { "Red" })
if ($perfCheck -and -not $UpdatePerfBaseline) {
    $perfLabel = if ($perfCheck.Regressions.Count -eq 0) { "passed" } else { "$($perfCheck.Regressions.Count) regression(s)" }
    Write-Host "  Performance:  $perfLabel" -ForegroundColor $(if ($perfCheck.Regressions.Count -eq 0) { "Green" } else { "Red" })
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
if (-not $diagnosticsPassed) {
    Write-Host ""
    Write-Host "Diagnostics suite failed" -ForegroundColor Red
}
if ($perfCheck -and -not $UpdatePerfBaseline -and $perfCheck.Regressions.Count -gt 0) {
    Write-Host ""
    Write-Host "Performance regressions (vs $PerfBaselineFile):" -ForegroundColor Red
    foreach ($r in $perfCheck.Regressions) {
        Write-Host "  - $($r.App) ($($r.Mode)): $($r.Baseline) -> $($r.Actual) cycles" -ForegroundColor Red
    }
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
    Remove-RunBuildDir
    exit 0
} else {
    Write-Host ">>> FAILURE: $failed test(s) failed <<<" -ForegroundColor Red
    if ($script:RunBuildRoot -and -not $KeepBuild) {
        Write-Host "  (build artifacts removed; re-run with -KeepBuild to retain them for debugging)" -ForegroundColor DarkGray
    }
    Restore-TerminalState
    Remove-RunBuildDir
    exit 1
}
