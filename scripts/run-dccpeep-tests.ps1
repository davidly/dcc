#Requires -Version 7
param(
    [string]$DccPeep,
    [string]$FixtureDir
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $DccPeep) { $DccPeep = Join-Path $repoRoot "dccpeep" }
if (-not $FixtureDir) { $FixtureDir = Join-Path $repoRoot "tests/dccpeep" }

function Get-NormalizedText([string]$Path) {
    return ([System.IO.File]::ReadAllText($Path) -replace "`r`n", "`n")
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("dccpeep-tests-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$failed = 0
$passed = 0

try {
    foreach ($input in Get-ChildItem -Path $FixtureDir -Filter "*.in.mac" | Sort-Object Name) {
        $stem = $input.Name.Substring(0, $input.Name.Length - ".in.mac".Length)
        $expected = Join-Path $FixtureDir "$stem.expected.mac"
        $actual = Join-Path $tempRoot "$stem.actual.mac"
        $again = Join-Path $tempRoot "$stem.again.mac"

        & $DccPeep $input.FullName $actual
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $actual)) {
            Write-Host "FAIL $stem (optimizer exit)" -ForegroundColor Red
            $failed++
            continue
        }
        if ((Get-NormalizedText $actual) -ne (Get-NormalizedText $expected)) {
            Write-Host "FAIL $stem (output mismatch)" -ForegroundColor Red
            $failed++
            continue
        }

        & $DccPeep $actual $again
        if ($LASTEXITCODE -ne 0 -or
            (Get-NormalizedText $again) -ne (Get-NormalizedText $actual)) {
            Write-Host "FAIL $stem (not idempotent)" -ForegroundColor Red
            $failed++
            continue
        }
        Write-Host "PASS $stem" -ForegroundColor Green
        $passed++
    }

    $longInput = Join-Path $tempRoot "long.in.mac"
    $longOutput = Join-Path $tempRoot "long.out.mac"
    [System.IO.File]::WriteAllText($longInput, "; " + ("x" * 700) + "`nend`n")
    & $DccPeep $longInput $longOutput
    $longLines = @(Get-Content -LiteralPath $longOutput)
    if ($LASTEXITCODE -ne 0 -or $longLines.Count -ne 2 -or $longLines[0].Length -ne 702) {
        Write-Host "FAIL long-line" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "PASS long-line" -ForegroundColor Green
        $passed++
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "dccpeep fixtures: $passed passed, $failed failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })
