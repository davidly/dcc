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

.PARAMETER NoRamDisk
    Linux only: disable automatic use of /dev/shm/dcc-runall for build artifacts.
    By default on Linux, the suite uses /dev/shm when writable and falls back to
    BuildDir when unavailable.

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

.PARAMETER FailFast
    Stop dispatching new work as soon as the first correctness failure or
    per-app performance regression is observed. Apps already in flight when
    the trigger fires still finish (parallel workers cannot be cancelled
    mid-run), but no further apps are started, and the run's exit code still
    reflects only the failures actually observed - it does not treat
    not-yet-run apps as failed. Skipped apps are reported separately from
    pass/fail counts. Use this to shorten the feedback loop while iterating;
    prefer a full unthrottled run (without -FailFast) for the final
    pre-commit validation so every app's status is known.

.PARAMETER Extended
    Also run the extended c-testsuite single-exec corpus after the main app suite.

.PARAMETER RunTimeout
    Per-test timeout in seconds for each build pass and each emulator run
    (default: 60). A timeout terminates the process and its descendants.

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
    measured cycle counts and .COM sizes. Only the column(s) for the mode(s)
    this invocation actually built are touched (peep_cycles/peep_size for
    -Mode fast, nopeep_cycles/nopeep_size for -Mode nopeep, all four for
    -Mode full) - any other app/column already in the file is left untouched.
    Use after an intentional, verified change moves cycle counts or .COM
    sizes (an improvement, or an accepted tradeoff).

.PARAMETER PerfBaselineFile
    CSV path for the cycle-count and .COM-size regression baseline (default:
    "tests/perf_baselines.csv"). One row per app, columns app,peep_cycles,
    nopeep_cycles,peep_size,nopeep_size. Checked into the repo like the
    output baselines in tests/baselines/, so a regression shows up as a diff
    in code review the same way an output-baseline change would. Size
    catches a class of regression cycle count alone cannot: dead code linked
    in but never executed (e.g. dccrtlstrip pulling in an RTL block a
    program never calls, because it happens to share a keep/strip unit with
    one it does) changes nothing on any executed path, so it's invisible to
    cycle counts but shows up immediately as a larger .COM.

.PARAMETER NarrowDiff
    After the main suite, build every app a second and third time - once
    normally, once with dcc's -fno-narrow (every int-array/scalar/for-counter
    byte-narrowing pass disabled) - and diff the two runs' raw stdout directly
    against each other, instead of against a recorded baseline. Narrowing is
    supposed to be invisible (a storage-width optimization, never a value
    change), so any difference is a real correctness bug in a narrowing pass -
    caught without needing an external reference compiler, and without the
    bug needing to already be reflected in some existing baseline's expected
    text. Off by default (extra build+run work per app); a mismatch fails the
    run just like an output-baseline mismatch does.

.PARAMETER KeepBuild
    In parallel mode the suite builds into a per-invocation folder
    (build/run-<pid>) so concurrent runs stay isolated, and removes it on exit to
    keep build/ from accumulating run-* folders. Pass -KeepBuild to retain that
    folder (e.g. to inspect failing build artifacts).

.PARAMETER TimingBreakdown
    Print a percentage breakdown of where wall-clock time went, for
    diagnosing suite-level slowdowns. Two sections:
      - Top-level phases (main app suite / perf check / diagnostics /
        dccpeep fixtures / narrow-diff / extended suite), each as a % of
        this run's total time - the same stopwatches already used for
        "Total time" above, just also reported per phase.
      - Build pipeline, aggregated across every app and mode built by the
        main suite: dcc / dccpeep / m80 assembly / dccrtlstrip / L80 link
        (each summed from a per-phase wall-clock timing line dccmake itself
        now prints - see src/dccmake/dccmake.c's now_ms()/run_build), plus
        the ntvcm execution time already captured for the cycle-count check.
        Both dccmake's own reported total and ntvcm's own reported elapsed
        milliseconds are measured from INSIDE those processes, so they
        exclude the actual OS process-create/exec cost and PowerShell's own
        overhead capturing/merging their output via 2>&1 - comparing each
        against a PowerShell-side Stopwatch wrapping the same call splits
        that out into its own "dccmake spawn" / "ntvcm spawn" bucket, with
        whatever's left (fixture staging, baseline comparison, parallel
        dispatch/collection) in a final "other script" bucket. This second
        section is normalized to the sum of all apps' work, NOT wall-clock
        time (parallel execution makes wall-clock time much smaller than
        that sum) - it answers "of all the work the suite did, what fraction
        was which pipeline stage", not "what fraction of the run's wall
        time".

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
  pwsh ./scripts/runall.ps1 -NarrowDiff

.NOTES
  App overrides are loaded from tests/_test_overrides.json:
    - args: command-line arguments for the app
        - stdin: text piped to app stdin during execution
    - stack_size: C stack reserve override (default 512)
    - fixtures: CP/M data files (from tests/) this app reads at runtime,
      copied into its build dir under their UPPERCASE name before it runs -
      e.g. cobint needs E.COB, but no other app does, so only cobint
      declares it. Most apps read/write only their own scratch files and
      need no fixtures at all (the default, an empty list).
    - ignore: set to true to skip building/running this app
    - perf_ignore: set to true to exclude this app from the cycle-count
      regression check entirely (never a regression, improvement, or new
      baseline) - for a test whose cycle count is known to vary run-to-run
      for reasons unrelated to codegen, e.g. tkbd (a keyboard-poll loop whose
      iteration count depends on real host timing) or tdirent/cpmenumd
      (directory enumeration order depends on real filesystem state).
    - narrow_diff_ignore: set to true to exclude this app from -NarrowDiff
      entirely - for a test whose observable output is inherently sensitive
      to overall stack/frame layout in a way unrelated to any narrowing bug,
      e.g. tstackov/tpragstk (both intentionally recurse until the
      stack-check guard fires, and print exactly how far the stack
      overran - narrowing shrinks locals elsewhere in the program, shifting
      that number even though nothing is actually wrong).

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
    [switch]$UseEmulatedM80,
    [string]$BuildDir = "build",
    [switch]$NoRamDisk,
    [string]$BaselineDir = "tests/baselines",
    [ValidateSet("fast", "nopeep", "full")]
    [string]$Mode = "fast",
    [switch]$Help,
    [switch]$Extended,
    [string]$Apps = "",
    [int]$RunTimeout = 60,
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [switch]$Report,
    [string]$ReportFile = "perf_results.csv",
    [long]$ReportClockHz = 400000000,
    [switch]$NoPerfCheck,
    [switch]$UpdatePerfBaseline,
    [string]$PerfBaselineFile = "tests/perf_baselines.csv",
    [switch]$NarrowDiff,
    [switch]$KeepBuild,
    [switch]$TimingBreakdown,
    [switch]$FailFast,
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

# On Linux, default build artifacts to tmpfs for lower I/O latency.
# If /dev/shm is unavailable or opt-out is requested, keep the normal BuildDir.
if ($IsLinux -and -not $NoRamDisk) {
    $ramRoot = "/dev/shm"
    if (Test-Path $ramRoot -PathType Container) {
        $candidate = Join-Path $ramRoot "dcc-runall"
        try {
            New-Item -ItemType Directory -Path $candidate -Force -ErrorAction Stop | Out-Null
            $probe = Join-Path $candidate ".rw-probe-$PID.tmp"
            Set-Content -LiteralPath $probe -Value "ok" -NoNewline -ErrorAction Stop
            Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
            $BuildDir = $candidate
            Write-Host "--- build root: using Linux tmpfs at $BuildDir (pass -NoRamDisk to disable) ---" -ForegroundColor Cyan
        }
        catch {
            Write-Host "--- build root: /dev/shm unavailable or not writable, using '$BuildDir' ---" -ForegroundColor DarkGray
        }
    }
    else {
        Write-Host "--- build root: /dev/shm not present, using '$BuildDir' ---" -ForegroundColor DarkGray
    }
}

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
        if ($app.narrow_diff_ignore) { $appOverrides[$app.name]['narrow_diff_ignore'] = $app.narrow_diff_ignore }
        if ($app.fixtures) { $appOverrides[$app.name]['fixtures'] = @($app.fixtures) }
        if ($app.extra_scenarios) { $appOverrides[$app.name]['extra_scenarios'] = @($app.extra_scenarios) }
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

