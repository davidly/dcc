#Requires -Version 7
$ErrorActionPreference = "Stop"

function Get-MirCompilerMutations {
    @(
        @{ Name = "baseline" },
        @{ Name = "dominance"; Before = 'errors != 0 || !mir_verify_dominance()'; After = 'errors != 0 || 0'; ExpectedFailure = 'FAIL branch value cannot escape join' },
        @{ Name = "argument-abi"; Before = 'target_type != 0 && insn->type != target_type'; After = '0 && target_type != 0 && insn->type != target_type'; ExpectedFailure = 'FAIL incorrect prototype argument type' },
        @{ Name = "call-arity"; Before = '(!prototype.variadic &&'; After = '(0 && !prototype.variadic &&'; ExpectedFailure = 'FAIL nonvariadic call rejects extra argument' },
        @{ Name = "indirect-callee"; Before = '!strcmp(insn->name, "<indirect>") && insn->src1 < 0'; After = '0 && !strcmp(insn->name, "<indirect>") && insn->src1 < 0'; ExpectedFailure = 'FAIL indirect call requires a callee value' },
        @{ Name = "callback-identity"; Before = 'if (declared >= 0) {'; After = 'if (0 && declared >= 0) {'; ExpectedFailure = 'FAIL unprototyped local callback ignores same-named global prototype' },
        @{ Name = "phi-edge-liveness"; Before = 'value == phi->src1'; After = 'value == phi->src2'; ExpectedFailure = 'FAIL PHI values must be live only on their own edges' },
        @{ Name = "call-argument-liveness"; Before = 'insn_is_call && mir_call_uses_value(insn, value)'; After = '0 && insn_is_call && mir_call_uses_value(insn, value)'; ExpectedFailure = 'FAIL argument must remain live through its matching call' },
        @{ Name = "phi-consumer-value"; Before = 'phi_value = phi->dst;'; After = 'phi_value = -1;'; ExpectedFailure = 'FAIL immediate PHI consumer forwarding' },
        @{ Name = "promotion-cache"; CompileProbe = $true }
    )
}

function Start-MirMutationProcess(
    [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory,
    [string]$LogPath, [hashtable]$Environment = @{}
) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    foreach ($name in @($start.Environment.Keys)) {
        if ($name -like "DCC_*" -or $name -eq "LLVM_PROFILE_FILE") {
            [void]$start.Environment.Remove($name)
        }
    }
    # Even instrumentation inherited through CFLAGS cannot join normal coverage.
    $profiles = Join-Path $WorkingDirectory "profiles"
    $scratch = Join-Path $WorkingDirectory "scratch"
    New-Item -ItemType Directory -Path $profiles, $scratch -Force | Out-Null
    $start.Environment["LLVM_PROFILE_FILE"] = Join-Path $profiles "%p-%m.profraw"
    foreach ($name in @("TMPDIR", "TMP", "TEMP")) {
        $start.Environment[$name] = $scratch
    }
    foreach ($name in $Environment.Keys) {
        $start.Environment[$name] = $Environment[$name]
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Failed to start $FilePath" }
    [pscustomobject]@{
        Process = $process; LogPath = $LogPath
        Stdout = $process.StandardOutput.ReadToEndAsync()
        Stderr = $process.StandardError.ReadToEndAsync()
    }
}

function Stop-MirMutationProcess($Command) {
    if (-not $Command.Process.HasExited) {
        $Command.Process.Kill($true)
        $Command.Process.WaitForExit()
    }
    $Command.Process.Dispose()
}

function Complete-MirMutationProcess($Command, [int]$TimeoutSeconds = 60) {
    try {
        $timedOut = -not $Command.Process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) {
            $Command.Process.Kill($true)
            $Command.Process.WaitForExit()
        }
        $text = $Command.Stdout.GetAwaiter().GetResult() +
            $Command.Stderr.GetAwaiter().GetResult()
        [System.IO.File]::WriteAllText($Command.LogPath, $text)
        [pscustomobject]@{
            ExitCode = $Command.Process.ExitCode; TimedOut = $timedOut; Output = $text
        }
    } finally {
        $Command.Process.Dispose()
    }
}

