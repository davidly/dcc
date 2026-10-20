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
        $options = @()
        if ($stem.EndsWith(".os")) { $options += "-Os" }

        & $DccPeep @options $input.FullName $actual
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

        & $DccPeep @options $actual $again
        if ($LASTEXITCODE -ne 0 -or
            (Get-NormalizedText $again) -ne (Get-NormalizedText $actual)) {
            Write-Host "FAIL $stem (not idempotent)" -ForegroundColor Red
            $failed++
            continue
        }
        Write-Host "PASS $stem" -ForegroundColor Green
        $passed++
    }

    $swapFixture = Join-Path $FixtureDir "aggregate-swap-spilled-ldir.in.mac"
    if (Test-Path -LiteralPath $swapFixture) {
        $swapText = Get-NormalizedText $swapFixture
        $swapCases = @(
            @{ Name = "reload"; Old = "ld l,(ix-2)`nld h,(ix-1)"; New = "ld l,(ix-6)`nld h,(ix-5)" },
            @{
                Name = "spill"
                Old = "ld h,b`nld (ix-2),l`nld (ix-1),h`nld hl,_temp`nex de,hl`nld l,(ix-2)`nld h,(ix-1)"
                New = "ld h,b`nld (ix-6),l`nld (ix-5),h`nld hl,_temp`nex de,hl`nld l,(ix-6)`nld h,(ix-5)"
            },
            @{ Name = "temporary"; Old = "ex de,hl`nld hl,_temp"; New = "ex de,hl`nld hl,_other" },
            @{ Name = "size"; Old = "ld hl,_temp`nld bc,13"; New = "ld hl,_temp`nld bc,12" },
            @{ Name = "pointer"; Old = "ldir`nld hl,(_left)"; New = "ldir`nld hl,(_other)" }
        )
        foreach ($case in $swapCases) {
            $inputPath = Join-Path $tempRoot "swap-$($case.Name).mac"
            $outputPath = Join-Path $tempRoot "swap-$($case.Name).out.mac"
            $mutated = $swapText.Replace($case.Old, $case.New)
            if ($mutated -eq $swapText) {
                throw "Aggregate-swap negative case '$($case.Name)' did not modify its input"
            }
            [System.IO.File]::WriteAllText($inputPath, $mutated)
            & $DccPeep $inputPath $outputPath
            if ($LASTEXITCODE -ne 0 -or
                [System.IO.File]::ReadAllText($outputPath).Contains("peep: aggregate_swap_ldir")) {
                Write-Host "FAIL aggregate-swap-$($case.Name) (unsafe rewrite)" -ForegroundColor Red
                $failed++
            } else {
                Write-Host "PASS aggregate-swap-$($case.Name)" -ForegroundColor Green
                $passed++
            }
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
        Write-Host "PASS long-line" -ForegroundColor Green
        $passed++
    }
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "dccpeep fixtures: $passed passed, $failed failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })
