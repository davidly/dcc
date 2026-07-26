#Requires -Version 7
<#
.SYNOPSIS
Isolates PowerShell/.NET scripting and process-spawn overhead from dcc's
own build pipeline, for a cross-platform/cross-machine comparison that
doesn't need dcc built at all.

.DESCRIPTION
runall.ps1 -TimingBreakdown's "other script" bucket - PowerShell-side
per-app work not accounted for by dcc/dccpeep/dccrtlstrip/m80/L80/ntvcm's
own self-reported timing - showed very different percentages across
machines in one comparison (10.4% Windows, 17.6% macOS, 34.4% Linux).
Percentages alone can't tell "PowerShell is slower on this machine" apart
from "the C tools are faster on this machine, diluting the same absolute
overhead into a bigger share." This script isolates the PowerShell-only
side with no dcc/dccpeep/ntvcm involved at all, so the absolute ms numbers
it prints are directly comparable across machines/platforms.

Phases, each run for -Iterations (default matches the real suite's scale:
307 apps x 2 modes = 614):
  ProcessSpawn      - spawn one trivial external process, capture output
                       via 2>&1, per iteration (isolates OS process-create/
                       exec + pwsh's stream-capture cost).
  ProcessSpawnX2    - same, twice per iteration (a build touches roughly
                       two major external processes: dccmake and ntvcm).
  ScriptingOnly     - no process spawn at all: hashtable lookup, a regex
                       -match, Join-Path, Test-Path, Get-Content -Raw on a
                       small scratch file, and a -ceq string comparison -
                       the same shapes of work Invoke-AppTest/
                       Test-MatchesBaseline do per app in runall.ps1.
  Combined          - ProcessSpawnX2 + ScriptingOnly together, to check
                       for any interaction effect beyond their simple sum.
  ParallelDispatch  - Combined's work run through ForEach-Object -Parallel
                       with the same per-iteration function-redefinition
                       pattern (${function:X} = $using:...) runall.ps1
                       itself uses, compared against CombinedSerial2 (the
                       same work run serially again, for a fair baseline
                       under identical cache/JIT-warmup state) - isolates
                       parallel-dispatch overhead specifically.

.PARAMETER Iterations
  Iteration count per phase (default 614, matching runall.ps1 -Mode full's
  real scale: 307 apps x 2 modes).

.PARAMETER ThrottleLimit
  Max concurrent workers for the ParallelDispatch phase (default: CPU
  core count, matching runall.ps1's own default).

.PARAMETER SkipParallel
  Skip the ParallelDispatch/CombinedSerial2 phases.

.EXAMPLE
  pwsh ./scripts/bench-pwsh-overhead.ps1
  pwsh ./scripts/bench-pwsh-overhead.ps1 -Iterations 1000
#>

param(
    [int]$Iterations = 614,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [switch]$SkipParallel
)

$ErrorActionPreference = "Stop"

# Platform-appropriate trivial no-op external process - the fastest
# possible thing that's still a REAL child process (not a PowerShell
# cmdlet), matching the shape of runall.ps1's own
# `& $cmd @args 2>&1` invocations of dccmake/ntvcm. Resolved once, up
# front, the same way Get-DccMakeCommand resolves dccmake's path once
# rather than per call.
if ($IsWindows) {
    $noopCmd = "cmd.exe"
    $noopArgs = @("/c", "exit", "0")
} else {
    $candidate = Get-Command "true" -ErrorAction SilentlyContinue
    $noopCmd = if ($candidate) { $candidate.Source } else { "/usr/bin/true" }
    $noopArgs = @()
}

# Scratch file for the ScriptingOnly phase's Get-Content/Test-Path calls -
# small, fixed content, matching a typical baseline file's shape.
$scratchDir = Join-Path ([System.IO.Path]::GetTempPath()) "pwsh-overhead-bench-$PID"
New-Item -ItemType Directory -Path $scratchDir -Force | Out-Null
$scratchFile = Join-Path $scratchDir "scratch.txt"
Set-Content -Path $scratchFile -NoNewline -Value "line one`nline two`nline three`n"

# A small in-memory hashtable shaped like runall.ps1's $appOverrides -
# 200 apps' worth of per-app settings, so Get-AppArgs/Get-StackSize-style
# lookups below have a realistic table size to search.
$appOverrides = @{}
for ($i = 0; $i -lt 200; $i++) {
    $appOverrides["app$i"] = @{ args = "-x"; stack_size = 512 }
}

function Measure-Phase {
    param([string]$Name, [scriptblock]$Body)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $Body
    $sw.Stop()
    [pscustomobject]@{
        Phase     = $Name
        TotalMs   = [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
        MsPerIter = [math]::Round($sw.Elapsed.TotalMilliseconds / $Iterations, 4)
    }
}

$results = @()

$results += Measure-Phase "ProcessSpawn" {
    for ($i = 0; $i -lt $Iterations; $i++) {
        & $noopCmd @noopArgs 2>&1 | Out-Null
    }
}

$results += Measure-Phase "ProcessSpawnX2" {
    for ($i = 0; $i -lt $Iterations; $i++) {
        & $noopCmd @noopArgs 2>&1 | Out-Null
        & $noopCmd @noopArgs 2>&1 | Out-Null
    }
}

$results += Measure-Phase "ScriptingOnly" {
    for ($i = 0; $i -lt $Iterations; $i++) {
        $app = "app$($i % 200)"
        $found = $appOverrides.ContainsKey($app)
        if ($found -and $appOverrides[$app]['args']) { $a = $appOverrides[$app]['args'] }
        $joined = Join-Path $scratchDir "sub$i.txt"
        $exists = Test-Path -LiteralPath $scratchFile -PathType Leaf
        $content = Get-Content -Path $scratchFile -Raw
        $matched = $content -match 'line (\w+)'
        $eq = ($content -ceq $content)
    }
}

$results += Measure-Phase "Combined" {
    for ($i = 0; $i -lt $Iterations; $i++) {
        & $noopCmd @noopArgs 2>&1 | Out-Null
        & $noopCmd @noopArgs 2>&1 | Out-Null
        $app = "app$($i % 200)"
        $found = $appOverrides.ContainsKey($app)
        if ($found -and $appOverrides[$app]['args']) { $a = $appOverrides[$app]['args'] }
        $joined = Join-Path $scratchDir "sub$i.txt"
        $exists = Test-Path -LiteralPath $scratchFile -PathType Leaf
        $content = Get-Content -Path $scratchFile -Raw
        $matched = $content -match 'line (\w+)'
        $eq = ($content -ceq $content)
    }
}

if (-not $SkipParallel) {
    # Serial baseline for the SAME combined work, run again right before
    # the parallel version below, so both halves of that specific
    # comparison run under identical warm-cache/JIT-warmup conditions
    # rather than comparing against "Combined" above (already run once).
    $results += Measure-Phase "CombinedSerial2" {
        for ($i = 0; $i -lt $Iterations; $i++) {
            & $noopCmd @noopArgs 2>&1 | Out-Null
            & $noopCmd @noopArgs 2>&1 | Out-Null
            $content = Get-Content -Path $scratchFile -Raw
            $matched = $content -match 'line (\w+)'
        }
    }

    function Invoke-BenchWork {
        param($NoopCmd, $NoopArgs, $ScratchFile)
        & $NoopCmd @NoopArgs 2>&1 | Out-Null
        & $NoopCmd @NoopArgs 2>&1 | Out-Null
        $content = Get-Content -Path $ScratchFile -Raw
        $matched = $content -match 'line (\w+)'
    }
    $ibwDef = ${function:Invoke-BenchWork}.ToString()

    $results += Measure-Phase "ParallelDispatch" {
        1..$Iterations | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
            ${function:Invoke-BenchWork} = $using:ibwDef
            Invoke-BenchWork -NoopCmd $using:noopCmd -NoopArgs $using:noopArgs -ScratchFile $using:scratchFile
        } | Out-Null
    }
}

Remove-Item -LiteralPath $scratchDir -Recurse -Force -ErrorAction SilentlyContinue

$results | Format-Table -AutoSize
Write-Host ""
Write-Host "Platform:  $($PSVersionTable.OS)" -ForegroundColor Cyan
Write-Host "PSVersion: $($PSVersionTable.PSVersion)" -ForegroundColor Cyan
Write-Host "Iterations per phase: $Iterations" -ForegroundColor Cyan
Write-Host "noop command: $noopCmd $noopArgs" -ForegroundColor Cyan
