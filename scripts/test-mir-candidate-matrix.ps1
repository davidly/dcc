#Requires -Version 7
param([string]$Dcc = "")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-mir-matrix-" + [guid]::NewGuid())
$saved = @{}
$variables = @(
    "DCC_MIR_CANDIDATE_MATRIX",
    "DCC_MIR_SPILLED_POLICY",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT"
)

try {
    foreach ($name in $variables) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
    }
    New-Item -ItemType Directory -Path $workspace | Out-Null
    $control = Join-Path $workspace "CONTROL.MAC"
    $diagnostic = Join-Path $workspace "MATRIX.MAC"
    & $Dcc -I $repoRoot -c (Join-Path $repoRoot "tests/tfpshad.c") `
        -o $control *> (Join-Path $workspace "control.log")
    if ($LASTEXITCODE -ne 0) {
        throw "Candidate-matrix control compile failed"
    }
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_CANDIDATE_MATRIX", "1", "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_SPILLED_POLICY", "cost-v1", "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_REQUIRE_COMPLETE", "1", "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_REQUIRE_EMIT", "1", "Process")
    $output = & $Dcc -I $repoRoot -c (Join-Path $repoRoot "tests/tfpshad.c") `
        -o $diagnostic 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Candidate-matrix diagnostic compile failed:`n" +
            ($output -join [Environment]::NewLine)
    }
    if (-not (Compare-Object (
        Get-Content -LiteralPath $control) (
        Get-Content -LiteralPath $diagnostic) -SyncWindow 0 |
        Measure-Object).Count -eq 0) {
        throw "Candidate-matrix diagnostics changed compiler output"
    }
    $text = $output -join [Environment]::NewLine
    $candidates = @(
        "baseline", "rhs-forward", "store-address", "wide-binary-lhs",
        "stable-pointer-argument", "global-argument", "stack-argument",
        "promoted-local-slot", "all", "phi-slot", "boolean-phi-branch",
        "boolean-phi-branch-no-prepack"
    )
    foreach ($candidate in $candidates) {
        $pattern = "(?m)^; MIR candidate-matrix\s+function=invoke\s+" +
            "candidate=$([regex]::Escape($candidate))\s+"
        if ([regex]::Matches($text, $pattern).Count -ne 1) {
            throw "Candidate-matrix row missing or duplicated: $candidate"
        }
    }
    if ([regex]::Matches(
        $text,
        "(?m)^; MIR candidate-matrix-selected\s+function=invoke\s+" +
            "candidate=\S+\s+score=").Count -ne 1) {
        throw "Candidate-matrix selected row missing or duplicated"
    }
    Write-Host "MIR candidate-matrix isolation passed"
} finally {
    foreach ($name in $variables) {
        [Environment]::SetEnvironmentVariable(
            $name, $saved[$name], "Process")
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
