#Requires -Version 7
$ErrorActionPreference = "Stop"
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) ("dcc-fuzz-generator-" + [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Path $workspace | Out-Null
    $generator = Join-Path $PSScriptRoot "new-mir-fuzz-source.ps1"
    & $generator -OutputPath "$workspace/first.c" -Seed 23117
    & $generator -OutputPath "$workspace/replay.c" -Seed 23117
    & $generator -OutputPath "$workspace/other.c" -Seed 1
    $first = [System.IO.File]::ReadAllText("$workspace/first.c")
    $replay = [System.IO.File]::ReadAllText("$workspace/replay.c")
    $other = [System.IO.File]::ReadAllText("$workspace/other.c")
    if ($first -ne $replay) { throw "Identical seeds must produce identical source and oracle" }
    if ($first -eq $other) { throw "Distinct seeds did not vary the generated program" }
    if ([regex]::Matches($first, 'unsigned int fuzz\d+\(unsigned int seed\)').Count -ne 12) {
        throw "Incorrect generated function count"
    }
    if ($first -notmatch 'expected\[96\]' -or $first -notmatch 'seed=23117') {
        throw "Generated program lacks stable oracle/replay identity"
    }
    if ($first -notmatch '\? touch8 : alter8' -or
        $first -notmatch '\? touch16 : alter16' -or
        [regex]::Matches($first, '\(\*callback\)').Count -ne 12) {
        throw "Generated program lacks conditional callback coverage"
    }
    & $generator -OutputPath "$workspace/single.c" -Seed 23117 -Programs 1
    $single = [System.IO.File]::ReadAllText("$workspace/single.c")
    if ($single -notmatch 'expected\[8\]' -or
        [regex]::Matches($single,
            'unsigned int fuzz\d+\(unsigned int seed\)').Count -ne 1) {
        throw "Generated program count does not control oracle size"
    }
    Write-Host "MIR fuzz generator reproducibility passed"
} finally {
    Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue
}