if ($Apps) {
    $requestedApps = @($Apps -split ',' | ForEach-Object {
        $_.Trim().ToLowerInvariant()
    } | Where-Object { $_ })
    $unknownApps = @($requestedApps | Where-Object { $_ -notin $testFiles })
    if ($unknownApps.Count -gt 0) {
        Write-Error "Unknown app(s): $($unknownApps -join ', ')" -ErrorAction Stop
    }
    $testFiles = @($testFiles | Where-Object { $_ -in $requestedApps })
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

# CP/M data fixtures (interpreter source files, weight files, etc.) this
# specific app reads at runtime, declared explicitly in the app's
# _test_overrides.json entry - e.g. cobint needs E.COB, but no other app does.
# Defaults to none: most apps read/write only their own scratch files and
# need nothing staged.
function Get-AppFixtures {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['fixtures']) {
        return @($appOverrides[$app]['fixtures'])
    }
    return @()
}

# Additional (args, fixtures) scenarios to run against the SAME already-built
# COM for this app, e.g. pint also runs against TTT.PAS in addition to its
# primary E.PAS fixture - each scenario gets its own baseline
# (tests/baselines/<app>_<suffix>.txt) so a bug that only manifests with one
# input's data/control-flow shape (see commit 077d30c) doesn't silently pass
# just because the primary fixture works.
function Get-AppExtraScenarios {
    param([string]$app)
    if ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['extra_scenarios']) {
        return @($appOverrides[$app]['extra_scenarios'])
    }
    return @()
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

# True for an app whose observable output is inherently sensitive to overall
# code size/frame layout in a way unrelated to any narrowing bug - e.g.
# tests/tstackov.c and tests/tpragstk.c both intentionally recurse until the
# stack-check guard fires, and print exactly how far the stack pointer
# overran its reserve ("exceeded by 0xNNNN"); narrowing shrinks locals
# elsewhere in the program, shifting the overall stack layout by a few bytes
# and changing that number even though nothing is actually wrong. Excluded
# from -NarrowDiff entirely, the same way perf_ignore excludes a
# timing-sensitive app from the cycle-count check.
function Get-NarrowDiffIgnoreApp {
    param([string]$app)
    return ($appOverrides.ContainsKey($app) -and $appOverrides[$app]['narrow_diff_ignore'])
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

function Stop-ProcessTree {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process -or $Process.HasExited) { return }
    if ($IsWindows) {
        try { $Process.Kill($true) } catch { try { $Process.Kill() } catch { } }
        return
    }

    $children = @{}
    try {
        foreach ($line in @(& ps -axo pid=,ppid= 2>$null)) {
            if ($line -match '^\s*(\d+)\s+(\d+)\s*$') {
                $pidValue = [int]$Matches[1]
                $parentValue = [int]$Matches[2]
                if (-not $children.ContainsKey($parentValue)) {
                    $children[$parentValue] = [System.Collections.Generic.List[int]]::new()
                }
                $children[$parentValue].Add($pidValue)
            }
        }
        $pending = [System.Collections.Generic.Stack[int]]::new()
        $descendants = [System.Collections.Generic.List[int]]::new()
        $pending.Push($Process.Id)
        while ($pending.Count -gt 0) {
            $parent = $pending.Pop()
            if (-not $children.ContainsKey($parent)) { continue }
            foreach ($child in $children[$parent]) {
                $descendants.Add($child)
                $pending.Push($child)
            }
        }
        for ($index = $descendants.Count - 1; $index -ge 0; --$index) {
            try { Stop-Process -Id $descendants[$index] -Force -ErrorAction SilentlyContinue } catch { }
        }
    }
    catch { }
    try { $Process.Kill($true) } catch { try { $Process.Kill() } catch { } }
}

