#Requires -Version 7
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDcc,
    [Parameter(Mandatory = $true)][string]$DebugDcc,
    [string]$ToolRoot = "",
    [string]$Emulator = "ntvcm",
    [int]$RunTimeout = 30
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if (-not $ToolRoot) {
    $ToolRoot = $repoRoot
}
$ToolRoot = (Resolve-Path $ToolRoot).ProviderPath
$fixture = Join-Path $repoRoot "tests/mir-clobber/w12lidx.c"
$tempRoot = Join-Path $repoRoot (
    "build/mir-long-index-wave12-" + [guid]::NewGuid())
$environmentNames = @(
    "DCC", "DCCPEEP", "DCCRTLSTRIP", "M80C", "L80C",
    "DCC_RUNTIME", "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT", "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_SELECT_REPORT", "DCC_MIR_MACHINE_MUTATE",
    "DCC_MIR_MACHINE_MUTATE_FUNCTION"
)
$savedEnvironment = @{}

function Invoke-WithTimeout(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$WorkingDirectory,
    [int]$TimeoutSeconds
) {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "failed to start $FilePath"
    }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) {
        try { $process.Kill($true) } catch { $process.Kill() }
    }
    $process.WaitForExit()
    [pscustomobject]@{
        ExitCode = if ($timedOut) { -1 } else { $process.ExitCode }
        TimedOut = $timedOut
        Output = $stdout.GetAwaiter().GetResult() +
            $stderr.GetAwaiter().GetResult()
    }
}

