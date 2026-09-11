#Requires -Version 7
param([string]$Dcc = "")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $Dcc) {
    $Dcc = Join-Path $repoRoot "dcc"
}
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-ptr-condition-" + [guid]::NewGuid())
$sourcePath = Join-Path $repoRoot "tests/tptrcnd.c"
$source = [System.IO.File]::ReadAllText($sourcePath)
$mainStart = $source.IndexOf("int main()")
if ($mainStart -lt 0) {
    throw "Pointer-condition mutation anchor changed: main"
}
$main = $source.Substring($mainStart)
$inactiveStart = $main.IndexOf("#ifdef PTRW25_POINTER_TRUTHINESS")
$inactiveEnd = $main.IndexOf("#else", $inactiveStart)
if ($inactiveStart -lt 0 -or $inactiveEnd -lt 0) {
    throw "Pointer-condition mutation anchor changed: inactive truthiness"
}
$mutations = [regex]::Matches(
    $main, '(?<operator>==|!=|<=|>=|<|>)\s*(?<number>\d+)(?<suffix>[LUlu]*)')
if ($mutations.Count -ne 82) {
    throw "Pointer-condition mutation inventory changed: $($mutations.Count)"
}
$environmentNames = @(
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_SELECT_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT"
)
$saved = @{}

function Invoke-Compile([string]$Text) {
    $inputFile = Join-Path $workspace "TPMUT.C"
    [System.IO.File]::WriteAllText(
        $inputFile, $Text, [System.Text.Encoding]::ASCII)
    $output = & $Dcc -I $repoRoot -c $inputFile `
        -o (Join-Path $workspace "TPMUT.MAC") 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = $output -join [Environment]::NewLine
        Assembly = if (Test-Path -LiteralPath (
            Join-Path $workspace "TPMUT.MAC")) {
            Get-Content -LiteralPath (
                Join-Path $workspace "TPMUT.MAC") -Raw
        } else {
            ""
        }
    }
}

try {
    foreach ($name in $environmentNames) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
        [Environment]::SetEnvironmentVariable($name, "1", "Process")
    }
    New-Item -ItemType Directory -Path $workspace | Out-Null
    $control = Invoke-Compile $source
    if ($control.ExitCode -ne 0 -or
        $control.Text -notmatch
            'MIR selection function=main selector=scheduled-machine-cfg') {
        throw "Pointer-condition exact control did not select"
    }
    for ($index = 0; $index -lt $mutations.Count; ++$index) {
        $mutation = $mutations[$index]
        $number = $mutation.Groups["number"]
        $replacement = ([long]$number.Value + 1).ToString() +
            $mutation.Groups["suffix"].Value
        $mutatedMain =
            $main.Substring(0, $number.Index) +
            $replacement +
            $main.Substring($number.Index + $number.Length)
        $result = Invoke-Compile (
            $source.Substring(0, $mainStart) + $mutatedMain)
        if ($result.ExitCode -ne 0) {
            throw "Pointer-condition mutation $index did not compile"
        }
        if ($number.Index -gt $inactiveStart -and
            $number.Index -lt $inactiveEnd) {
            if ($result.Text -notmatch
                    'MIR selection function=main selector=scheduled-machine-cfg' -or
                $result.Assembly -cne $control.Assembly) {
                throw "Pointer-condition inactive mutation $index changed output"
            }
            continue
        }
        if ($result.Text -notmatch
                'MIR machine function=main template=pointer-condition-main reject=' -or
            $result.Text -match
                'MIR selection function=main selector=scheduled-machine-cfg' -or
            $result.Text -notmatch
                'MIR selection function=main selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir') {
            throw "Pointer-condition mutation $index escaped exact rejection"
        }
    }
    Write-Host "Pointer-condition exact matcher rejected $($mutations.Count) semantic mutations"
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $saved[$name], "Process")
    }
    Remove-Item -LiteralPath $workspace -Recurse -Force `
        -ErrorAction SilentlyContinue
}
