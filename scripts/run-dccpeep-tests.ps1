#Requires -Version 7
param(
    [string]$DccPeep,
    [string]$FixtureDir,
    [switch]$FailuresOnly,
    [ValidateRange(1, 1024)]
    [int]$ThrottleLimit = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $DccPeep) { $DccPeep = Join-Path $repoRoot "dccpeep" }
if (-not $FixtureDir) { $FixtureDir = Join-Path $repoRoot "tests/dccpeep" }

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("dccpeep-tests-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tempRoot | Out-Null
$failed = 0
$passed = 0

try {
    $fixtureResults = @(Get-ChildItem -Path $FixtureDir -Filter "*.in.mac" -File |
        ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
            $fixture = $_
            $stem = $fixture.Name.Substring(0, $fixture.Name.Length - ".in.mac".Length)
            $expected = Join-Path $using:FixtureDir "$stem.expected.mac"
            $actual = Join-Path $using:tempRoot "$stem.actual.mac"
            $again = Join-Path $using:tempRoot "$stem.again.mac"
            $options = @()
            if ($stem.EndsWith(".os")) { $options += "-Os" }
            $detail = $null
            try {
                if (-not [System.IO.File]::Exists($expected)) {
                    $detail = "expected output missing"
                }
                else {
                    $null = & $using:DccPeep @options $fixture.FullName $actual 2>&1
                    if ($LASTEXITCODE -ne 0 -or -not [System.IO.File]::Exists($actual)) {
                        $detail = "optimizer exit"
                    }
                    else {
                        $actualText = [System.IO.File]::ReadAllText($actual) -replace "`r`n", "`n"
                        $expectedText = [System.IO.File]::ReadAllText($expected) -replace "`r`n", "`n"
                        if ($actualText -ne $expectedText) {
                            $detail = "output mismatch"
                        }
                        else {
                            $null = & $using:DccPeep @options $actual $again 2>&1
                            if ($LASTEXITCODE -ne 0 -or -not [System.IO.File]::Exists($again)) {
                                $detail = "optimizer exit on second pass"
                            }
                            elseif (([System.IO.File]::ReadAllText($again) -replace "`r`n", "`n") -ne $actualText) {
                                $detail = "not idempotent"
                            }
                        }
                    }
                }
            }
            catch { $detail = $_.ToString() }
            [pscustomobject]@{ Name = $stem; Passed = ($null -eq $detail); Detail = $detail }
        })
    foreach ($result in ($fixtureResults | Sort-Object Name)) {
        if ($result.Passed) {
            if (-not $FailuresOnly) { Write-Host "PASS $($result.Name)" -ForegroundColor Green }
            $passed++
        }
        else {
            Write-Host "FAIL $($result.Name) ($($result.Detail))" -ForegroundColor Red
            $failed++
        }
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
        if (-not $FailuresOnly) { Write-Host "PASS long-line" -ForegroundColor Green }
        $passed++
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "dccpeep fixtures: $passed passed, $failed failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })
