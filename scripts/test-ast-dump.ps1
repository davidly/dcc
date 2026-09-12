#Requires -Version 7
param([string]$Dcc = "")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-ast-dump-" + [guid]::NewGuid())
$savedAstBuild = [Environment]::GetEnvironmentVariable(
    "DCC_AST_BUILD", "Process")

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
    [Environment]::SetEnvironmentVariable(
        "DCC_AST_BUILD", "2", "Process")
    $output = & $Dcc -I $repoRoot -c (Join-Path $workspace "tadump.c") `
        -o (Join-Path $workspace "TADUMP.MAC") 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "AST dump compile failed:`n$($output -join [Environment]::NewLine)"
    }
    $text = $output -join [Environment]::NewLine
    foreach ($kind in @("if", "assign", "call", "return", "binary", "ident", "int")) {
        if ($text -notmatch "(?m)^\s*$([regex]::Escape($kind))\b") {
            throw "AST dump omitted '$kind':`n$text"
        }
    }
    if ($text -notmatch "(?m)^\s*<null>$") {
        throw "AST dump omitted null-child markers:`n$text"
    }
    Write-Host "AST diagnostic dump passed"
} finally {
    [Environment]::SetEnvironmentVariable(
        "DCC_AST_BUILD", $savedAstBuild, "Process")
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
