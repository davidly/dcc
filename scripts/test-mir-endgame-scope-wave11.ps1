#Requires -Version 7
param(
    [string]$Dcc = "",
    [string]$DccMake = "",
    [string]$Emulator = "ntvcm"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
if (-not $DccMake) {
    $DccMake = Join-Path $repoRoot "dccmake"
}
$fixture = Join-Path $repoRoot `
    "tests/mir-endgame-wave11/scopw11.c"
$buildRoot = Join-Path $repoRoot "build/mir-endgame-scope-wave11"
$expected = "tforsco passed with great success"
$environmentNames = @(
    "DCC",
    "DCC_MIR_MACHINE_MUTATE",
    "DCC_MIR_MACHINE_MUTATE_FUNCTION",
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT",
    "DCC_MIR_SELECT_REPORT"
)
$saved = @{}

function Invoke-Process(
    [string]$FilePath,
    [string[]]$Arguments,
    [int]$TimeoutSeconds
) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "failed to start $FilePath"
    }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill($true) } catch { $process.Kill() }
        throw "$FilePath timed out"
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = $stdout.GetAwaiter().GetResult() +
            $stderr.GetAwaiter().GetResult()
    }
}

function Invoke-Case(
    [string]$Name,
    [string]$Mutation,
    [bool]$RequireExact
) {
    $output = $Name.ToUpperInvariant()
    $caseBuild = Join-Path $buildRoot $Name
    if (Test-Path $caseBuild) {
        Remove-Item -LiteralPath $caseBuild -Recurse -Force
    }
    New-Item -ItemType Directory -Path $caseBuild | Out-Null
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_MACHINE_MUTATE", $Mutation, "Process")
    $compile = Invoke-Process $DccMake @(
        $fixture,
        "dcc-output=$output",
        "dcc-build-dir=$caseBuild",
        "dcc-peep=false"
    ) 120
    if ($compile.ExitCode -ne 0) {
        throw "$Name build failed:`n$($compile.Output)"
    }
    $exact =
        $compile.Output -match
            'MIR machine function=main template=endgame-scope-runner accept' -and
        $compile.Output -match
            'MIR selection function=main selector=scheduled-machine-cfg'
    if ($RequireExact -and -not $exact) {
        throw "$Name did not select the exact scope schedule:`n$($compile.Output)"
    }
    if (-not $RequireExact -and
        ($exact -or
         $compile.Output -notmatch
            'template=endgame-scope-runner reject=initial-stores' -or
         $compile.Output -notmatch
            'selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|' +
            'regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir')) {
        throw "$Name did not reject into generated fallback:`n$($compile.Output)"
    }
    $run = Invoke-Process $Emulator @(
        "-p", "-s:0", (Join-Path $caseBuild "$output.COM")
    ) 30
    $lines = @($run.Output -split '\r?\n' |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ })
    if ($run.ExitCode -ne 0 -or $lines.Count -eq 0 -or
        $lines[0] -ne $expected -or $run.Output -match 'FAIL') {
        throw "$Name runtime oracle failed (exit $($run.ExitCode)):`n" +
            $run.Output
    }
}

try {
    foreach ($name in $environmentNames) {
        $saved[$name] =
            [Environment]::GetEnvironmentVariable($name, "Process")
    }
    [Environment]::SetEnvironmentVariable("DCC", $Dcc, "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_MACHINE_MUTATE_FUNCTION", "main", "Process")
    foreach ($name in @(
        "DCC_MIR_MACHINE_REPORT",
        "DCC_MIR_REQUIRE_COMPLETE",
        "DCC_MIR_REQUIRE_EMIT",
        "DCC_MIR_SELECT_REPORT"
    )) {
        [Environment]::SetEnvironmentVariable($name, "1", "Process")
    }
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    Invoke-Case "ESGCTL" "" $true
    Invoke-Case "ESGMUT" "2:memory_size:1" $false
    Write-Host (
        "Endgame scope exact control and malformed-store fallback passed")
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $saved[$name], "Process")
    }
}
