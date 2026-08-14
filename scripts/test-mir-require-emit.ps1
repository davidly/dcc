#Requires -Version 7

param(
    [string]$Dcc = "",
    [string]$BuildDir = "build/mir-require-emit-test"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$dccCommand = if ($Dcc) {
    $Dcc
}
elseif (Test-Path (Join-Path $repoRoot "dcc")) {
    Join-Path $repoRoot "dcc"
}
else {
    "dcc"
}
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
}
else {
    Join-Path $repoRoot $BuildDir
}

function Invoke-Dcc {
    param(
        [string[]]$Arguments,
        [hashtable]$Environment = @{}
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $dccCommand
    $psi.WorkingDirectory = $repoRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        [void]$psi.ArgumentList.Add($argument)
    }
    foreach ($name in $Environment.Keys) {
        $psi.Environment[$name] = $Environment[$name]
    }

    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $stdout + $stderr
    }
}

function Assert-Success {
    param([string]$Name, [pscustomobject]$Result)
    if ($Result.ExitCode -ne 0) {
        throw "$Name failed unexpectedly (exit $($Result.ExitCode)):`n$($Result.Output)"
    }
    Write-Host "PASS $Name"
}

function Assert-Failure {
    param(
        [string]$Name,
        [pscustomobject]$Result,
        [string]$Expected
    )
    if ($Result.ExitCode -eq 0) {
        throw "$Name succeeded unexpectedly"
    }
    if (-not $Result.Output.Contains($Expected)) {
        throw "$Name did not report '$Expected':`n$($Result.Output)"
    }
    Write-Host "PASS $Name"
}

function Assert-DiagnosticBaseline {
    param(
        [string]$Name,
        [pscustomobject]$Result,
        [string]$Source,
        [string]$Baseline
    )

    $actual = (($Result.Output -replace "`r`n", "`n") -replace "`r", "`n")
    $resolvedSource = (Resolve-Path -LiteralPath $Source).ProviderPath
    $actual = $actual -replace [regex]::Escape($resolvedSource), "<source>"
    $actual = $actual -replace [regex]::Escape($Source), "<source>"
    if ($actual.Length -gt 0 -and -not $actual.EndsWith("`n")) {
        $actual += "`n"
    }
    $expected = [System.IO.File]::ReadAllText(
        (Resolve-Path -LiteralPath $Baseline).ProviderPath)
    $expected = (($expected -replace "`r`n", "`n") -replace "`r", "`n")
    if ($expected.Length -gt 0 -and -not $expected.EndsWith("`n")) {
        $expected += "`n"
    }
    if ($actual -cne $expected) {
        throw "$Name diagnostic changed:`nEXPECTED:`n$expected`nACTUAL:`n$actual"
    }
    Write-Host "PASS $Name"
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

$normal = Invoke-Dcc @(
    "-stack", "512", "-I", ".", "tests/tmircfg.c",
    "-o", (Join-Path $buildRoot "NORMAL.MAC")
) @{
    DCC_MIR_REQUIRE_EMIT = "1"
}
Assert-Success "all-MIR compile with DCC_MIR_REQUIRE_EMIT" $normal

$stack = Invoke-Dcc @(
    "-fstack-check", "-stack", "512", "-I", ".", "tests/tmircfg.c",
    "-o", (Join-Path $buildRoot "STACK.MAC")
) @{
    DCC_MIR_REQUIRE_EMIT = "1"
}
Assert-Success "all-MIR stack-check compile with DCC_MIR_REQUIRE_EMIT" $stack

$invalidSource = "tests/diagnostics/ast-local-init-unsupported-member.c"
$invalid = Invoke-Dcc @(
    $invalidSource,
    "-o", (Join-Path $buildRoot "INVALID.MAC")
) @{
    DCC_MIR_REQUIRE_COMPLETE = "1"
    DCC_MIR_REQUIRE_EMIT = "1"
}
if ($invalid.ExitCode -eq 0) {
    throw "invalid initializer strict diagnostic succeeded unexpectedly"
}
Assert-DiagnosticBaseline "invalid initializer preserves DCC-E1002" `
    $invalid $invalidSource `
    "tests/diagnostics/baselines/ast-local-init-unsupported-member.txt"

$fallthroughSource = "tests/diagnostics/warn-nonmain-fallthrough.c"
$fallthroughPath = Join-Path $buildRoot "FALLTHROUGH.MAC"
$fallthrough = Invoke-Dcc @(
    $fallthroughSource,
    "-o", $fallthroughPath
) @{
    DCC_MIR_REQUIRE_EMIT = "1"
}
Assert-Success "non-main fallthrough emits MIR" $fallthrough
Assert-DiagnosticBaseline "non-main fallthrough warning preserved" `
    $fallthrough $fallthroughSource `
    "tests/diagnostics/baselines/warn-nonmain-fallthrough.txt"
$fallthroughAssembly = [System.IO.File]::ReadAllText($fallthroughPath)
$helperStart = $fallthroughAssembly.IndexOf("_helper:")
$mainStart = $fallthroughAssembly.IndexOf("_main:", $helperStart + 1)
if ($helperStart -lt 0 -or $mainStart -lt 0) {
    throw "non-main fallthrough assembly is missing helper/main labels"
}
$helperAssembly = $fallthroughAssembly.Substring(
    $helperStart, $mainStart - $helperStart)
if ($helperAssembly.Contains("`tld hl,0")) {
    throw "non-main fallthrough incorrectly forces HL=0"
}
Write-Host "PASS non-main fallthrough leaves return value undefined"

$oversizedSource = Join-Path $buildRoot "oversized.c"
$writer = [System.IO.StreamWriter]::new($oversizedSource, $false,
    [System.Text.Encoding]::ASCII)
try {
    $writer.WriteLine("int oversized(int x) {")
    for ($i = 0; $i -lt 3000; $i++) {
        $writer.WriteLine("    x = x + 1;")
    }
    $writer.WriteLine("    return x;")
    $writer.WriteLine("}")
    $writer.WriteLine("int main(void) { return oversized(0) != 3000; }")
}
finally {
    $writer.Dispose()
}

$oversizedRequired = Invoke-Dcc @(
    "-g", "-stack", "512", "-I", ".", $oversizedSource,
    "-o", (Join-Path $buildRoot "OVERSIZED-REQUIRED.MAC")
) @{
    DCC_MIR_REQUIRE_EMIT = "1"
}
Assert-Failure "oversized MIR boundary" $oversizedRequired `
    "MIR emission failed for function oversized: no generated candidate (reason=oversized)"

Write-Host "MIR require-emit diagnostics passed."
