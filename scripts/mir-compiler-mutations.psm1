#Requires -Version 7
$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "process-supervision.psm1") -Force

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
        @{ Name = "promotion-cache"; CompileProbe = $true },
        @{ Name = "deferred-call-transaction"; Before = 'matching_calls != 1 ||'; After = '0 && matching_calls != 1 ||'; ExpectedFailure = 'FAIL repeated-ID deferred direct-call transaction' },
        @{ Name = "debug-conversion-gate"; Before = 'if (opt_debug && comparison &&'; After = 'if (1 && comparison &&'; ExpectedFailure = 'FAIL release deferred binary conversion gating' },
        @{ Name = "phi-call-prototype"; Before = 'if (source->opcode == MIR_PHI) {'; After = 'if (0 && source->opcode == MIR_PHI) {'; ExpectedFailure = 'FAIL PHI callback rejects excess argument' },
        @{ Name = "conditional-call-prototype"; Source = "src/dcc/dcc_ast_gen_support.c"; Before = 'if (callee != NULL && callee->kind == AST_COND) {'; After = 'if (0 && callee != NULL && callee->kind == AST_COND) {'; ExpectedFailure = 'FAIL matching conditional callback prototype' },
        @{ Name = "conditional-call-compatibility"; Source = "src/dcc/dcc_ast_gen_support.c"; Before = 'return *prototype != NULL ? 1 : -1;'; After = 'return *prototype != NULL ? 1 : 0;'; ExpectedFailure = 'FAIL incompatible conditional callback support' },
        @{ Name = "call-signature-snapshot"; Before = 'signature->present = 1;'; After = 'signature->present = 0;'; ExpectedFailure = 'FAIL recorded indirect call argument ABI' },
        @{ Name = "scalar-call-signature"; Before = '        mir_record_call_signature(call_id, call_prototype);'; After = '        (void)call_prototype;'; ExpectedFailure = 'FAIL conditional call signature snapshot' },
        @{ Name = "call-crossing-allocation"; Before = 'cross_call[value] = 1;'; After = '(void)value;'; ExpectedFailure = 'FAIL caller-saved home across call' },
        @{ Name = "wide-call-crossing-allocation"; Before = '            if (cross_call[value]) {'; After = '            if (0 && cross_call[value]) {'; ExpectedFailure = 'FAIL wide value retained caller-clobbered home across call' },
        @{ Name = "guarded-call-preservation"; Source = "src/dcc/dcc_mir_homed_cfg.c"; Before = '                preserve_de = mir_home_color_live_across('; After = '                preserve_de = 0 && mir_home_color_live_across('; ExpectedFailure = 'FAIL guarded call DE preservation' },
        @{ Name = "wide-guarded-call-preservation"; Source = "src/dcc/dcc_mir_homed_cfg.c"; Before = '                preserve_bc_iy = mir_home_color_live_across('; After = '                preserve_bc_iy = 0 && mir_home_color_live_across('; ExpectedFailure = 'FAIL guarded call BC:IY preservation' },
        @{
            Name = "allocation-first-result"
            Source = "src/dcc/dcc_mir_machine_validation_runners.c"
            Before = 'mir.insns[5].immediate != 0 ||'
            After = '0 && mir.insns[5].immediate != 0 ||'
            MatcherProbe = "5:immediate:999"
            MatcherReject = "first-allocation"
            ExpectedFailure =
                "FAIL allocation matcher accepted mutated first result"
        },
        @{
            Name = "allocation-store-width"
            Source = "src/dcc/dcc_mir_machine_validation_runners.c"
            Before = 'mir.insns[22].memory_size != 1 ||'
            After = '0 && mir.insns[22].memory_size != 1 ||'
            MatcherProbe = "22:memory_size:3"
            MatcherReject = "large-writes"
            ExpectedFailure =
                "FAIL allocation matcher accepted mutated store width"
        }
    )
}

