#Requires -Version 7
param(
    [int]$RunTimeout = 30
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$runall = Join-Path $PSScriptRoot "runall.ps1"
$compilerName = if ($IsWindows) { "dcc.exe" } else { "dcc" }
$compiler = Join-Path $repoRoot $compilerName
$pwsh = (Get-Process -Id $PID).Path
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-mir-lifetime-tests-" + [guid]::NewGuid())
$environmentNames = @(
    "DCC_MIR_COST_REPORT",
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT",
    "DCC_MIR_SELECT_CANDIDATE",
    "DCC_MIR_SELECT_FUNCTION"
)
$savedEnvironment = @{}

foreach ($name in $environmentNames) {
    $savedEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Set-ProcessEnvironment([string]$Name, [string]$Value) {
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Assert-ForcedCandidate(
    [string]$App,
    [string]$Function,
    [string]$Candidate,
    [int]$StackSize,
    [bool]$NoStackCheck
) {
    $source = Join-Path $repoRoot "tests/$App.c"
    $configuration = if ($NoStackCheck) { "nostack" } else { "stack" }
    $assembly = Join-Path $tempRoot "$App-$Candidate-$configuration.mac"
    $report = Join-Path $tempRoot "$App-$Candidate-$configuration.log"
    $arguments = @("-stack", $StackSize, $source, "-o", $assembly)

    if (-not $NoStackCheck) {
        $arguments = @("-fstack-check") + $arguments
    }

    Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $Function
    Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $Candidate
    Set-ProcessEnvironment "DCC_MIR_COST_REPORT" "1"
    & $compiler @arguments 2> $report
    if ($LASTEXITCODE -ne 0) {
        throw "dcc failed while probing $App.$Function with $Candidate"
    }
    $expected =
        "MIR cost-selected function=$Function candidate=$Candidate "
    if (-not (Select-String -LiteralPath $report -SimpleMatch $expected)) {
        throw "$App.$Function did not select forced candidate $Candidate"
    }
    Set-ProcessEnvironment "DCC_MIR_COST_REPORT" $null
}

function Assert-MachineRejection(
    [string]$App,
    [string]$Function,
    [string]$Template,
    [string]$Reason,
    [int]$StackSize,
    [bool]$NoStackCheck
) {
    $source = Join-Path $repoRoot "tests/$App.c"
    $configuration = if ($NoStackCheck) { "nostack" } else { "stack" }
    $assembly = Join-Path $tempRoot "$App-$Function-$configuration.mac"
    $report = Join-Path $tempRoot "$App-$Function-$configuration.log"
    $arguments = @("-stack", $StackSize, $source, "-o", $assembly)

    if (-not $NoStackCheck) {
        $arguments = @("-fstack-check") + $arguments
    }
    Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $null
    Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $null
    Set-ProcessEnvironment "DCC_MIR_MACHINE_REPORT" "1"
    & $compiler @arguments 2> $report
    if ($LASTEXITCODE -ne 0) {
        throw "dcc failed while probing $App.$Function ($configuration)"
    }
    $expected =
        "function=$Function template=$Template reject=$Reason"
    if (-not (Select-String -LiteralPath $report -SimpleMatch $expected)) {
        throw "$App.$Function did not reject $Template for $Reason"
    }
    Set-ProcessEnvironment "DCC_MIR_MACHINE_REPORT" $null
}

function Invoke-ForcedRuntime(
    [string]$App,
    [string]$Function,
    [string]$Candidate,
    [int]$StackSize
) {
    foreach ($noStackCheck in @($false, $true)) {
        $configuration = if ($noStackCheck) { "nostack" } else { "stack" }
        $buildDir = Join-Path $tempRoot "$App-$Candidate-$configuration"
        Assert-ForcedCandidate `
            $App $Function $Candidate $StackSize $noStackCheck
        $arguments = @(
            "-NoProfile",
            "-File", $runall,
            "-Apps", $App,
            "-Mode", "full",
            "-RunTimeout", $RunTimeout,
            "-BuildDir", $buildDir,
            "-NoRamDisk",
            "-NoPerfCheck"
        )
        if ($noStackCheck) {
            $arguments += "-NoStackCheck"
        }
        & $pwsh @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$App.$Function failed with $Candidate ($configuration)"
        }
    }
}

try {
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_COMPLETE" "1"
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_EMIT" "1"

    foreach ($noStackCheck in @($false, $true)) {
        Assert-MachineRejection `
            "tmirlife" "parse_signed_byte" `
            "bounded-decimal-parse-schedule" `
            "argument-conversion" 512 $noStackCheck
    }
    Invoke-ForcedRuntime `
        "tmirlife" "regional_address" "regional" 512
    Invoke-ForcedRuntime `
        "cint" "primary" "spilled-phi-slot" 768

    Write-Host "MIR physical-lifetime forced tests passed" `
        -ForegroundColor Green
} finally {
    foreach ($name in $environmentNames) {
        Set-ProcessEnvironment $name $savedEnvironment[$name]
    }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}
