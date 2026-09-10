#Requires -Version 7
param([string]$Dcc = "")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
$workspace = Join-Path $repoRoot (
    "build/mir-scope-block-mutations-" + [guid]::NewGuid())
$source = [System.IO.File]::ReadAllText(
    (Join-Path $repoRoot "tests/tforblk.c"))
$environmentNames = @(
    "DCC_MIR_MACHINE_MUTATE",
    "DCC_MIR_MACHINE_MUTATE_FUNCTION",
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_SELECT_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT"
)
$saved = @{}

function Invoke-Compile(
    [string]$Name,
    [string]$Text,
    [string[]]$Options = @(),
    [string]$Mutation = ""
) {
    $inputFile = Join-Path $workspace "$Name.c"
    $outputFile = Join-Path $workspace "$Name.MAC"
    [System.IO.File]::WriteAllText(
        $inputFile, $Text, [System.Text.Encoding]::ASCII)
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_MACHINE_MUTATE", $Mutation, "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        $(if ($Mutation) { "main" } else { $null }), "Process")
    $output = & $Dcc @Options -I $repoRoot -c $inputFile -o $outputFile 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = $output -join [Environment]::NewLine
    }
}

function Assert-Exact([string]$Name, $Result) {
    if ($Result.ExitCode -ne 0 -or
        $Result.Text -notmatch
            'MIR machine function=main template=scope-block-runner accept=emitted' -or
        $Result.Text -notmatch
            'MIR selection function=main selector=scheduled-machine-cfg') {
        throw "$Name did not select the scope-block exact schedule:`n$($Result.Text)"
    }
}

function Assert-Generic([string]$Name, $Result) {
    if ($Result.ExitCode -ne 0 -or
        $Result.Text -notmatch
            'MIR machine function=main template=scope-block-runner reject=' -or
        $Result.Text -match
            'MIR machine function=main template=scope-block-runner accept=emitted' -or
        $Result.Text -notmatch
            'MIR selection function=main selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir') {
        throw "$Name did not reject into a generic MIR emitter:`n$($Result.Text)"
    }
}

function Replace-Once(
    [string]$Text,
    [string]$Before,
    [string]$After,
    [string]$Name
) {
    $first = $Text.IndexOf($Before)
    if ($first -lt 0 -or $Text.IndexOf($Before, $first + 1) -ge 0) {
        throw "$Name mutation anchor is not unique"
    }
    return $Text.Substring(0, $first) + $After +
        $Text.Substring($first + $Before.Length)
}

try {
    foreach ($name in $environmentNames) {
        $saved[$name] =
            [Environment]::GetEnvironmentVariable($name, "Process")
    }
    foreach ($name in @(
        "DCC_MIR_MACHINE_REPORT",
        "DCC_MIR_SELECT_REPORT",
        "DCC_MIR_REQUIRE_COMPLETE",
        "DCC_MIR_REQUIRE_EMIT"
    )) {
        [Environment]::SetEnvironmentVariable($name, "1", "Process")
    }
    New-Item -ItemType Directory -Path $workspace | Out-Null

    Assert-Exact "normal control" (Invoke-Compile "normal" $source)
    Assert-Exact "stack control" (
        Invoke-Compile "stack" $source @("-fstack-check"))
    Assert-Exact "line-debug control" (
        Invoke-Compile "line-debug" $source @("-gline"))

    $debug = Invoke-Compile "debug" $source @("-g")
    if ($debug.ExitCode -ne 0 -or
        $debug.Text -match
            'MIR machine function=main template=scope-block-runner accept=emitted' -or
        $debug.Text -notmatch
            'MIR selection function=main selector=spilled-scalar-cfg result=mir') {
        throw "Full-debug control did not use the generic emitter:`n$($debug.Text)"
    }

    $diagnosticMutations = @(
        "6:type:4",
        "6:immediate:11",
        "7:type:4",
        "7:src1:11",
        "7:memory_size:4",
        "7:identity:88",
        "12:type:2",
        "43:src1:31",
        "43:src2:29",
        "43:immediate:45",
        "153:src1:83",
        "153:identity:88",
        "159:src1:79",
        "223:type:4",
        "223:identity:88",
        "246:type:4",
        "635:type:2",
        "675:immediate:29",
        "697:src1:419"
    )
    foreach ($mutation in $diagnosticMutations) {
        Assert-Generic "MIR mutation $mutation" (
            Invoke-Compile ("mir-" + $mutation.Replace(":", "-")) `
                $source @() $mutation)
    }

    $sourceMutations = @(
        @{
            Name = "operator"
            Before = "{ int a = 3; s += a; }"
            After = "{ int a = 3; s -= a; }"
        },
        @{
            Name = "control-flow"
            Before = "while (w < 2)"
            After = "while (w <= 2)"
        },
        @{
            Name = "local-width"
            Before = "long n = 100000L;"
            After = "int n = 10000;"
        },
        @{
            Name = "volatile-local"
            Before = "int x = 20;`n            chk(x, 20L"
            After = "volatile int x = 20;`n            chk(x, 20L"
        },
        @{
            Name = "alias-dataflow"
            Before = "sum += outer;               /* 0 + 1 */"
            After = "sum += w;                   /* changed dataflow */"
        },
        @{
            Name = "call-argument"
            Before = 'chk(x, 20L, "inner x");'
            After = 'chk(20L, x, "inner x");'
        },
        @{
            Name = "call-prototype"
            Before = "static void chk(long got, long want, char *name)"
            After = "static void chk(unsigned long got, long want, char *name)"
        },
        @{
            Name = "return-flow"
            Before = "return fails != 0;"
            After = "return fails == 0;"
        }
    )
    foreach ($mutation in $sourceMutations) {
        $mutated = Replace-Once $source $mutation.Before `
            $mutation.After $mutation.Name
        Assert-Generic "source mutation $($mutation.Name)" (
            Invoke-Compile ("source-" + $mutation.Name) $mutated)
    }

    $stringVariant = Replace-Once $source '"inner x"' `
        '"inner value"' "string spelling"
    Assert-Exact "string spelling control" (
        Invoke-Compile "string-spelling" $stringVariant)

    Write-Host (
        "Scope-block exact matcher: 4 controls passed, " +
        "$($diagnosticMutations.Count + $sourceMutations.Count) " +
        "semantic mutations rejected")
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $saved[$name], "Process")
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