function Set-ProcessEnvironment([string]$Name, [string]$Value) {
    if ([string]::IsNullOrEmpty($Value)) {
        [Environment]::SetEnvironmentVariable($Name, $null, "Process")
    } else {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Assert-LongIndexCase(
    [string]$CompilerName,
    [string]$Compiler,
    [string]$Name,
    [string]$Define,
    [string]$Mutation,
    [string]$RejectReason,
    [bool]$Peep,
    [bool]$StackCheck,
    [bool]$RequireExact
) {
    $caseName = "$CompilerName-$Name"
    $buildDir = Join-Path $tempRoot $caseName
    $outputName = ("W12" + $script:caseIndex.ToString("D5"))
    $script:caseIndex++
    $arguments = @(
        "dcc-input=$fixture",
        "dcc-output=$outputName",
        "dcc-build-dir=$buildDir",
        "dcc-peep=$([string]$Peep)",
        "dcc-stack-check=$([string]$StackCheck)",
        "dcc-stack-bytes=512"
    )
    if ($Define) {
        $arguments += "dcc-define=$Define"
    }

    Set-ProcessEnvironment "DCC" $Compiler
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $Mutation
    $mutationFunction = if ($Mutation) { "main" } else { "" }
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" `
        $mutationFunction
    $build = Invoke-WithTimeout (
        Join-Path $ToolRoot "dccmake") $arguments $repoRoot 90
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" ""
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" ""
    if ($build.TimedOut -or $build.ExitCode -ne 0) {
        throw "$caseName failed to build:`n$($build.Output)"
    }

    $templatePrefix =
        "MIR machine function=main template=long-index-call-runner"
    $selectionPrefix = "MIR selection function=main"
    if ($RequireExact) {
        if ($build.Output -notmatch
                [regex]::Escape("$templatePrefix accept=emitted") -or
            $build.Output -notmatch
                [regex]::Escape(
                    "$selectionPrefix selector=scheduled-machine-cfg")) {
            throw "$caseName did not report exact acceptance:`n$($build.Output)"
        }
    } else {
        $rejectPattern = [regex]::Escape(
            "$templatePrefix reject=$RejectReason")
        $genericPattern =
            [regex]::Escape("$selectionPrefix selector=") +
            "(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|" +
            "regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
        if ($build.Output -notmatch $rejectPattern -or
            $build.Output -notmatch $genericPattern -or
            $build.Output -match [regex]::Escape(
                "$selectionPrefix selector=scheduled-machine-cfg")) {
            throw "$caseName did not reject into generated generic code:`n" +
                $build.Output
        }
    }

    $program = Join-Path $buildDir "$outputName.COM"
    $run = Invoke-WithTimeout $Emulator @("-p", "-s:0", $program) `
        $repoRoot $RunTimeout
    if ($run.TimedOut -or $run.ExitCode -ne 0 -or
        $run.Output -notmatch "checks=7 failures=0" -or
        $run.Output -notmatch "RESULT: PASS" -or
        $run.Output -match "FAIL ") {
        throw "$caseName failed its runtime/guard oracle:`n$($run.Output)"
    }
    Write-Host "PASS $caseName"
}

$rejections = @(
    @{ Name = "source-wide-index"; Define = "LONG_INDEX_UNSIGNED_COUNT";
       Reason = "shape" },
    @{ Name = "source-array-stride"; Define = "LONG_INDEX_CHAR_VALUES";
       Reason = "sum-contract" },
    @{ Name = "source-call-identity"; Define = "LONG_INDEX_SECOND_COPY";
       Reason = "call-family" },
    @{ Name = "source-call-abi"; Define = "LONG_INDEX_COPY_RETURNS_INT";
       Reason = "call-family" },
    @{ Name = "source-loop-flow"; Define = "LONG_INDEX_LOOP_STEP_TWO";
       Reason = "shape" },
    @{ Name = "source-return-flow"; Define = "LONG_INDEX_RETURN_BOOL";
       Reason = "shape" },
    @{ Name = "mutation-wide-type"; Mutation = "8:type:36";
       Reason = "call-family" },
    @{ Name = "mutation-truncation"; Mutation = "13:immediate:1";
       Reason = "wide-count-flow" },
    @{ Name = "mutation-array-bound"; Mutation = "59:immediate:17";
       Reason = "array-layout" },
    @{ Name = "mutation-array-stride"; Mutation = "65:immediate:1";
       Reason = "inline-loop" },
    @{ Name = "mutation-call-identity"; Mutation = "45:identity:120";
       Reason = "call-family" },
    @{ Name = "mutation-call-abi"; Mutation = "20:type:2";
       Reason = "call-family" },
    @{ Name = "mutation-argument-type"; Mutation = "19:type:18";
       Reason = "first-copy" },
    @{ Name = "mutation-loop-phi"; Mutation = "57:type:34";
       Reason = "inline-loop" },
    @{ Name = "mutation-return-condition"; Mutation = "138:src1:83";
       Reason = "report-return-flow" },
    @{ Name = "mutation-return-phi"; Mutation = "146:src1:84";
       Reason = "report-return-flow" }
)
$compilers = @(
    @{ Name = "release"; Path = (Resolve-Path $ReleaseDcc).ProviderPath },
    @{ Name = "debug"; Path = (Resolve-Path $DebugDcc).ProviderPath }
)

foreach ($name in $environmentNames) {
    $savedEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    Set-ProcessEnvironment "DCCPEEP" (Join-Path $ToolRoot "dccpeep")
    Set-ProcessEnvironment "DCCRTLSTRIP" (
        Join-Path $ToolRoot "dccrtlstrip")
    Set-ProcessEnvironment "M80C" (Join-Path $ToolRoot "m80c")
    Set-ProcessEnvironment "L80C" (Join-Path $ToolRoot "l80c")
    Set-ProcessEnvironment "DCC_RUNTIME" (
        Join-Path $repoRoot "DCCRTL.MAC")
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_COMPLETE" "1"
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_EMIT" "1"
    Set-ProcessEnvironment "DCC_MIR_MACHINE_REPORT" "1"
    Set-ProcessEnvironment "DCC_MIR_SELECT_REPORT" "1"
    $script:caseIndex = 0

    foreach ($compiler in $compilers) {
        Assert-LongIndexCase $compiler.Name $compiler.Path `
            "exact-stack-nopeep" "" "" "" $false $true `
            $true
        Assert-LongIndexCase $compiler.Name $compiler.Path `
            "exact-nostack-peep" "" "" "" $true $false `
            $true
        foreach ($case in $rejections) {
            Assert-LongIndexCase $compiler.Name $compiler.Path `
                $case.Name $case.Define $case.Mutation $case.Reason `
                $false $true $false
        }
    }
} finally {
    foreach ($name in $environmentNames) {
        Set-ProcessEnvironment $name $savedEnvironment[$name]
    }
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host "long-index wave12 campaign passed"
