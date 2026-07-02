#Requires -Version 7
<#
.SYNOPSIS
Run compile-fail diagnostic tests for dcc.

.DESCRIPTION
Each tests/diagnostics/*.c file is expected to fail during dcc compilation.
The runner captures compiler output, normalizes source paths to <source>, and
compares it with tests/diagnostics/baselines/<name>.txt.

Use -Update to refresh or create baselines after intentionally changing
diagnostic wording.
#>

param(
    [string]$Dcc = "",
    [string]$TestDir = "tests/diagnostics",
    [string]$BaselineDir = "tests/diagnostics/baselines",
    [string]$BuildDir = "build/diagnostics",
    [switch]$Update,
    [switch]$Help
)

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    exit 0
}

function Resolve-DccCommand {
    param([string]$Explicit)

    if ($Explicit) { return $Explicit }
    if ($env:DCC) { return $env:DCC }
    if (Test-Path -LiteralPath "./dcc") {
        return (Resolve-Path -LiteralPath "./dcc").ProviderPath
    }
    if (Test-Path -LiteralPath "./dcc.exe") {
        return (Resolve-Path -LiteralPath "./dcc.exe").ProviderPath
    }
    return "dcc"
}

function Invoke-Capture {
    param(
        [string]$Command,
        [string[]]$Arguments
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Command
    foreach ($arg in $Arguments) {
        [void]$psi.ArgumentList.Add($arg)
    }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    try {
        $process = [System.Diagnostics.Process]::Start($psi)
    }
    catch {
        return [pscustomobject]@{
            ExitCode = 127
            Output = "ERROR starting $Command : $_`n"
        }
    }

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $stdout + $stderr
    }
}

function Normalize-Output {
    param(
        [string]$Text,
        [string]$SourcePath
    )

    $normalized = (($Text -replace "`r`n", "`n") -replace "`r", "`n")
    $resolvedSource = (Resolve-Path -LiteralPath $SourcePath).ProviderPath
    $normalized = $normalized -replace [regex]::Escape($resolvedSource), "<source>"
    $normalized = $normalized -replace [regex]::Escape($SourcePath), "<source>"
    if ($normalized.Length -gt 0 -and -not $normalized.EndsWith("`n")) {
        $normalized += "`n"
    }
    return $normalized
}

$dccCommand = Resolve-DccCommand $Dcc

if (-not (Test-Path -LiteralPath $TestDir -PathType Container)) {
    Write-Error "Diagnostic test directory not found: $TestDir"
    exit 1
}

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
if ($Update) {
    New-Item -ItemType Directory -Path $BaselineDir -Force | Out-Null
}

$tests = Get-ChildItem -LiteralPath $TestDir -Filter "*.c" | Sort-Object Name
if ($tests.Count -eq 0) {
    Write-Error "No diagnostic tests found in $TestDir"
    exit 1
}

$failures = 0
$index = 0
foreach ($test in $tests) {
    $index++
    $name = [System.IO.Path]::GetFileNameWithoutExtension($test.Name)
    $outPath = Join-Path $BuildDir "$name.MAC"
    $baselinePath = Join-Path $BaselineDir "$name.txt"

    Remove-Item -LiteralPath $outPath -Force -ErrorAction SilentlyContinue
    $result = Invoke-Capture $dccCommand @($test.FullName, "-o", $outPath)
    $actual = Normalize-Output $result.Output $test.FullName

    if ($result.ExitCode -eq 0) {
        Write-Host "[$index/$($tests.Count)] FAIL $($test.Name): expected compile failure, got success" -ForegroundColor Red
        $failures++
        continue
    }

    if ($Update) {
        [System.IO.File]::WriteAllText($baselinePath, $actual, [System.Text.UTF8Encoding]::new($false))
        Write-Host "[$index/$($tests.Count)] UPDATE $($test.Name)" -ForegroundColor Yellow
        continue
    }

    if (-not (Test-Path -LiteralPath $baselinePath)) {
        Write-Host "[$index/$($tests.Count)] FAIL $($test.Name): missing baseline $baselinePath" -ForegroundColor Red
        $failures++
        continue
    }

    $expected = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $baselinePath).ProviderPath)
    $expected = (($expected -replace "`r`n", "`n") -replace "`r", "`n")
    if ($expected.Length -gt 0 -and -not $expected.EndsWith("`n")) {
        $expected += "`n"
    }

    if ($actual -ne $expected) {
        Write-Host "[$index/$($tests.Count)] FAIL $($test.Name): diagnostic mismatch" -ForegroundColor Red
        Write-Host "--- expected"
        Write-Host $expected
        Write-Host "--- actual"
        Write-Host $actual
        $failures++
        continue
    }

    Write-Host "[$index/$($tests.Count)] PASS $($test.Name)" -ForegroundColor Green
}

if ($failures -ne 0) {
    Write-Host "diagnostics: $failures failure(s)" -ForegroundColor Red
    exit 1
}

if ($Update) {
    Write-Host "diagnostics: baselines updated" -ForegroundColor Yellow
} else {
    Write-Host "diagnostics: all $($tests.Count) passed" -ForegroundColor Green
}