function Invoke-ProcessWithTimeout {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSeconds,
        [string]$StandardInput = ""
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add([string]$argument)
    }
    try {
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) { throw "failed to start $FilePath" }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if ($StandardInput) {
            $process.StandardInput.Write($StandardInput)
            if (-not $StandardInput.EndsWith("`n")) {
                $process.StandardInput.Write("`n")
            }
        }
        $process.StandardInput.Close()
        $timedOut = $TimeoutSeconds -gt 0 -and
            -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) {
            Stop-ProcessTree -Process $process
            $process.WaitForExit()
        }
        else {
            $process.WaitForExit()
        }
        $output = $stdoutTask.GetAwaiter().GetResult() +
                  $stderrTask.GetAwaiter().GetResult()
        return [pscustomobject]@{
            ExitCode = if ($timedOut) { -1 } else { $process.ExitCode }
            TimedOut = $timedOut
            Output = $output
        }
    }
    catch {
        return [pscustomobject]@{ ExitCode = 1; TimedOut = $false; Output = "$_" }
    }
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
        [switch]$UseEmulatedM80,
        [switch]$Quiet,
        [int]$TimeoutSeconds = 60,
        [ref]$TimingOut
    )

    $modeLower = $Mode.ToLowerInvariant()
    if ($modeLower -eq "full") {
        $fastTiming = $null
        $nopeepTiming = $null
        $fastOk = Invoke-DccMakeBuild -Name $Name -Mode fast -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -UseEmulatedM80:$UseEmulatedM80 -Quiet:$Quiet -TimeoutSeconds $TimeoutSeconds -TimingOut ([ref]$fastTiming)
        $nopeepOk = Invoke-DccMakeBuild -Name $Name -Mode nopeep -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -UseEmulatedM80:$UseEmulatedM80 -Quiet:$Quiet -TimeoutSeconds $TimeoutSeconds -TimingOut ([ref]$nopeepTiming)
        if ($TimingOut) { $TimingOut.Value = @{ peep = $fastTiming; nopeep = $nopeepTiming } }
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
        if (-not $Quiet) { Write-Warning "Source file not found for: $Name" }
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
    if ($UseEmulatedM80) {
        $args += "dcc-use-emulated-m80=true"
    }
    if ($DccArgs) {
        $args += @($DccArgs -split '\s+' | Where-Object { $_ })
    }

    $dccmakeSw = [System.Diagnostics.Stopwatch]::StartNew()
    $buildResult = Invoke-ProcessWithTimeout -FilePath $dccmake -Arguments $args `
        -WorkingDirectory (Get-Location).Path -TimeoutSeconds $TimeoutSeconds
    $dccmakeSw.Stop()
    $buildOut = @($buildResult.Output -split "`r?`n")
    $exitCode = $buildResult.ExitCode
    if ($TimingOut) {
        $timingLine = @($buildOut) | Where-Object { $_ -match '^dccmake: timing ' } | Select-Object -Last 1
        if ($timingLine -match 'dcc=(\d+)ms peep=(\d+)ms asm=(\d+)ms\((\w+)\) rtlstrip=(\d+)ms link=(\d+)ms other=(\d+)ms total=(\d+)ms') {
            $TimingOut.Value = @{
                dcc      = [int]$Matches[1]
                peep     = [int]$Matches[2]
                asm      = [int]$Matches[3]
                asmMode  = $Matches[4]
                rtlstrip = [int]$Matches[5]
                link     = [int]$Matches[6]
                other    = [int]$Matches[7]
                total    = [int]$Matches[8]
                psInvokeMs = $dccmakeSw.ElapsedMilliseconds
            }
        }
    }
    if (-not $Quiet) { $buildOut | Write-Host }
    if ($buildResult.TimedOut) {
        if (-not $Quiet) { Write-Warning "dccmake timed out for $Name ($Mode) after ${TimeoutSeconds}s" }
        return $false
    }
    if ($exitCode -ne 0) {
        if (-not $Quiet) { Write-Warning "dccmake failed for $Name ($Mode) with exit code $exitCode" }
        return $false
    }

    $buildWarnings = @($buildOut | Where-Object { $_ -match '%Mult\. Def\.|%Phase error|%Undefined' })
    if ($buildWarnings) {
        $buildWarnings | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
        if (-not $Quiet) { Write-Warning "Build warnings treated as errors for $Name" }
        return $false
    }

    $appCom = Join-Path $BuildDir "$upperBase.COM"
    if (Test-Path -LiteralPath $appCom -PathType Leaf) {
        return $true
    }

    if (-not $Quiet) { Write-Warning "Build failed: .COM file not produced for $Name" }
    return $false
}

# Stage the declared CP/M data fixtures into the shared build dir (serial
# mode) - the union of every app's own Get-AppFixtures list, not every file in
# tests/: some interpreters read these files from the current working
# directory, and apps are run from the build dir (below), so the fixtures
# must live there too.
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

# Run one already-built COM once with a given set of args/stdin, strip the
# ntvcm perf-report block, and compare the remaining output against an
# (optional) expected baseline. Shared by Invoke-AppTest's primary run and
# each of its extra scenario runs (see Get-AppExtraScenarios) so a single COM
# can be exercised against multiple fixtures without rebuilding.
function Invoke-ComRunAndCompare {
    param(
        [string]$BuildDir,
        [string]$ComFileName,
        [string]$Emulator,
        [string[]]$EmulatorRunArgs,
        [string]$RunArgs,
        [string]$RunStdin,
        [bool]$HasBaseline,
        [string]$Expected,
        [System.Collections.IDictionary]$Placeholders,
        [string]$BaselinePath,
        [string]$DiffPrefix,
        [System.Collections.Generic.List[string]]$Lines,
        [int]$RunTimeout
    )

    Push-Location $BuildDir
    $runSw = [System.Diagnostics.Stopwatch]::StartNew()
    $runFailed = $false
    $runTimedOut = $false
    try {
        $appArgs = if ($RunArgs) { @($RunArgs -split '\s+') } else { @() }
        $nativeArgs = @($EmulatorRunArgs) + @($ComFileName) + $appArgs
        $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $Emulator
        $startInfo.WorkingDirectory = (Get-Location).Path
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardInput = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        foreach ($nativeArg in $nativeArgs) {
            $startInfo.ArgumentList.Add([string]$nativeArg)
        }
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            throw "failed to start emulator"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if ($RunStdin) {
            $process.StandardInput.Write($RunStdin)
            if (-not $RunStdin.EndsWith("`n")) {
                $process.StandardInput.Write("`n")
            }
        }
        $process.StandardInput.Close()
        if (-not $process.WaitForExit($RunTimeout * 1000)) {
            Stop-ProcessTree -Process $process
            $process.WaitForExit()
            $Lines.Add("    ${DiffPrefix}ERROR running ${ComFileName}: timed out after ${RunTimeout}s")
            $runTimedOut = $true
        } elseif ($process.ExitCode -ne 0) {
            $Lines.Add("    ${DiffPrefix}Emulator exit code: $($process.ExitCode)")
        }
        $output = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($stderr) {
            $output += $stderr
        }
    }
    catch {
        $output = ""
        $Lines.Add("    ${DiffPrefix}ERROR running ${ComFileName}: $_")
        $runFailed = $true
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

    $result = [pscustomobject]@{
        Passed      = -not ($runTimedOut -or $runFailed)
        Ms          = if ($ntvcmMs -ne "") { $ntvcmMs } else { $runSw.ElapsedMilliseconds }
        Cycles      = $ntvcmCycles
        ClockHz     = $ntvcmClockHz
        PsElapsedMs = $runSw.ElapsedMilliseconds
    }

    if ($HasBaseline) {
        $actual = ($output -replace "`r`n", "`n").TrimEnd("`n")
        if (-not (Test-MatchesBaseline -Actual $actual -Baseline $Expected -Placeholders $Placeholders)) {
            $Lines.Add("    ${DiffPrefix}OUTPUT MISMATCH (vs $BaselinePath)")
            $expLines = if ($Expected) { @($Expected -split "`n") } else { @() }
            $actLines = if ($actual)   { @($actual   -split "`n") } else { @() }
            $maxLen = [Math]::Max($expLines.Count, $actLines.Count)
            for ($i = 0; $i -lt $maxLen; $i++) {
                $e = if ($i -lt $expLines.Count) { $expLines[$i] } else { $null }
                $a = if ($i -lt $actLines.Count) { $actLines[$i] } else { $null }
                if ($null -eq $a) {
                    $Lines.Add("    ${DiffPrefix}DIFF- $e")
                } elseif ($null -eq $e) {
                    $Lines.Add("    ${DiffPrefix}DIFF+ $a")
                } elseif ($e -ceq $a) {
                    $Lines.Add("    ${DiffPrefix}DIFF  $e")
                } else {
                    $Lines.Add("    ${DiffPrefix}DIFF- $e")
                    $Lines.Add("    ${DiffPrefix}DIFF+ $a")
                }
            }
            $result.Passed = $false
        }
        else {
            $Lines.Add("    ${DiffPrefix}Output matches baseline")
        }
    }
    else {
        $Lines.Add("    ${DiffPrefix}ERROR: no baseline at $BaselinePath (every app/scenario must have one)")
        $result.Passed = $false
    }

    return $result
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
        [switch]$UseEmulatedM80,
        [string[]]$EmulatorRunArgs,
        [object[]]$Fixtures,
        [bool]$StageFixtures = $true,
        [object[]]$ExtraScenarios = @(),
        [int]$RunTimeout
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $appPassed = $true
    $modeMetrics = @{}

    # Ensure the (possibly per-app) build dir exists and stage CP/M fixtures
    # (the primary fixtures plus every extra scenario's own fixtures - all
    # scenarios run against the same build, so everything must be staged
    # before any run happens).
    if (-not (Test-Path $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    if ($StageFixtures) {
        foreach ($f in $Fixtures) {
            Copy-FixtureUpper -Fixture $f -DestDir $BuildDir
        }
        foreach ($scenario in $ExtraScenarios) {
            foreach ($f in @($scenario.fixtures)) {
                Copy-FixtureUpper -Fixture $f -DestDir $BuildDir
            }
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

    # Same, per extra scenario - each gets its own baseline file
    # (tests/baselines/<AppName>_<suffix>.txt).
    $scenarioBaselines = @{}
    foreach ($scenario in $ExtraScenarios) {
        $sBlPath = Join-Path $BaselineDir "${AppName}_$($scenario.suffix).txt"
        $sHasBaseline = Test-Path $sBlPath -PathType Leaf
        $sExpected = $null
        if ($sHasBaseline) {
            $sExpected = (((Get-Content -Path $sBlPath -Raw) -replace "`r`n", "`n")).TrimEnd("`n")
        }
        $scenarioBaselines[$scenario.suffix] = [pscustomobject]@{
            Path        = $sBlPath
            HasBaseline = $sHasBaseline
            Expected    = $sExpected
        }
    }

    $buildTimingByMode = @{}
    foreach ($buildMode in $Modes) {
        $displayMode = if ($buildMode -eq "peep") { "fast" } else { $buildMode }
        $stackSizeInt = if ($StackSize) { [int]$StackSize } else { 0 }
        $ok = $false
        $modeTiming = $null
        try {
            $ok = Invoke-DccMakeBuild -Name $AppName -Mode $buildMode -BuildDir $BuildDir -Emulator $Emulator -StackSize $stackSizeInt -DccArgs $DccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -UseEmulatedM80:$UseEmulatedM80 -Quiet -TimeoutSeconds $RunTimeout -TimingOut ([ref]$modeTiming)
        }
        catch { $ok = $false }
        if ($modeTiming) { $buildTimingByMode[$buildMode] = $modeTiming }

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

        $primaryResult = Invoke-ComRunAndCompare -BuildDir $BuildDir -ComFileName "$upper.COM" `
            -Emulator $Emulator -EmulatorRunArgs $EmulatorRunArgs -RunArgs $RunArgs -RunStdin $RunStdin `
            -HasBaseline $hasBaseline -Expected $expected -Placeholders $Placeholders `
            -BaselinePath "$BaselineDir/$AppName.txt" -DiffPrefix "" -Lines $lines `
            -RunTimeout $RunTimeout

        $modeMetrics[$buildMode] = [pscustomobject]@{
            Ms          = $primaryResult.Ms
            Cycles      = $primaryResult.Cycles
            ClockHz     = $primaryResult.ClockHz
            Size        = $comSize
            PsRunMs     = $primaryResult.PsElapsedMs
        }
        if (-not $primaryResult.Passed) { $appPassed = $false }
        if (-not $hasBaseline) { break }

        # Rerun the SAME already-built COM against every extra scenario's own
        # args/fixtures and baseline - no rebuild, since the fixture data is
        # a runtime argv input, not a different source file. A scenario
        # failure marks the app failed but doesn't stop other scenarios/modes
        # from also being checked and reported.
        foreach ($scenario in $ExtraScenarios) {
            $sb = $scenarioBaselines[$scenario.suffix]
            $scenarioResult = Invoke-ComRunAndCompare -BuildDir $BuildDir -ComFileName "$upper.COM" `
                -Emulator $Emulator -EmulatorRunArgs $EmulatorRunArgs -RunArgs $scenario.args `
                -RunStdin $(if ($scenario.stdin) { $scenario.stdin } else { "" }) `
                -HasBaseline $sb.HasBaseline -Expected $sb.Expected -Placeholders $Placeholders `
                -BaselinePath $sb.Path -DiffPrefix "[$($scenario.suffix)] " -Lines $lines `
                -RunTimeout $RunTimeout
            if (-not $scenarioResult.Passed) { $appPassed = $false }
        }
    }

    $sw.Stop()
    return [pscustomobject]@{
        App     = $AppName
        Passed  = $appPassed
        Elapsed = $sw.Elapsed
        Lines   = $lines.ToArray()
        Metrics = $modeMetrics
        Timing  = $buildTimingByMode
    }
}

# Builds one app TWICE - once normally, once with dcc's -fno-narrow (every
# int-array/scalar/for-counter byte-narrowing pass disabled) - and diffs the
# two runs' raw stdout directly against each other, rather than against a
# recorded baseline. Narrowing is supposed to be invisible (a storage-width
# optimization, never a value change), so any difference here is a real
# correctness bug in one of the narrowing passes - caught without needing the
# bug to already be reflected in some existing baseline's expected text, and
# without needing an external reference compiler. Motivated directly by this
# session's narrowing bugs (a vacuous-dependency proof gap, a byte-assign
# value-propagation bug) - both were only found by hand, by broadening a
# narrowing trigger and noticing an EXISTING test's baseline stopped matching;
# this generalizes that same check to run automatically, every time, for
# every test, independent of whether its baseline happens to already encode
# the affected value.
function Invoke-NarrowDiffTest {
    param(
        [string]$AppName,
        [string]$BuildDir,
        [string]$Emulator,
        [string]$RunArgs,
        [string]$RunStdin,
        [string]$StackSize,
        [string]$DccArgs,
        [object]$DccFloatio,
        [object]$DccLongio,
        [switch]$UseEmulatedM80,
        [string[]]$EmulatorRunArgs,
        [object[]]$Fixtures,
        [int]$RunTimeout
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $stackSizeInt = if ($StackSize) { [int]$StackSize } else { 0 }
    $upper = $AppName.ToUpper()

    $narrowDir = Join-Path $BuildDir "narrow"
    $noNarrowDir = Join-Path $BuildDir "nonarrow"
    foreach ($d in @($narrowDir, $noNarrowDir)) {
        if (-not (Test-Path $d -PathType Container)) {
            New-Item -ItemType Directory -Path $d -Force | Out-Null
        }
        foreach ($f in $Fixtures) { Copy-FixtureUpper -Fixture $f -DestDir $d }
    }

    $baseDccArgs = if ($DccArgs) { $DccArgs } else { "" }
    $noNarrowDccArgs = ("$baseDccArgs -fno-narrow").Trim()

    $okNarrow = $false
    $okNoNarrow = $false
    try {
        $okNarrow = Invoke-DccMakeBuild -Name $AppName -Mode fast -BuildDir $narrowDir -Emulator $Emulator `
            -StackSize $stackSizeInt -DccArgs $baseDccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -UseEmulatedM80:$UseEmulatedM80 -Quiet -TimeoutSeconds $RunTimeout
    } catch { $okNarrow = $false }
    try {
        $okNoNarrow = Invoke-DccMakeBuild -Name $AppName -Mode fast -BuildDir $noNarrowDir -Emulator $Emulator `
            -StackSize $stackSizeInt -DccArgs $noNarrowDccArgs -DccFloatio $DccFloatio -DccLongio $DccLongio -UseEmulatedM80:$UseEmulatedM80 -Quiet -TimeoutSeconds $RunTimeout
    } catch { $okNoNarrow = $false }

    if (-not $okNarrow -or -not $okNoNarrow) {
        $sw.Stop()
        $lines.Add("  Building $AppName (narrow-diff)... FAILED (narrow ok=$okNarrow, no-narrow ok=$okNoNarrow)")
        return [pscustomobject]@{ App = $AppName; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
    }

    $narrowCom = Join-Path $narrowDir "$upper.COM"
    $noNarrowCom = Join-Path $noNarrowDir "$upper.COM"
    if (-not (Test-Path $narrowCom) -or -not (Test-Path $noNarrowCom)) {
        $sw.Stop()
        $lines.Add("  WARNING: $AppName - .COM not produced for narrow-diff build")
        return [pscustomobject]@{ App = $AppName; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
    }

    $appArgs = if ($RunArgs) { @($RunArgs -split '\s+') } else { @() }
    $nativeArgs = @($EmulatorRunArgs) + @("$upper.COM") + $appArgs

    $narrowRun = Invoke-ProcessWithTimeout -FilePath $Emulator -Arguments $nativeArgs `
        -WorkingDirectory $narrowDir -TimeoutSeconds $RunTimeout -StandardInput $RunStdin
    $noNarrowRun = Invoke-ProcessWithTimeout -FilePath $Emulator -Arguments $nativeArgs `
        -WorkingDirectory $noNarrowDir -TimeoutSeconds $RunTimeout -StandardInput $RunStdin
    if ($narrowRun.TimedOut -or $noNarrowRun.TimedOut) {
        $sw.Stop()
        $lines.Add("    ERROR: narrow-diff emulator timed out after ${RunTimeout}s")
        return [pscustomobject]@{ App = $AppName; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
    }
    $outNarrow = $narrowRun.Output
    $outNoNarrow = $noNarrowRun.Output

    # Strip the ntvcm -p performance block: cycle counts legitimately differ
    # between a narrowed and unnarrowed build (that's the whole point of
    # narrowing), so it is not itself a correctness signal here.
    $outNarrow = [regex]::Replace($outNarrow, '(?s)\r?\n\s*elapsed milliseconds:.*$', '')
    $outNoNarrow = [regex]::Replace($outNoNarrow, '(?s)\r?\n\s*elapsed milliseconds:.*$', '')
    $normNarrow = ($outNarrow -replace "`r`n", "`n").TrimEnd("`n")
    $normNoNarrow = ($outNoNarrow -replace "`r`n", "`n").TrimEnd("`n")

    # Normalize __DATE__/__TIME__-shaped text before comparing: these two
    # builds are compiled moments apart, so a program that prints __DATE__ or
    # __TIME__ (e.g. tests/tstdc.c) can legitimately differ across a real
    # second/date boundary - unrelated to narrowing, and otherwise an
    # intermittent false positive. Narrower than the baseline harness's full
    # placeholder set deliberately: only these two specific volatile patterns
    # are normalized, not general hex/uint numbers, since a wrong NUMBER
    # printed by an actual narrowing bug is exactly the kind of difference
    # this check exists to catch.
    $normNarrow = [regex]::Replace($normNarrow, '[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}', '{{DATE}}')
    $normNarrow = [regex]::Replace($normNarrow, '\d{2}:\d{2}:\d{2}', '{{TIME}}')
    $normNoNarrow = [regex]::Replace($normNoNarrow, '[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}', '{{DATE}}')
    $normNoNarrow = [regex]::Replace($normNoNarrow, '\d{2}:\d{2}:\d{2}', '{{TIME}}')

    $match = ($normNarrow -ceq $normNoNarrow)
    if (-not $match) {
        $lines.Add("    NARROW-DIFF MISMATCH: narrowing changed observable output")
        $narrowLines = @($normNarrow -split "`n")
        $noNarrowLines = @($normNoNarrow -split "`n")
        $maxLen = [Math]::Max($narrowLines.Count, $noNarrowLines.Count)
        for ($i = 0; $i -lt $maxLen; $i++) {
            $n = if ($i -lt $narrowLines.Count) { $narrowLines[$i] } else { $null }
            $u = if ($i -lt $noNarrowLines.Count) { $noNarrowLines[$i] } else { $null }
            if ($n -cne $u) {
                $lines.Add("    DIFF- (narrow)    $n")
                $lines.Add("    DIFF+ (no-narrow) $u")
            }
        }
    }

    $sw.Stop()
    return [pscustomobject]@{
        App     = $AppName
        Passed  = $match
        Elapsed = $sw.Elapsed
        Lines   = $lines.ToArray()
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
        Fixtures     = (Get-AppFixtures $app)
        ExtraScenarios = (Get-AppExtraScenarios $app)
    })
}

# Union of every app's declared runtime fixtures, including each app's extra
# scenarios - used only by the serial shared build dir (Stage-FixtureInputs);
# parallel mode stages each app's own subset individually (see $item.Fixtures
# / $item.ExtraScenarios at each Invoke-AppTest call below), so most apps'
# build dirs get nothing copied at all instead of every fixture in tests/.
$fixtureList = @($workItems | ForEach-Object {
    @($_.Fixtures) + @($_.ExtraScenarios | ForEach-Object { $_.fixtures })
} | Select-Object -Unique)

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
                 elseif ($line -match '^    (\[\S+\] )?DIFF-') { 'Red' }
                 elseif ($line -match '^    (\[\S+\] )?DIFF\+') { 'Green' }
                 elseif ($line -match '^    (\[\S+\] )?DIFF  ') { 'DarkGray' }
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

# Loads tests/perf_baselines.csv (one row per app: app,peep_cycles,
# nopeep_cycles,peep_size,nopeep_size) into a hashtable keyed by app name.
# Returns an empty hashtable if the file does not exist yet (e.g. before the
# first -UpdatePerfBaseline run).
function Get-PerfBaselines {
    param([string]$Path)
    $result = @{}
    if (-not (Test-Path $Path -PathType Leaf)) { return $result }
    foreach ($row in (Import-Csv -Path $Path)) {
        $result[$row.app] = @{
            peep_cycles   = $row.peep_cycles
            nopeep_cycles = $row.nopeep_cycles
            peep_size     = $row.peep_size
            nopeep_size   = $row.nopeep_size
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
            peep_size     = $Baselines[$app].peep_size
            nopeep_size   = $Baselines[$app].nopeep_size
        }
    }
    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $csv = $rows | ConvertTo-Csv -NoTypeInformation
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $Path,
        (($csv -join "`n") + "`n"),
        $utf8NoBom)
}

# Compares this run's measured Z80 cycle counts and .COM sizes (both already
# collected as a side effect of the normal build+run+verify pass - see the -p
# note above $emulatorRunArgs for cycles; size is just the built .COM's byte
# length) against tests/perf_baselines.csv, for whichever mode(s) ("peep"
# and/or "nopeep") this invocation actually built. Only a passing app's
# metrics are trusted - a broken build/run's numbers mean nothing. Returns a
# hashtable with Regressions/Improvements/New arrays (each entry tagged with
# which Metric - "cycles" or "bytes" - it's about, since the two are
# independent: dead code linked in but never executed moves size without
# moving any executed path's cycle count, and vice versa a codegen change
# that shuffles instructions without changing what's linked in can move
# cycles without moving size); the caller decides how that affects the exit
# code and, under -UpdatePerfBaseline, whether to write the file instead of
# comparing against it.
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
        $updated[$app] = @{
            peep_cycles   = $existing[$app].peep_cycles
            nopeep_cycles = $existing[$app].nopeep_cycles
            peep_size     = $existing[$app].peep_size
            nopeep_size   = $existing[$app].nopeep_size
        }
    }

    $regressions = [System.Collections.Generic.List[object]]::new()
    $improvements = [System.Collections.Generic.List[object]]::new()
    $newEntries = [System.Collections.Generic.List[object]]::new()

    $perfMetrics = @(
        @{ Suffix = "cycles"; Prop = "Cycles"; Label = "cycles" },
        @{ Suffix = "size";   Prop = "Size";   Label = "bytes" }
    )

    foreach ($result in $Results) {
        if (-not $result.Passed) { continue }
        if (-not $result.Metrics) { continue }
        if ($ignoreSet.Contains($result.App)) { continue }
        foreach ($modeKey in $ModesRun) {
            if (-not $result.Metrics.ContainsKey($modeKey)) { continue }
            foreach ($metric in $perfMetrics) {
                $valueRaw = $result.Metrics[$modeKey].($metric.Prop)
                if (-not $valueRaw) { continue }
                $value = [long]$valueRaw
                $col = "${modeKey}_$($metric.Suffix)"

                if ($UpdateBaseline) {
                    if (-not $updated.ContainsKey($result.App)) {
                        $updated[$result.App] = @{ peep_cycles = ""; nopeep_cycles = ""; peep_size = ""; nopeep_size = "" }
                    }
                    $updated[$result.App][$col] = $value
                    continue
                }

                $hasBaseline = $existing.ContainsKey($result.App) -and $existing[$result.App][$col]
                if (-not $hasBaseline) {
                    $newEntries.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Metric = $metric.Label; Value = $value })
                    continue
                }

                $baseline = [long]$existing[$result.App][$col]
                if ($value -gt $baseline) {
                    $regressions.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Metric = $metric.Label; Baseline = $baseline; Actual = $value })
                }
                elseif ($value -lt $baseline) {
                    $improvements.Add([pscustomobject]@{ App = $result.App; Mode = $modeKey; Metric = $metric.Label; Baseline = $baseline; Actual = $value })
                }
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
        [int]$ThrottleLimit,
        [bool]$UseEmulatedM80
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
    if ($UseEmulatedM80) { $extendedArgs += "-UseEmulatedM80" }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "STARTING EXTENDED C-TESTSUITE" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    & pwsh @extendedArgs
    $script:ExtendedSuiteExitCode = $LASTEXITCODE
}

