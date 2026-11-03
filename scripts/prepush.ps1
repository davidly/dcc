#Requires -Version 7
<#
.SYNOPSIS
Run the complete local pre-push validation gate.

.DESCRIPTION
Runs build-diagnostic regression checks, native compiler baseline validation,
and the comprehensive MIR proof suite: tool builds, script tests and audits,
normal and sanitized MIR host tests, debugger tests, compiler mutants, strict full+extended release
suites with and without stack checks, generated-MIR census, and coverage.
The release suites always use Mode full. Stops at the first failed gate and
returns its exit code. Does not commit or push.

Requires the same tools as scripts/run-mir-proof-suite.ps1, including
PowerShell 7, Python, CMake, a native compiler, ntvcm, sh, and matching Clang
coverage tools. This is a long-running gate; artifacts are retained by the
proof suite. Use -List to inspect the gates without executing tests.

.EXAMPLE
pwsh scripts/prepush.ps1

.EXAMPLE
pwsh scripts/prepush.ps1 -Jobs 8 -OutputDirectory build/prepush
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1024)]
    [int]$Jobs = [Environment]::ProcessorCount,
    [ValidateRange(0, 1024)]
    [int]$MutationJobs = 0,
    [ValidateRange(1, 1024)]
    [int]$MutationBuildJobs = 4,
    [ValidateRange(1, 3600)]
    [int]$RunTimeout = 60,
    [string]$OutputDirectory = "",
    [switch]$RequireComplete,
    [switch]$List
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$pwsh = Join-Path $PSHOME $(if ($IsWindows) { "pwsh.exe" } else { "pwsh" })
$proofArguments = @(
    "-Jobs", "$Jobs",
    "-MutationJobs", "$MutationJobs",
    "-MutationBuildJobs", "$MutationBuildJobs",
    "-RunTimeout", "$RunTimeout"
)
if ($OutputDirectory) { $proofArguments += @("-OutputDirectory", $OutputDirectory) }
if ($RequireComplete) { $proofArguments += "-RequireComplete" }
if ($List) { $proofArguments += "-List" }

Push-Location $repoRoot
try {
    if ($List) {
        Write-Host "Build-diagnostic regression checks"
    } else {
        Write-Host "Running build-diagnostic regression checks"
        & $pwsh -NoLogo -NoProfile -File (
            Join-Path $PSScriptRoot "tests/test_runall_build_diagnostics.ps1")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    if ($List) {
        Write-Host "Native compiler baseline validation"
    } else {
        Write-Host "Running native compiler baseline validation"
        & $pwsh -NoLogo -NoProfile -File (
            Join-Path $PSScriptRoot "validate-unit-test.ps1") `
            -ThrottleLimit $Jobs -RunTimeout $RunTimeout
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    Write-Host "Full MIR proof suite (Mode full, extended, stack and no-stack)"
    & $pwsh -NoLogo -NoProfile -File (
        Join-Path $PSScriptRoot "run-mir-proof-suite.ps1") @proofArguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if (-not $List) { Write-Host "Pre-push validation passed" }
} finally {
    Pop-Location
}
exit 0