function Get-MirMutationOutcome($Execution, $Mutation) {
    if ($Execution.TimedOut) { return "invalid" }
    if ($Mutation.CompileProbe) {
        $mismatch = $Execution.Output -cmatch (
            '(?m)^; MIR CACHE MISMATCH mir_definition function=\S+ value=-?\d+ ' +
            'cached=-?\d+ uncached=-?\d+\r?$')
        $fatal = $Execution.Output -cmatch '(?m)^dcc: fatal: MIR use-cache mismatch\r?$'
        if ($Execution.ExitCode -eq 1 -and $mismatch -and $fatal) { return "killed" }
        if ($Execution.ExitCode -eq 0 -and -not $mismatch -and -not $fatal) {
            return "survived"
        }
    } else {
        $assertion = $Execution.Output -cmatch (
            '(?m)^' + [regex]::Escape($Mutation.ExpectedFailure) + '\r?$')
        $failed = $Execution.Output -cmatch '(?m)^MIR verifier failures=[1-9]\d*\r?$'
        if ($Execution.ExitCode -eq 1 -and $assertion -and $failed) { return "killed" }
        if ($Execution.ExitCode -eq 0 -and
            $Execution.Output -cmatch '(?m)^MIR verifier failures=0\r?$' -and
            $Execution.Output -cnotmatch '(?m)^FAIL ' -and -not $failed) {
            return "survived"
        }
    }
    return "invalid"
}