$results = @()
$totalToRun = $workItems.Count
$mainSuiteSw = [System.Diagnostics.Stopwatch]::StartNew()

# Precomputed once, up front, so -FailFast can run the identical per-app perf
# comparison (Test-PerfRegressions, unchanged) as each result streams in,
# rather than duplicating its regression formula.
$failFastPerfIgnoreApps = @($testFiles | Where-Object { Get-PerfIgnoreApp $_ })
$failFastPerfCheckActive = (Test-IsNtvcmEmulator $Emulator) -and $StackCheck -and (-not $NoPerfCheck) -and (-not $UpdatePerfBaseline)

if ($Parallel) {
    # Each worker runs in its own runspace with its own filesystem location, so
    # per-app build dirs (build/<app>) prevent the shared-file clobbering that
    # would otherwise occur (DCCRTL.MAC, RTLMIN.MAC, tool COMs, .COM outputs).
    $repoRoot     = (Get-Location).Path
    $tmbDef       = ${function:Test-MatchesBaseline}.ToString()
    $iatDef       = ${function:Invoke-AppTest}.ToString()
    $icrcDef      = ${function:Invoke-ComRunAndCompare}.ToString()
    $cfuDef       = ${function:Copy-FixtureUpper}.ToString()
    $ctbsDef      = ${function:ConvertTo-BooleanSetting}.ToString()
    $gdmDef       = ${function:Get-DccMakeCommand}.ToString()
    $sptDef       = ${function:Stop-ProcessTree}.ToString()
    $ipwtDef      = ${function:Invoke-ProcessWithTimeout}.ToString()
    $idmbDef      = ${function:Invoke-DccMakeBuild}.ToString()
    $stackCheckOn = [bool]$StackCheck
    $runArgs      = @($emulatorRunArgs)

    # Shared abort signal for -FailFast: a synchronized hashtable is a
    # reference type, so every parallel runspace observes live mutations made
    # by the collector below as soon as the first failure/regression is seen.
    # This cannot cancel work already dispatched (ForEach-Object -Parallel has
    # no built-in mid-run cancellation hook), but it stops the throttle pool
    # from starting any NEW app once triggered, bounding the extra work to
    # whatever was already in flight (at most ThrottleLimit-1 apps).
    $failFastState = [hashtable]::Synchronized(@{ Triggered = $false })

    # ForEach-Object -Parallel streams each worker's result as it completes, so
    # pipe straight into a loop that prints a live status line per app. Results
    # arrive in completion order (not sorted); we collect them for the summary.
    $done = 0
    $workItems | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        $item = $_
        if ($using:FailFast -and ($using:failFastState).Triggered) {
            # Abort already signalled before this item started: skip it
            # entirely rather than spending an emulator/build slot on it.
            return [pscustomobject]@{
                App = $item.App; Passed = $true; Skipped = $true
                Elapsed = [TimeSpan]::Zero; Lines = @(); Metrics = $null; Timing = $null
            }
        }
        Set-Location $using:repoRoot
        if ($using:stackCheckOn) { $env:DCC_FORCE_STACK_CHECK = "1" }
        # Bring the needed functions into this runspace.
        ${function:Test-MatchesBaseline} = $using:tmbDef
        ${function:Copy-FixtureUpper}    = $using:cfuDef
        ${function:ConvertTo-BooleanSetting} = $using:ctbsDef
        ${function:Get-DccMakeCommand}   = $using:gdmDef
        ${function:Stop-ProcessTree}     = $using:sptDef
        ${function:Invoke-ProcessWithTimeout} = $using:ipwtDef
        ${function:Invoke-DccMakeBuild}  = $using:idmbDef
        ${function:Invoke-ComRunAndCompare} = $using:icrcDef
        ${function:Invoke-AppTest}       = $using:iatDef

        $appBuildDir = Join-Path $using:BuildDir $item.App
        Invoke-AppTest -AppName $item.App -Modes $using:modes -BuildDir $appBuildDir `
            -BaselineDir $using:BaselineDir -Emulator $using:Emulator `
            -Placeholders $using:Placeholders -RunArgs $item.RunArgs `
            -RunStdin $item.RunStdin `
            -StackSize $item.StackSize -DccArgs $item.DccArgs `
            -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
            -UseEmulatedM80:$using:UseEmulatedM80 `
            -EmulatorRunArgs $using:runArgs `
            -Fixtures $item.Fixtures -StageFixtures $true `
            -ExtraScenarios $item.ExtraScenarios -RunTimeout $using:RunTimeout
    } | ForEach-Object {
        $result = $_
        $results += $result
        $done++
        $elapsed = $result.Elapsed
        $elapsedStr = if ($elapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $elapsed } else { "{0:0.00}s" -f $elapsed.TotalSeconds }
        $counter = "[{0,3}/{1}]" -f $done, $totalToRun
        $scTag = if ($StackCheck) { "Stack Check Enabled" } else { "No Stack Check" }
        if ($result.Skipped) {
            Write-Host ("{0} SKIP  {1,-12} (fail-fast: not started)" -f $counter, $result.App) -ForegroundColor DarkGray
            return
        }
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
                } elseif ($detail -match '^    (\[\S+\] )?DIFF-') {
                    Write-Host "        $($detail.Trim())" -ForegroundColor Red
                } elseif ($detail -match '^    (\[\S+\] )?DIFF\+') {
                    Write-Host "        $($detail.Trim())" -ForegroundColor Green
                }
            }
        }

        if ($FailFast -and (-not $failFastState.Triggered)) {
            if (-not $result.Passed) {
                $failFastState.Triggered = $true
                Write-Host ("  FAIL-FAST: correctness failure in {0} - no new apps will be started" -f $result.App) -ForegroundColor Yellow
            } elseif ($failFastPerfCheckActive) {
                # Reuses Test-PerfRegressions unchanged (Item 86 discipline:
                # one cost formula, not a second hand-rolled comparison) on a
                # singleton result so this check is always identical to the
                # end-of-run perf check, just evaluated per-app instead of
                # only after every app has finished.
                $singlePerfCheck = Test-PerfRegressions -Results @($result) -ModesRun $modes `
                    -BaselineFile $PerfBaselineFile -IgnoreApps $failFastPerfIgnoreApps
                if ($singlePerfCheck.Regressions.Count -gt 0) {
                    $failFastState.Triggered = $true
                    Write-Host ("  FAIL-FAST: performance regression in {0} - no new apps will be started" -f $result.App) -ForegroundColor Yellow
                    foreach ($r in $singlePerfCheck.Regressions) {
                        $pct = if ($r.Baseline -gt 0) { [math]::Round((($r.Actual - $r.Baseline) / $r.Baseline) * 100, 2) } else { 0 }
                        Write-Host ("        - ({0,-6}) {1:N0} -> {2:N0} {3} (+{4}%)" -f $r.Mode, $r.Baseline, $r.Actual, $r.Metric, $pct) -ForegroundColor Red
                    }
                }
            }
        }
    }
}
else {
    # Keep serial runs isolated too: CP/M programs observe their current
    # directory, so artifacts from earlier apps must not become test input.
    foreach ($item in $workItems) {
        $appBuildDir = Join-Path $BuildDir $item.App
        $result = Invoke-AppTest -AppName $item.App -Modes $modes -BuildDir $appBuildDir `
            -BaselineDir $BaselineDir -Emulator $Emulator -Placeholders $Placeholders `
            -RunArgs $item.RunArgs -RunStdin $item.RunStdin `
            -StackSize $item.StackSize -DccArgs $item.DccArgs `
            -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
            -UseEmulatedM80:$UseEmulatedM80 `
            -EmulatorRunArgs $emulatorRunArgs `
            -Fixtures $item.Fixtures -StageFixtures $true `
            -ExtraScenarios $item.ExtraScenarios -RunTimeout $RunTimeout
        Show-AppResult $result
        $results += $result
        if ($FailFast) {
            $stop = $false
            if (-not $result.Passed) {
                Write-Host ("  FAIL-FAST: correctness failure in {0} - stopping" -f $result.App) -ForegroundColor Yellow
                $stop = $true
            } elseif ($failFastPerfCheckActive) {
                $singlePerfCheck = Test-PerfRegressions -Results @($result) -ModesRun $modes `
                    -BaselineFile $PerfBaselineFile -IgnoreApps $failFastPerfIgnoreApps
                if ($singlePerfCheck.Regressions.Count -gt 0) {
                    Write-Host ("  FAIL-FAST: performance regression in {0} - stopping" -f $result.App) -ForegroundColor Yellow
                    foreach ($r in $singlePerfCheck.Regressions) {
                        $pct = if ($r.Baseline -gt 0) { [math]::Round((($r.Actual - $r.Baseline) / $r.Baseline) * 100, 2) } else { 0 }
                        Write-Host ("        - ({0,-6}) {1:N0} -> {2:N0} {3} (+{4}%)" -f $r.Mode, $r.Baseline, $r.Actual, $r.Metric, $pct) -ForegroundColor Red
                    }
                    $stop = $true
                }
            }
            if ($stop) { break }
        }
    }
}
$mainSuiteSw.Stop()
if ($FailFast) {
    $failFastSkippedApps = @($workItems.App | Where-Object { $_ -notin @($results.App) })
    if ($failFastSkippedApps.Count -gt 0) {
        Write-Host ("  ({0} app(s) not started due to -FailFast: {1})" -f $failFastSkippedApps.Count, ($failFastSkippedApps -join ', ')) -ForegroundColor DarkGray
        $skipped += $failFastSkippedApps.Count
    }
}

# Tally results.
foreach ($result in $results) {
    if ($result.Skipped) {
        $skipped++
        continue
    } elseif ($result.Passed) {
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
$perfCheckSw = [System.Diagnostics.Stopwatch]::StartNew()
if ((Test-IsNtvcmEmulator $Emulator) -and $StackCheck -and (-not $NoPerfCheck -or $UpdatePerfBaseline)) {
    $perfIgnoreApps = @($testFiles | Where-Object { Get-PerfIgnoreApp $_ })
    $perfCheck = Test-PerfRegressions -Results $results -ModesRun $modes `
        -BaselineFile $PerfBaselineFile -IgnoreApps $perfIgnoreApps -UpdateBaseline:$UpdatePerfBaseline

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "PERFORMANCE (CYCLE COUNT & .COM SIZE) CHECK" -ForegroundColor Cyan
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
                Write-Host ("    - {0,-12} ({1,-6}) {2:N0} -> {3:N0} {4} (+{5}%)" -f $r.App, $r.Mode, $r.Baseline, $r.Actual, $r.Metric, $pct) -ForegroundColor Red
            }
            $failed += $perfCheck.Regressions.Count
        }
        if ($perfCheck.Improvements.Count -gt 0) {
            Write-Host "  Improvements: $($perfCheck.Improvements.Count)" -ForegroundColor Green
            foreach ($i in $perfCheck.Improvements) {
                $pct = if ($i.Baseline -gt 0) { [math]::Round((($i.Baseline - $i.Actual) / $i.Baseline) * 100, 2) } else { 0 }
                Write-Host ("    - {0,-12} ({1,-6}) {2:N0} -> {3:N0} {4} (-{5}%)" -f $i.App, $i.Mode, $i.Baseline, $i.Actual, $i.Metric, $pct) -ForegroundColor Green
            }
            Write-Host "  (run -UpdatePerfBaseline to accept improved cycle counts/sizes)" -ForegroundColor DarkGray
        }
        if ($perfCheck.New.Count -gt 0) {
            Write-Host "  No baseline yet for $($perfCheck.New.Count) app/mode/metric combination(s); run -UpdatePerfBaseline to capture" -ForegroundColor DarkGray
        }
    }
}
$perfCheckSw.Stop()

