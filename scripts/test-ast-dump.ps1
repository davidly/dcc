#Requires -Version 7
param([string]$Dcc = "")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-ast-dump-" + [guid]::NewGuid())
$environmentNames = @(
    "DCC_AST_DUMP", "DCC_AST_BUILD",
    "DCC_MIR_CANDIDATES", "DCC_MIR_GENERAL_CANDIDATES"
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Invoke-AstCompile([string]$OutputName) {
    $output = & $Dcc -I $repoRoot -c (Join-Path $workspace "tadump.c") `
        -o (Join-Path $workspace $OutputName) 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "AST compile failed:`n$($output -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{
        Text = $output -join [Environment]::NewLine
        Assembly = [System.IO.File]::ReadAllText(
            (Join-Path $workspace $OutputName))
    }
}

try {
    New-Item -ItemType Directory -Path $workspace | Out-Null
    $source = @'
int twice(int value)
{
    return value * 2;
}

int main(void)
{
    int value = 3;
    if (value)
        value = twice(value);
    else
        value = 0;
    return value;
}
'@
    [System.IO.File]::WriteAllText(
        (Join-Path $workspace "tadump.c"), $source,
        [System.Text.Encoding]::ASCII)
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }
    $normal = Invoke-AstCompile "NORMAL.MAC"
    [Environment]::SetEnvironmentVariable("DCC_AST_BUILD", "2", "Process")
    [Environment]::SetEnvironmentVariable("DCC_MIR_CANDIDATES", "1", "Process")
    [Environment]::SetEnvironmentVariable(
        "DCC_MIR_GENERAL_CANDIDATES", "1", "Process")
    $retired = Invoke-AstCompile "RETIRED.MAC"
    if ($retired.Assembly -cne $normal.Assembly -or
        $retired.Text -cne $normal.Text) {
        throw "Retired rollout controls changed compiler output"
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }
    [Environment]::SetEnvironmentVariable("DCC_AST_DUMP", "1", "Process")
    $dump = Invoke-AstCompile "TADUMP.MAC"
    if ($dump.Assembly -cne $normal.Assembly) {
        throw "AST dump changed generated assembly"
    }
    $text = $dump.Text
    foreach ($kind in @("if", "assign", "call", "return", "binary", "ident", "int")) {
        if ($text -notmatch "(?m)^\s*$([regex]::Escape($kind))\b") {
            throw "AST dump omitted '$kind':`n$text"
        }
    }
    if ($text -notmatch "(?m)^\s*<null>$") {
        throw "AST dump omitted null-child markers:`n$text"
    }
    Write-Host "AST diagnostic dump and retired-control isolation passed"
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $savedEnvironment[$name], "Process")
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