function Find-MirMutationBinary([string]$Directory, [string]$Name) {
    $binary = @(
        (Join-Path $Directory $Name), (Join-Path $Directory "$Name.exe"),
        (Join-Path $Directory "Debug/$Name"), (Join-Path $Directory "Debug/$Name.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $binary) { throw "Mutation binary was not built: $Directory/$Name" }
    return $binary
}

function Invoke-MirMutationWorker(
    [string]$RepoRoot, [string]$Workspace, [string]$OutputDirectory,
    [string]$Name, [int]$BuildJobs
) {
    $result = [ordered]@{
        mutation = $Name; outcome = "invalid"; phase = "prepare"
        exitCode = $null; detail = "Worker did not complete"
    }
    try {
        $mutation = Get-MirCompilerMutations | Where-Object { $_.Name -eq $Name }
        if (-not $mutation) { throw "Unknown compiler mutation: $Name" }
        New-Item -ItemType Directory -Path "$Workspace/src", "$Workspace/tests/host",
            "$Workspace/include", "$Workspace/bin", "$Workspace/output" | Out-Null
        Copy-Item -LiteralPath (Join-Path $RepoRoot "src/dcc") -Destination "$Workspace/src/dcc" -Recurse
        Copy-Item -LiteralPath (Join-Path $RepoRoot "tests/host/mir_verify.c") -Destination "$Workspace/tests/host/mir_verify.c"
        Get-ChildItem -LiteralPath $RepoRoot -Filter "*.h" -File |
            Copy-Item -Destination "$Workspace/include"
        $result.phase = "mutate"
        $sourcePath = "$Workspace/src/dcc/dcc_mir.c"
        $text = [System.IO.File]::ReadAllText($sourcePath)
        if ($mutation.Before) {
            if ([regex]::Matches($text, [regex]::Escape($mutation.Before)).Count -ne 1) {
                throw "Mutation anchor changed: $Name"
            }
            $text = $text.Replace($mutation.Before, $mutation.After)
        }
        if ($mutation.CompileProbe) {
            $start = $text.IndexOf('static int mir_promote_objects(void)')
            if ($start -lt 0) { throw "Promotion mutation start changed" }
            $end = $text.IndexOf('struct MirAllocationSummary', $start)
            if ($end -le $start) { throw "Promotion mutation end changed" }
            $body = $text.Substring($start, $end - $start)
            if ([regex]::Matches($body, 'mir_invalidate_use_cache\(\);').Count -ne 3) {
                throw "Promotion invalidation mutation inventory changed"
            }
            $text = $text.Substring(0, $start) +
                $body.Replace('mir_invalidate_use_cache();', '(void)0;') + $text.Substring($end)
        }
        [System.IO.File]::WriteAllText($sourcePath, $text)
        & (Join-Path $RepoRoot "scripts/new-mir-fuzz-source.ps1") `
            -OutputPath "$Workspace/probe.c" -Seed 23117 -Programs 1
        $result.phase = "configure"
        $execution = Complete-MirMutationProcess (Start-MirMutationProcess "cmake" @(
            "-S", "$Workspace/src/dcc", "-B", "$Workspace/cmake",
            "-DDCC_BUILD_MIR_TESTS=ON", "-DDCC_ENABLE_COVERAGE=OFF", "-DCMAKE_BUILD_TYPE=Debug",
            "-DDCC_RUNTIME_OUTPUT_DIRECTORY=$Workspace/bin"
        ) "$Workspace/output" "$OutputDirectory/configure.log") 120
        $result.exitCode = $execution.ExitCode
        if ($execution.TimedOut -or $execution.ExitCode -ne 0) {
            $result.detail = "Configure failed or timed out; see configure.log"
            return
        }
        $result.phase = "build"
        # Every tree starts empty: no cache, binaries, or objects from the baseline.
        $execution = Complete-MirMutationProcess (Start-MirMutationProcess "cmake" @(
            "--build", "$Workspace/cmake", "--target", "mir-verify-test", "dcc",
            "--config", "Debug", "--parallel", "$BuildJobs"
        ) "$Workspace/output" "$OutputDirectory/build.log") 1800
        $result.exitCode = $execution.ExitCode
        if ($execution.TimedOut -or $execution.ExitCode -ne 0) {
            $result.detail = "Build failed or timed out; see build.log"
            return
        }
        if ($mutation.CompileProbe -or $Name -eq "baseline") {
            $result.phase = "compile"
            $compiler = Find-MirMutationBinary "$Workspace/bin" "dcc"
            $execution = Complete-MirMutationProcess (Start-MirMutationProcess $compiler @(
                "-I", "$Workspace/include", "-fstack-check", "-c",
                "$Workspace/probe.c", "-o", "$Workspace/output/PROBE.MAC"
            ) "$Workspace/output" "$OutputDirectory/compile.log" @{
                DCC_MIR_CACHE_VERIFY = "1"
            }) 60
            $result.exitCode = $execution.ExitCode
            if ($mutation.CompileProbe) {
                $result.outcome = Get-MirMutationOutcome $execution $mutation
                $result.detail = "Cache probe; see compile.log"
                return
            }
            if ($execution.TimedOut -or $execution.ExitCode -ne 0 -or
                -not (Test-Path -LiteralPath "$Workspace/output/PROBE.MAC")) {
                $result.detail = "Unmutated cache probe failed; see compile.log"
                return
            }
        }
        $result.phase = "test"
        # Invoke the CTest executable directly to distinguish assertion exit 1
        # from a crash/timeout (CTest folds all of these into exit 8).
        $verifier = Find-MirMutationBinary "$Workspace/cmake" "mir-verify-test"
        $execution = Complete-MirMutationProcess (Start-MirMutationProcess $verifier @() `
            "$Workspace/output" "$OutputDirectory/test.log" @{
                DCC_MIR_CACHE_VERIFY = "1"
            }) 60
        $result.exitCode = $execution.ExitCode
        $result.outcome = Get-MirMutationOutcome $execution @{
            ExpectedFailure = $mutation.ExpectedFailure
        }
        if ($Name -eq "baseline") {
            $result.outcome = if ($result.outcome -eq "survived") { "passed" } else { "invalid" }
        }
        $result.detail = "Host verifier; see test.log"
    } finally {
        # Unexpected exceptions remain worker failures, never mutation kills.
        $result | ConvertTo-Json | Set-Content -LiteralPath "$OutputDirectory/result.json"
        if (Test-Path -LiteralPath "$Workspace/output") {
            Copy-Item -LiteralPath "$Workspace/output" -Destination "$OutputDirectory/artifacts" -Recurse -Force
        }
    }
}

Export-ModuleMember -Function Get-MirCompilerMutations, Start-MirMutationProcess,
    Complete-MirMutationProcess, Stop-MirMutationProcess, Get-MirMutationOutcome,
    Invoke-MirMutationWorker