$diagnosticsPassed = $null
$diagnosticsSw = [System.Diagnostics.Stopwatch]::StartNew()
if (-not $Apps) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "RUNNING DIAGNOSTICS SUITE" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    & pwsh (Join-Path $PSScriptRoot "run-diagnostics.ps1") -Dcc (Join-Path $script:RepoRoot "dcc")
    $diagnosticsExitCode = $LASTEXITCODE
    $diagnosticsPassed = ($diagnosticsExitCode -eq 0)
}
$diagnosticsSw.Stop()
if ($null -ne $diagnosticsPassed -and -not $diagnosticsPassed) {
    $failed++
}

$dccpeepTestsPassed = $null
$dccpeepFixturesSw = [System.Diagnostics.Stopwatch]::StartNew()
if (-not $Apps) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "RUNNING DCCPEEP FIXTURES" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    & pwsh (Join-Path $PSScriptRoot "run-dccpeep-tests.ps1") `
        -DccPeep (Join-Path $script:RepoRoot "dccpeep")
    $dccpeepTestsPassed = ($LASTEXITCODE -eq 0)
}
$dccpeepFixturesSw.Stop()
if ($null -ne $dccpeepTestsPassed -and -not $dccpeepTestsPassed) {
    $failed++
}

$narrowDiffPassed = $null
$narrowDiffSw = [System.Diagnostics.Stopwatch]::StartNew()
if ($NarrowDiff) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "NARROW-DIFF CHECK (narrowing on vs -fno-narrow)" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    $narrowDiffBuildRoot = Join-Path $BuildDir "narrow-diff"
    $narrowWorkItems = @($workItems | Where-Object { -not (Get-NarrowDiffIgnoreApp $_.App) })
    $narrowSkipped = $workItems.Count - $narrowWorkItems.Count
    if ($narrowSkipped -gt 0) {
        Write-Host "  (skipping $narrowSkipped app(s) marked narrow_diff_ignore)" -ForegroundColor DarkGray
    }
    $narrowResults = @()
    $narrowDone = 0
    $narrowTotal = $narrowWorkItems.Count

    if ($Parallel) {
        $ndRepoRoot = (Get-Location).Path
        $ndCfuDef   = ${function:Copy-FixtureUpper}.ToString()
        $ndGdmDef   = ${function:Get-DccMakeCommand}.ToString()
        $ndCtbsDef  = ${function:ConvertTo-BooleanSetting}.ToString()
        $ndSptDef   = ${function:Stop-ProcessTree}.ToString()
        $ndIpwtDef  = ${function:Invoke-ProcessWithTimeout}.ToString()
        $ndIdmbDef  = ${function:Invoke-DccMakeBuild}.ToString()
        $ndIndtDef  = ${function:Invoke-NarrowDiffTest}.ToString()
        $ndRunArgs  = @($emulatorRunArgs)

        $narrowWorkItems | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
            $item = $_
            Set-Location $using:ndRepoRoot
            if ($using:StackCheck) { $env:DCC_FORCE_STACK_CHECK = "1" }
            ${function:Copy-FixtureUpper}    = $using:ndCfuDef
            ${function:Get-DccMakeCommand}   = $using:ndGdmDef
            ${function:ConvertTo-BooleanSetting} = $using:ndCtbsDef
            ${function:Stop-ProcessTree}     = $using:ndSptDef
            ${function:Invoke-ProcessWithTimeout} = $using:ndIpwtDef
            ${function:Invoke-DccMakeBuild}  = $using:ndIdmbDef
            ${function:Invoke-NarrowDiffTest} = $using:ndIndtDef

            $appBuildDir = Join-Path $using:narrowDiffBuildRoot $item.App
            Invoke-NarrowDiffTest -AppName $item.App -BuildDir $appBuildDir -Emulator $using:Emulator `
                -RunArgs $item.RunArgs -RunStdin $item.RunStdin -StackSize $item.StackSize `
                -DccArgs $item.DccArgs -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
                -UseEmulatedM80:$using:UseEmulatedM80 `
                -EmulatorRunArgs $using:ndRunArgs -Fixtures $item.Fixtures -RunTimeout $using:RunTimeout
        } | ForEach-Object {
            $result = $_
            $narrowResults += $result
            $narrowDone++
            $status = if ($result.Passed) { "PASS" } else { "FAIL" }
            $counter = "[{0,3}/{1}]" -f $narrowDone, $narrowTotal
            $line = "$counter $status  $($result.App)"
            if ($result.Passed) {
                Write-Host $line -ForegroundColor Green
            } else {
                Write-Host $line -ForegroundColor Red
                foreach ($detail in $result.Lines) { Write-Host "        $($detail.Trim())" -ForegroundColor Red }
            }
        }
    }
    else {
        foreach ($item in $narrowWorkItems) {
            $appBuildDir = Join-Path $narrowDiffBuildRoot $item.App
            $result = Invoke-NarrowDiffTest -AppName $item.App -BuildDir $appBuildDir -Emulator $Emulator `
                -RunArgs $item.RunArgs -RunStdin $item.RunStdin -StackSize $item.StackSize `
                -DccArgs $item.DccArgs -DccFloatio $item.DccFloatio -DccLongio $item.DccLongio `
                -UseEmulatedM80:$UseEmulatedM80 `
                -EmulatorRunArgs $emulatorRunArgs -Fixtures $item.Fixtures -RunTimeout $RunTimeout
            $narrowResults += $result
            $narrowDone++
            $status = if ($result.Passed) { "PASS" } else { "FAIL" }
            $line = "[{0,3}/{1}] {2}  {3}" -f $narrowDone, $narrowTotal, $status, $result.App
            if ($result.Passed) {
                Write-Host $line -ForegroundColor Green
            } else {
                Write-Host $line -ForegroundColor Red
                foreach ($detail in $result.Lines) { Write-Host "        $($detail.Trim())" -ForegroundColor Red }
            }
        }
    }

    if (-not $KeepBuild -and (Test-Path $narrowDiffBuildRoot -PathType Container)) {
        Remove-Item -LiteralPath $narrowDiffBuildRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $narrowFailedApps = @($narrowResults | Where-Object { -not $_.Passed } | ForEach-Object { $_.App })
    $narrowDiffPassed = ($narrowFailedApps.Count -eq 0)
    Write-Host ""
    Write-Host "  Narrow-diff: $($narrowResults.Count - $narrowFailedApps.Count)/$($narrowResults.Count) matched" -ForegroundColor $(if ($narrowDiffPassed) { "Green" } else { "Red" })
    if (-not $narrowDiffPassed) {
        Write-Host "  Mismatched apps: $($narrowFailedApps -join ', ')" -ForegroundColor Red
        $failed += $narrowFailedApps.Count
    }
}
$narrowDiffSw.Stop()

