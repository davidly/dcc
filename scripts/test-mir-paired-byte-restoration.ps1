#Requires -Version 7
param(
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$IncludeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$ExpectedFailure =
        "FAIL paired-byte matcher accepted nonadjacent fields"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$marker = ";@dcc.mir paired-byte-call"
$failures = 0
$savedFunction = $env:DCC_MIR_SELECT_FUNCTION
$savedCandidate = $env:DCC_MIR_SELECT_CANDIDATE
$env:DCC_MIR_SELECT_FUNCTION = "read_pair"
$env:DCC_MIR_SELECT_CANDIDATE = "regional"

function Invoke-PairedByteCompile([string]$Name, [string[]]$Defines) {
    $outputPath = Join-Path $OutputDirectory "$Name.MAC"
    $arguments = @("-I", $IncludeDirectory, "-fstack-check", "-c")
    foreach ($define in $Defines) {
        $arguments += "-D$define"
    }
    $arguments += @($Source, "-o", $outputPath)
    $output = & $Compiler @arguments 2>&1
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        $output | ForEach-Object { Write-Output $_ }
        throw "Paired-byte mutation control did not compile: $Name"
    }
    return Get-Content -LiteralPath $outputPath -Raw
}

try {
    $adjacent = Invoke-PairedByteCompile "adjacent" @()
    if (-not $adjacent.Contains($marker)) {
        Write-Output "FAIL paired-byte matcher rejected adjacent fields"
        ++$failures
    }
    $nonadjacent = Invoke-PairedByteCompile "nonadjacent" @(
        "MIR_CLOBBER_PAIRED_GAP=1"
    )
    if ($nonadjacent.Contains($marker)) {
        Write-Output $ExpectedFailure
        ++$failures
    }
} finally {
    $env:DCC_MIR_SELECT_FUNCTION = $savedFunction
    $env:DCC_MIR_SELECT_CANDIDATE = $savedCandidate
}

Write-Output "MIR paired-byte mutation failures=$failures"
if ($failures -ne 0) {
    exit 1
}
