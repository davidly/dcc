param(
    [string]$CasesFile = "tests/mir_forced_correctness_cases.tsv",
    [string]$BuildRoot = "build/mir-forced-correctness",
    [int]$RunTimeout = 20,
    [switch]$NoStackCheck,
    [switch]$KeepBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Get-ForcedMirCases {
    param([string]$Path)

    $cases = @()
    foreach ($rawLine in Get-Content $Path) {
        $line = $rawLine.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith("#")) {
            continue
        }
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2) {
            throw "Invalid forced-MIR case '$rawLine' in $Path (expected: app<TAB>function)"
        }
        $cases += [pscustomobject]@{
            App      = $parts[0].Trim()
            Function = $parts[1].Trim()
        }
    }
    return $cases
}

Push-Location $repoRoot
try {
    $casesPath = Resolve-Path $CasesFile
    $cases = @(Get-ForcedMirCases -Path $casesPath)
    if ($cases.Count -eq 0) {
        throw "No forced-MIR correctness cases found in $casesPath"
    }

    $failures = @()
    foreach ($case in $cases) {
        $caseBuildRoot = Join-Path $BuildRoot ("{0}-{1}" -f $case.App, $case.Function)
        $savedForce = [Environment]::GetEnvironmentVariable("DCC_MIR_FORCE_ACCEPT_FUNCTION", "Process")
        [Environment]::SetEnvironmentVariable("DCC_MIR_FORCE_ACCEPT_FUNCTION", $case.Function, "Process")
        try {
            Write-Host ("=== Forced MIR correctness: {0}:{1} ===" -f $case.App, $case.Function) -ForegroundColor Cyan
            $runallArgs = @(
                "./scripts/runall.ps1",
                "-Apps", $case.App,
                "-Mode", "full",
                "-RunTimeout", $RunTimeout.ToString(),
                "-BuildDir", $caseBuildRoot,
                "-NoRamDisk",
                "-NoPerfCheck"
            )
            if ($NoStackCheck) { $runallArgs += "-NoStackCheck" }
            if ($KeepBuild) { $runallArgs += "-KeepBuild" }
            & pwsh @runallArgs
            if ($LASTEXITCODE -ne 0) {
                $failures += ("{0}:{1}" -f $case.App, $case.Function)
            }
        } finally {
            [Environment]::SetEnvironmentVariable("DCC_MIR_FORCE_ACCEPT_FUNCTION", $savedForce, "Process")
        }
    }

    if ($failures.Count -gt 0) {
        throw ("Forced MIR correctness failures: {0}" -f ($failures -join ", "))
    }
} finally {
    Pop-Location
}