$extendedPassed = $null
$extendedSw = [System.Diagnostics.Stopwatch]::StartNew()
if ($Extended) {
    Invoke-ExtendedSuite -Mode $Mode -Emulator $Emulator -RunTimeout $RunTimeout `
        -BuildDir $BuildDir -StackCheck $StackCheck -Serial (-not $Parallel) -ThrottleLimit $ThrottleLimit `
        -UseEmulatedM80 $UseEmulatedM80
    $extendedExitCode = $script:ExtendedSuiteExitCode
    $extendedPassed = ($extendedExitCode -eq 0)
    if (-not $extendedPassed) {
        $failed++
    }
}
$extendedSw.Stop()

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
Write-Host "  Diagnostics:  $(if ($null -eq $diagnosticsPassed) { 'skipped' } elseif ($diagnosticsPassed) { 'passed' } else { 'failed' })" -ForegroundColor $(if ($null -eq $diagnosticsPassed) { "DarkGray" } elseif ($diagnosticsPassed) { "Green" } else { "Red" })
Write-Host "  Dccpeep:      $(if ($null -eq $dccpeepTestsPassed) { 'skipped' } elseif ($dccpeepTestsPassed) { 'passed' } else { 'failed' })" -ForegroundColor $(if ($null -eq $dccpeepTestsPassed) { "DarkGray" } elseif ($dccpeepTestsPassed) { "Green" } else { "Red" })
if ($perfCheck -and -not $UpdatePerfBaseline) {
    $perfLabel = if ($perfCheck.Regressions.Count -eq 0) { "passed" } else { "$($perfCheck.Regressions.Count) regression(s)" }
    Write-Host "  Performance:  $perfLabel" -ForegroundColor $(if ($perfCheck.Regressions.Count -eq 0) { "Green" } else { "Red" })
}
if ($NarrowDiff) {
    Write-Host "  Narrow-diff:  $(if ($narrowDiffPassed) { 'passed' } else { 'failed' })" -ForegroundColor $(if ($narrowDiffPassed) { "Green" } else { "Red" })
}
Write-Host "  Total time:   $suiteElapsedStr"
Write-Host "  Optimisation: $optimisationSummary"

