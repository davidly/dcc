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
    $candidates = @(
        "baseline", "rhs-forward", "store-address", "wide-binary-lhs",
        "stable-pointer-argument", "global-argument", "stack-argument",
        "promoted-local-slot", "all", "phi-slot", "boolean-phi-branch",
        "boolean-phi-branch-no-prepack"
    )
    $probes = @(
        @{ Name = "shadow"; Source = "tfpshad.c"; Function = "invoke" },
        @{ Name = "time"; Source = "ttime.c"; Function = "main" },
        @{ Name = "switch"; Source = "tswitch.c"; Function = "main" },
        @{ Name = "postidx"; Source = "tpostidx.c"; Function = "main" },
        @{ Name = "ptrarr"; Source = "tptrarr.c"; Function = "main" },
        @{
            Name = "widen"; Source = "tlongopt.c"
            Function = "test_widen_mul_edges"
        },
        @{ Name = "ldiv"; Source = "tstdlib.c"; Function = "check_ldiv" }
    )
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_REQUIRE_COMPLETE", "1", "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_REQUIRE_EMIT", "1", "Process")
    foreach ($probe in $probes) {
        $control = Join-Path $workspace "$($probe.Name)-control.MAC"
        $diagnostic = Join-Path $workspace "$($probe.Name)-matrix.MAC"
        $source = Join-Path $repoRoot "tests/$($probe.Source)"
        [Environment]::SetEnvironmentVariable(
            "DCC_MIR_CANDIDATE_MATRIX", $null, "Process")
        [Environment]::SetEnvironmentVariable(
            "DCC_MIR_SPILLED_POLICY", $null, "Process")
        & $Dcc -I $repoRoot -c $source -o $control *> (
            Join-Path $workspace "$($probe.Name)-control.log")
        if ($LASTEXITCODE -ne 0) {
            throw "$($probe.Name) candidate-matrix control compile failed"
        }
        [Environment]::SetEnvironmentVariable(
            "DCC_MIR_CANDIDATE_MATRIX", "1", "Process")
        [Environment]::SetEnvironmentVariable(
            "DCC_MIR_SPILLED_POLICY", "cost-v1", "Process")
        $output = & $Dcc -I $repoRoot -c $source -o $diagnostic 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "$($probe.Name) candidate-matrix diagnostic compile failed:`n" +
                ($output -join [Environment]::NewLine)
        }
        if (-not (Compare-Object (
            Get-Content -LiteralPath $control) (
            Get-Content -LiteralPath $diagnostic) -SyncWindow 0 |
            Measure-Object).Count -eq 0) {
            throw "$($probe.Name) candidate-matrix diagnostics changed compiler output"
        }
        $text = $output -join [Environment]::NewLine
        foreach ($candidate in $candidates) {
            $pattern =
                "(?m)^; MIR candidate-matrix\s+" +
                "function=$([regex]::Escape($probe.Function))\s+" +
                "candidate=$([regex]::Escape($candidate))\s+"
            if ([regex]::Matches($text, $pattern).Count -ne 1) {
                throw "$($probe.Name) matrix row missing or duplicated: $candidate"
            }
        }
        $selectedPattern =
            "(?m)^; MIR candidate-matrix-selected\s+" +
            "function=$([regex]::Escape($probe.Function))\s+" +
            "candidate=\S+\s+score="
        if ([regex]::Matches($text, $selectedPattern).Count -ne 1) {
            throw "$($probe.Name) matrix selected row missing or duplicated"
        }
    }
    Write-Host "MIR candidate-matrix isolation passed for $($probes.Count) probes"
} finally {
    foreach ($name in $variables) {
        [Environment]::SetEnvironmentVariable(
            $name, $saved[$name], "Process")
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
