#Requires -Version 7
<#
.SYNOPSIS
Stress-test the test suite by running runall.py repeatedly, stopping on the
first failure.

.DESCRIPTION
Runs scripts/runall.py up to -Iterations times. Each iteration cleans the
per-app build directories first, then runs the suite (in parallel by default).
The loop stops immediately when an iteration fails (runall.py exits non-zero),
printing which iteration broke and the failing detail lines from its log.

Useful for shaking out intermittent/flaky failures (e.g. CPU-bound tests under
heavy parallel contention) that a single run might not reveal.

.PARAMETER Iterations
  Maximum number of suite runs (default: 50).

.PARAMETER Serial
  Run the suite serially instead of in parallel.

.PARAMETER ThrottleLimit
  Max concurrent apps when running in parallel (default: CPU core count).

.PARAMETER LogDir
  Directory for per-iteration logs (default: a temp folder). Each run writes
  stress_<n>.log so a failure can be inspected in full.

.PARAMETER KeepLogs
  Keep all iteration logs, not just the failing one (which is always kept).

.EXAMPLE
  pwsh ./scripts/stress-runall.ps1
  pwsh ./scripts/stress-runall.ps1 -Iterations 100
  pwsh ./scripts/stress-runall.ps1 -Serial -Iterations 10
  pwsh ./scripts/stress-runall.ps1 -ThrottleLimit 8 -KeepLogs

.NOTES
  Exit codes:
    0 = all iterations passed
    1 = an iteration failed (the loop stopped early)
#>

param(
    [int]$Iterations = 10,
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [string]$LogDir,
    [switch]$KeepLogs
)

$ErrorActionPreference = "Stop"

# Resolve repo root (parent of the scripts folder) and run from there so
# The runner sees tests/, build/, DCCRTL.MAC, etc. at the expected paths.
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$runall = Join-Path $PSScriptRoot "runall.py"
if (-not (Test-Path $runall)) {
    Write-Error "runall.py not found at $runall"
    exit 1
}

if (-not $LogDir) {
    $LogDir = Join-Path ([System.IO.Path]::GetTempPath()) "dcc-stress-$PID"
}
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

$mode = if ($Serial) { "serial" } else { "parallel (throttle = $ThrottleLimit)" }
Write-Host "Stress-testing runall.py: up to $Iterations iterations, $mode" -ForegroundColor Cyan
Write-Host "Logs: $LogDir" -ForegroundColor DarkGray
Write-Host ""

$suiteSw = [System.Diagnostics.Stopwatch]::StartNew()
$failedIteration = 0

for ($i = 1; $i -le $Iterations; $i++) {
    # Clean per-app build subdirectories between runs so each iteration starts
    # from a known state (parallel mode creates build/<app>/ dirs).
    Get-ChildItem -Path "build" -Directory -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

    $log = Join-Path $LogDir "stress_$i.log"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $runArgs = @($runall, "--verbose")
    if ($Serial) {
        $runArgs += "--serial"
    } else {
        $runArgs += @("--throttle-limit", "$ThrottleLimit")
    }

    # Run the suite, capturing all output to the per-iteration log.
    & python @runArgs *> $log
    $rc = $LASTEXITCODE
    $sw.Stop()
    $elapsedStr = "{0:0.0}s" -f $sw.Elapsed.TotalSeconds

    if ($rc -ne 0) {
        Write-Host ("[{0,3}/{1}] FAIL ({2})" -f $i, $Iterations, $elapsedStr) -ForegroundColor Red
        $failedIteration = $i
        Write-Host ""
        Write-Host "Failure detail (from $log):" -ForegroundColor Red
        Select-String -Path $log -Pattern 'FAIL|MISMATCH|WARNING|ERROR' |
            Select-Object -First 20 |
            ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
        break
    }
    else {
        # Pull the suite's own reported time if present.
        $totalLine = (Select-String -Path $log -Pattern 'Total time:' | Select-Object -First 1).Line
        $suffix = if ($totalLine) { "($($totalLine.Trim()))" } else { "($elapsedStr)" }
        Write-Host ("[{0,3}/{1}] PASS {2}" -f $i, $Iterations, $suffix) -ForegroundColor Green
        if (-not $KeepLogs) {
            Remove-Item -Path $log -Force -ErrorAction SilentlyContinue
        }
    }
}

$suiteSw.Stop()
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
if ($failedIteration -gt 0) {
    Write-Host "STRESS FAILED on iteration $failedIteration of $Iterations" -ForegroundColor Red
    Write-Host "Full log: $(Join-Path $LogDir "stress_$failedIteration.log")" -ForegroundColor Red
    Write-Host "Elapsed: $("{0:0.0}s" -f $suiteSw.Elapsed.TotalSeconds)"
    exit 1
}
else {
    Write-Host "STRESS PASSED: $Iterations/$Iterations iterations succeeded" -ForegroundColor Green
    Write-Host "Elapsed: $("{0:0.0}s" -f $suiteSw.Elapsed.TotalSeconds)"
    exit 0
}