if ($TimingBreakdown) {
    function Write-TimingRow {
        param([string]$Label, [double]$Ms, [double]$DenominatorMs)
        $pct = if ($DenominatorMs -gt 0) { [math]::Round(($Ms / $DenominatorMs) * 100, 1) } else { 0 }
        $sStr = "{0:0.00}s" -f ($Ms / 1000.0)
        Write-Host ("    {0,-16} {1,9} ({2,5}%)" -f $Label, $sStr, $pct)
    }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "TIMING BREAKDOWN" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    $totalMs = $suiteElapsed.TotalMilliseconds
    Write-Host "  Top-level phases (% of total run time above):"
    Write-TimingRow "Main app suite" $mainSuiteSw.Elapsed.TotalMilliseconds $totalMs
    Write-TimingRow "Perf check" $perfCheckSw.Elapsed.TotalMilliseconds $totalMs
    Write-TimingRow "Diagnostics" $diagnosticsSw.Elapsed.TotalMilliseconds $totalMs
    Write-TimingRow "Dccpeep fixtures" $dccpeepFixturesSw.Elapsed.TotalMilliseconds $totalMs
    if ($NarrowDiff) { Write-TimingRow "Narrow-diff" $narrowDiffSw.Elapsed.TotalMilliseconds $totalMs }
    if ($Extended)   { Write-TimingRow "Extended suite" $extendedSw.Elapsed.TotalMilliseconds $totalMs }

    # Aggregate every app's build-pipeline timing (from dccmake's own
    # per-phase wall-clock report - see now_ms()/run_build in
    # src/dccmake/dccmake.c) and run timing (ntvcm's own reported elapsed
    # milliseconds, already captured for the cycle-count check) across both
    # modes. This is normalized to the SUM of per-app work, not wall time -
    # parallel execution makes wall time far smaller than that sum.
    $sumDcc = 0.0; $sumPeep = 0.0; $sumAsm = 0.0; $sumRtlstrip = 0.0; $sumLink = 0.0; $sumDccOther = 0.0
    $sumRunMs = 0.0; $sumAppElapsedMs = 0.0
    $sumDccmakeSelfTotal = 0.0; $sumDccmakePsInvoke = 0.0; $sumRunPs = 0.0
    $asmModes = @{}
    foreach ($r in $results) {
        $sumAppElapsedMs += $r.Elapsed.TotalMilliseconds
        foreach ($buildModeKey in @($r.Timing.Keys)) {
            $t = $r.Timing[$buildModeKey]
            if ($t) {
                $sumDcc += $t.dcc; $sumPeep += $t.peep; $sumAsm += $t.asm
                $sumRtlstrip += $t.rtlstrip; $sumLink += $t.link; $sumDccOther += $t.other
                if ($t.asmMode) { $asmModes[$t.asmMode] = $true }
                if ($t.total) { $sumDccmakeSelfTotal += $t.total }
                if ($t.psInvokeMs) { $sumDccmakePsInvoke += $t.psInvokeMs }
            }
        }
        foreach ($buildModeKey in @($r.Metrics.Keys)) {
            $m = $r.Metrics[$buildModeKey]
            if ($m.Ms) { $sumRunMs += [double]$m.Ms }
            if ($m.PsRunMs) { $sumRunPs += [double]$m.PsRunMs }
        }
    }
    $accountedMs = $sumDcc + $sumPeep + $sumAsm + $sumRtlstrip + $sumLink + $sumDccOther + $sumRunMs
    $scriptOverheadMs = [math]::Max($sumAppElapsedMs - $accountedMs, 0)
    $asmModeLabel = if ($asmModes.Count -eq 0) { "unknown" } elseif ($asmModes.Count -gt 1) { "MIXED: $($asmModes.Keys -join ', ')" } else { [string]$asmModes.Keys }
    # dccmake/ntvcm's own SELF-reported timing (dcc/peep/.../total, and
    # ntvcm's own "elapsed milliseconds") is measured from inside those
    # processes - it excludes the actual OS process-create/exec cost and
    # PowerShell's own overhead capturing/merging their output via `2>&1`.
    # Comparing that self-reported figure against a PowerShell-side
    # Stopwatch wrapping the SAME call splits the old undifferentiated
    # "script/spawn" bucket into what's actually process-invocation
    # overhead for dccmake/ntvcm specifically vs. everything else
    # (fixture staging, baseline comparison, parallel dispatch).
    $dccmakeInvokeOverheadMs = [math]::Max($sumDccmakePsInvoke - $sumDccmakeSelfTotal, 0)
    $ntvcmInvokeOverheadMs = [math]::Max($sumRunPs - $sumRunMs, 0)
    $otherScriptOverheadMs = [math]::Max($scriptOverheadMs - $dccmakeInvokeOverheadMs - $ntvcmInvokeOverheadMs, 0)

    Write-Host ""
    Write-Host "  Main suite build pipeline (% of aggregate per-app work across both" -ForegroundColor DarkGray
    Write-Host "  modes, NOT wall time - parallel execution overlaps these):"
    Write-TimingRow "dcc compile" $sumDcc $sumAppElapsedMs
    Write-TimingRow "dccpeep" $sumPeep $sumAppElapsedMs
    Write-TimingRow "m80 assemble" $sumAsm $sumAppElapsedMs
    Write-Host ("    {0,-16} {1}" -f "  m80 mode:", $asmModeLabel) -ForegroundColor $(if ($asmModeLabel -eq "native") { "DarkGray" } else { "Yellow" })
    Write-TimingRow "dccrtlstrip" $sumRtlstrip $sumAppElapsedMs
    Write-TimingRow "L80 link" $sumLink $sumAppElapsedMs
    Write-TimingRow "dccmake other" $sumDccOther $sumAppElapsedMs
    Write-TimingRow "ntvcm run" $sumRunMs $sumAppElapsedMs
    Write-TimingRow "dccmake spawn" $dccmakeInvokeOverheadMs $sumAppElapsedMs
    Write-TimingRow "ntvcm spawn" $ntvcmInvokeOverheadMs $sumAppElapsedMs
    Write-TimingRow "other script" $otherScriptOverheadMs $sumAppElapsedMs
}

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
if ($null -ne $diagnosticsPassed -and -not $diagnosticsPassed) {
    Write-Host ""
    Write-Host "Diagnostics suite failed" -ForegroundColor Red
}
if ($null -ne $dccpeepTestsPassed -and -not $dccpeepTestsPassed) {
    Write-Host ""
    Write-Host "Dccpeep fixture suite failed" -ForegroundColor Red
}
if ($NarrowDiff -and -not $narrowDiffPassed) {
    Write-Host ""
    Write-Host "Narrow-diff check failed: narrowing changed observable output for one or more apps (see detail above)" -ForegroundColor Red
}
if ($perfCheck -and -not $UpdatePerfBaseline -and $perfCheck.Regressions.Count -gt 0) {
    Write-Host ""
    Write-Host "Performance regressions (vs $PerfBaselineFile):" -ForegroundColor Red
    foreach ($r in $perfCheck.Regressions) {
        Write-Host "  - $($r.App) ($($r.Mode)): $($r.Baseline) -> $($r.Actual) $($r.Metric)" -ForegroundColor Red
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
