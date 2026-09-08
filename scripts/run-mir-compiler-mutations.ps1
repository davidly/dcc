#Requires -Version 7
param([string]$OutputDirectory = "build/mir-compiler-mutations")

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$workspace = Join-Path ([System.IO.Path]::GetTempPath()) ("dcc-mutants-" + [guid]::NewGuid())
$output = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
$results = [System.Collections.Generic.List[object]]::new()
$savedCacheVerify = [Environment]::GetEnvironmentVariable("DCC_MIR_CACHE_VERIFY", "Process")
$mutants = @(
    @{ Name = "dominance"; Before = 'errors != 0 || !mir_verify_dominance()'; After = 'errors != 0 || 0'; ExpectedFailure = 'FAIL branch value cannot escape join' },
    @{ Name = "argument-abi"; Before = 'target_type != 0 && insn->type != target_type'; After = '0 && target_type != 0 && insn->type != target_type'; ExpectedFailure = 'FAIL incorrect prototype argument type' },
    @{ Name = "call-arity"; Before = 'has_proto && (argument_count < parameter_count ||'; After = '0 && (argument_count < parameter_count ||'; ExpectedFailure = 'FAIL known prototype requires its argument' },
    @{ Name = "promotion-cache"; CompileProbe = $true }
)
try {
    New-Item -ItemType Directory -Path "$workspace/src", "$workspace/tests/host", $output -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot "src/dcc") -Destination "$workspace/src/dcc" -Recurse
    Copy-Item -LiteralPath (Join-Path $repoRoot "tests/host/mir_verify.c") -Destination "$workspace/tests/host/mir_verify.c"
    $sourcePath = "$workspace/src/dcc/dcc_mir.c"
    $original = [System.IO.File]::ReadAllText($sourcePath)
    & (Join-Path $PSScriptRoot "new-mir-fuzz-source.ps1") -OutputPath "$workspace/probe.c" -Seed 23117 -Programs 1
    & cmake -S "$workspace/src/dcc" -B "$workspace/cmake" -DDCC_BUILD_MIR_TESTS=ON -DCMAKE_BUILD_TYPE=Debug "-DDCC_RUNTIME_OUTPUT_DIRECTORY=$workspace/bin" *> "$output/configure.log"
    if ($LASTEXITCODE -ne 0) { throw "Mutation configure failed: $output/configure.log" }
    foreach ($mutant in @(@{ Name = "baseline" }) + $mutants) {
        $text = $original
        if ($mutant.Before) {
            if ([regex]::Matches($text, [regex]::Escape($mutant.Before)).Count -ne 1) {
                throw "Mutation anchor changed: $($mutant.Name)"
            }
            $text = $text.Replace($mutant.Before, $mutant.After)
        }
        if ($mutant.CompileProbe) {
            $start = $text.IndexOf('static int mir_promote_objects(void)')
            $end = $text.IndexOf('struct MirAllocationSummary', $start)
            if ($start -lt 0 -or $end -le $start) { throw "Promotion mutation boundaries changed" }
            $body = $text.Substring($start, $end - $start)
            if ([regex]::Matches($body, 'mir_invalidate_use_cache\(\);').Count -ne 3) {
                throw "Promotion invalidation mutation inventory changed"
            }
            $text = $text.Substring(0, $start) + $body.Replace('mir_invalidate_use_cache();', '(void)0;') + $text.Substring($end)
        }
        [System.IO.File]::WriteAllText($sourcePath, $text)
        & cmake --build "$workspace/cmake" --target mir-verify-test dcc --config Debug --clean-first --parallel *> "$output/$($mutant.Name)-build.log"
        if ($LASTEXITCODE -ne 0) { throw "Invalid mutant (build failure): $($mutant.Name)" }
        if ($mutant.CompileProbe -or $mutant.Name -eq "baseline") {
            $compiler = @("$workspace/bin/dcc", "$workspace/bin/Debug/dcc.exe", "$workspace/bin/dcc.exe") |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
            if (-not $compiler) { throw "Mutation compiler was not built" }
            [Environment]::SetEnvironmentVariable("DCC_MIR_CACHE_VERIFY", "1", "Process")
            & $compiler -I $repoRoot -fstack-check -c "$workspace/probe.c" -o "$workspace/PROBE.MAC" *> "$output/$($mutant.Name)-compile.log"
            $compileExit = $LASTEXITCODE
            $compileLog = [System.IO.File]::ReadAllText("$output/$($mutant.Name)-compile.log")
            if ($mutant.Name -eq "baseline" -and $compileExit -ne 0) { throw "Unmutated cache probe failed" }
            if ($mutant.CompileProbe) {
                $outcome = if ($compileExit -ne 0 -and $compileLog -match 'MIR CACHE MISMATCH mir_definition') {
                    "killed"
                } elseif ($compileExit -eq 0) { "survived" } else { "invalid" }
                $results.Add(@{ mutation = $mutant.Name; outcome = $outcome })
                continue
            }
        }
        & ctest --test-dir "$workspace/cmake" -C Debug -R '^mir-verify$' --timeout 60 --output-on-failure *> "$output/$($mutant.Name)-test.log"
        $exitCode = $LASTEXITCODE
        $log = [System.IO.File]::ReadAllText("$output/$($mutant.Name)-test.log")
        if ($mutant.Name -eq "baseline") {
            if ($exitCode -ne 0) { throw "Unmutated verifier tests failed" }
            $outcome = "passed"
        } elseif ($exitCode -ne 0 -and $log.Contains($mutant.ExpectedFailure) -and $log -match 'MIR verifier failures=[1-9]') {
            $outcome = "killed"
        } elseif ($exitCode -eq 0) {
            $outcome = "survived"
        } else {
            $outcome = "invalid"
        }
        $results.Add(@{ mutation = $mutant.Name; outcome = $outcome })
    }
    $results | ConvertTo-Json | Set-Content -LiteralPath "$output/results.json"
    if (@($results | Where-Object { $_.outcome -in @("survived", "invalid") }).Count -gt 0) {
        throw "Compiler mutation checks failed: $output/results.json"
    }
    Write-Host "Compiler mutation controls: baseline passed, $($mutants.Count) mutants killed"
} finally {
    [Environment]::SetEnvironmentVariable("DCC_MIR_CACHE_VERIFY", $savedCacheVerify, "Process")
    Remove-Item -LiteralPath $workspace -Recurse -Force -ErrorAction SilentlyContinue
}