function Start-MirMutationProcess(
    [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory,
    [string]$LogPath, [hashtable]$Environment = @{}, [string]$ParentScope = ""
) {
    $removed = @([Environment]::GetEnvironmentVariables().Keys |
        Where-Object { $_ -like "DCC_*" -or $_ -eq "LLVM_PROFILE_FILE" })
    # Even instrumentation inherited through CFLAGS cannot join normal coverage.
    $profiles = Join-Path $WorkingDirectory "profiles"
    $scratch = Join-Path $WorkingDirectory "scratch"
    New-Item -ItemType Directory -Path $profiles, $scratch -Force | Out-Null
    $overrides = @{ LLVM_PROFILE_FILE = (Join-Path $profiles "%p-%m.profraw") }
    foreach ($name in @("TMPDIR", "TMP", "TEMP")) {
        $overrides[$name] = $scratch
    }
    foreach ($name in $Environment.Keys) {
        $overrides[$name] = $Environment[$name]
    }
    Start-SupervisedProcess $FilePath $Arguments $WorkingDirectory $LogPath `
        -Environment $overrides -RemoveEnvironment $removed -ParentScope $ParentScope
}

function Stop-MirMutationProcess($Command) {
    Stop-SupervisedProcess $Command
}

function Test-MirMutationProcessComplete($Command, [double]$TimeoutSeconds) {
    Test-SupervisedProcessComplete $Command $TimeoutSeconds
}

function Complete-MirMutationProcess($Command, [double]$TimeoutSeconds = 60) {
    Complete-SupervisedProcess $Command $TimeoutSeconds
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
        $failureLabel = if ($Mutation.MatcherProbe) {
            "MIR matcher restoration failures"
        } else {
            "MIR verifier failures"
        }
        $failed = $Execution.Output -cmatch (
            '(?m)^' + [regex]::Escape($failureLabel) + '=[1-9]\d*\r?$')
        if ($Execution.ExitCode -eq 1 -and $assertion -and $failed) { return "killed" }
        if ($Execution.ExitCode -eq 0 -and
            $Execution.Output -cmatch (
                '(?m)^' + [regex]::Escape($failureLabel) + '=0\r?$') -and
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
    $processScope = $env:DCC_PROCESS_SCOPE
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
        Get-ChildItem -LiteralPath (Join-Path $RepoRoot "tests/host") `
            -Filter "*.c" -File |
            Copy-Item -Destination "$Workspace/tests/host"
        Get-ChildItem -LiteralPath $RepoRoot -Filter "*.h" -File |
            Copy-Item -Destination "$Workspace/include"
        $result.phase = "mutate"
        $source = if ($mutation.Source) {
            $mutation.Source
        } else {
            "src/dcc/dcc_mir.c"
        }
        $sourcePath = Join-Path $Workspace $source
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
        ) "$Workspace/output" "$OutputDirectory/configure.log" -ParentScope $processScope) 120
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
        ) "$Workspace/output" "$OutputDirectory/build.log" -ParentScope $processScope) 1800
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
            } -ParentScope $processScope) 60
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
        $matcherProbes = if ($Name -eq "baseline") {
            @(Get-MirCompilerMutations | Where-Object { $_.MatcherProbe })
        } elseif ($mutation.MatcherProbe) {
            @($mutation)
        } else {
            @()
        }
        foreach ($probe in $matcherProbes) {
            $result.phase = "matcher"
            $compiler = Find-MirMutationBinary "$Workspace/bin" "dcc"
            $probeLog = if ($Name -eq "baseline") {
                Join-Path $OutputDirectory (
                    "matcher-$($probe.Name).log")
            } else {
                Join-Path $OutputDirectory "test.log"
            }
            $execution = Complete-MirMutationProcess (Start-MirMutationProcess `
                (Get-Process -Id $PID).Path @(
                    "-NoLogo", "-NoProfile", "-NonInteractive", "-File",
                    (Join-Path $RepoRoot "scripts/test-mir-matcher-restoration.ps1"),
                    "-Compiler", $compiler,
                    "-Source", (Join-Path $RepoRoot "tests/tmalloch.c"),
                    "-IncludeDirectory", "$Workspace/include",
                    "-OutputPath", "$Workspace/output/TMALLOCH.MAC",
                    "-MachineMutation", $probe.MatcherProbe,
                    "-ExpectedReject", $probe.MatcherReject,
                    "-ExpectedFailure", $probe.ExpectedFailure
                ) "$Workspace/output" $probeLog @{
                    DCC_MIR_REQUIRE_COMPLETE = "1"
                    DCC_MIR_REQUIRE_EMIT = "1"
                    DCC_MIR_CACHE_VERIFY = "1"
                    DCC_MIR_MACHINE_REPORT = "1"
                    DCC_MIR_SELECT_REPORT = "1"
                } -ParentScope $processScope) 60
            $result.exitCode = $execution.ExitCode
            if ($Name -eq "baseline") {
                if ($execution.TimedOut -or $execution.ExitCode -ne 0) {
                    $result.detail =
                        "Unmutated matcher restoration probe failed; see " +
                        [System.IO.Path]::GetFileName($probeLog)
                    return
                }
            } else {
                $result.outcome = Get-MirMutationOutcome $execution $probe
                $result.detail = "Matcher restoration probe; see test.log"
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
            } -ParentScope $processScope) 60
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
    Invoke-MirMutationWorker, Test-MirMutationProcessComplete
