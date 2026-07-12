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

Runs in parallel by default (each test is a single, independent dcc
invocation with its own build-dir output file, so there's no shared-file
clobbering to guard against). Pass -Serial to force one-at-a-time
execution, e.g. for easier debugging of a failure.
#>

param(
    [string]$Dcc = "",
    [string]$TestDir = "tests/diagnostics",
    [string]$BaselineDir = "tests/diagnostics/baselines",
    [string]$BuildDir = "build/diagnostics",
    [switch]$Update,
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
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

# Runs one diagnostic test end to end and returns a result object; never
# writes to the console itself so the parallel path can print results in a
# stable, readable order as they stream back in.
function Invoke-DiagnosticTest {
    param(
        [string]$TestPath,
        [string]$TestName,
        [string]$DccCommand,
        [string]$BuildDir,
        [string]$BaselineDir,
        [bool]$Update
    )

    $outPath = Join-Path $BuildDir "$TestName.MAC"
    $baselinePath = Join-Path $BaselineDir "$TestName.txt"

    Remove-Item -LiteralPath $outPath -Force -ErrorAction SilentlyContinue
    $result = Invoke-Capture $DccCommand @($TestPath, "-o", $outPath)
    $actual = Normalize-Output $result.Output $TestPath

    if ($result.ExitCode -eq 0) {
        return [pscustomobject]@{
            Name = $TestName
            Passed = $false
            Updated = $false
            Detail = "expected compile failure, got success"
        }
    }

    if ($Update) {
        [System.IO.File]::WriteAllText($baselinePath, $actual, [System.Text.UTF8Encoding]::new($false))
        return [pscustomobject]@{
            Name = $TestName
            Passed = $true
            Updated = $true
            Detail = $null
        }
    }

    if (-not (Test-Path -LiteralPath $baselinePath)) {
        return [pscustomobject]@{
            Name = $TestName
            Passed = $false
            Updated = $false
            Detail = "missing baseline $baselinePath"
        }
    }

    $expected = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $baselinePath).ProviderPath)
    $expected = (($expected -replace "`r`n", "`n") -replace "`r", "`n")
    if ($expected.Length -gt 0 -and -not $expected.EndsWith("`n")) {
        $expected += "`n"
    }

    if ($actual -ne $expected) {
        return [pscustomobject]@{
            Name = $TestName
            Passed = $false
            Updated = $false
            Detail = "diagnostic mismatch"
            Expected = $expected
            Actual = $actual
        }
    }

    return [pscustomobject]@{
        Name = $TestName
        Passed = $true
        Updated = $false
        Detail = $null
    }
}

function Show-DiagnosticResult {
    param(
        [pscustomobject]$Result,
        [int]$Index,
        [int]$Total
    )

    $counter = "[$Index/$Total]"
    if ($Result.Updated) {
        Write-Host "$counter UPDATE $($Result.Name).c" -ForegroundColor Yellow
        return
    }
    if ($Result.Passed) {
        Write-Host "$counter PASS $($Result.Name).c" -ForegroundColor Green
        return
    }
    Write-Host "$counter FAIL $($Result.Name).c: $($Result.Detail)" -ForegroundColor Red
    if ($Result.Detail -eq "diagnostic mismatch") {
        Write-Host "--- expected"
        Write-Host $Result.Expected
        Write-Host "--- actual"
        Write-Host $Result.Actual
    }
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

if ($Serial) {
    $index = 0
    foreach ($test in $tests) {
        $index++
        $name = [System.IO.Path]::GetFileNameWithoutExtension($test.Name)
        $result = Invoke-DiagnosticTest -TestPath $test.FullName -TestName $name `
            -DccCommand $dccCommand -BuildDir $BuildDir -BaselineDir $BaselineDir -Update:$Update
        Show-DiagnosticResult -Result $result -Index $index -Total $tests.Count
        if (-not $result.Passed) { $failures++ }
    }
}
else {
    # Each worker runs in its own runspace, so bring the needed functions
    # and values in explicitly (module-scope functions/variables aren't
    # visible inside -Parallel script blocks otherwise).
    $icDef = ${function:Invoke-Capture}.ToString()
    $noDef = ${function:Normalize-Output}.ToString()
    $idtDef = ${function:Invoke-DiagnosticTest}.ToString()

    $done = 0
    $tests | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        $test = $_
        ${function:Invoke-Capture} = $using:icDef
        ${function:Normalize-Output} = $using:noDef
        ${function:Invoke-DiagnosticTest} = $using:idtDef

        $name = [System.IO.Path]::GetFileNameWithoutExtension($test.Name)
        Invoke-DiagnosticTest -TestPath $test.FullName -TestName $name `
            -DccCommand $using:dccCommand -BuildDir $using:BuildDir -BaselineDir $using:BaselineDir -Update:$using:Update
    } | ForEach-Object {
        $done++
        Show-DiagnosticResult -Result $_ -Index $done -Total $tests.Count
        if (-not $_.Passed) { $failures++ }
    }
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
