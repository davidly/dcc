#Requires -Version 7
param(
    [int]$RunTimeout = 30,
    [string]$Emulator = "ntvcm",
    [string[]]$Cases = @(),
    [int[]]$FuzzSeeds = @(23117, 1, 65535),
    [string]$ExecutionManifest = ""
)

$ErrorActionPreference = "Stop"
$Cases = @($Cases -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$dccmake = Join-Path $repoRoot "dccmake"
$dccCommand = if ($env:DCC) { $env:DCC } else { Join-Path $repoRoot "dcc" }
$emulator = (Get-Command $Emulator -ErrorAction Stop).Source
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-mir-clobber-tests-" + [guid]::NewGuid())
$environmentNames = @(
    "DCC_MIR_COST_REPORT",
    "DCC_MIR_CACHE_VERIFY",
    "DCC_MIR_EMIT_FUNCTION",
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT",
    "DCC_MIR_SELECT_CANDIDATE",
    "DCC_MIR_SELECT_FUNCTION",
    "DCC_MIR_SELECT_REPORT"
)
$savedEnvironment = @{}
$executedConfigurations =
    [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)

foreach ($name in $environmentNames) {
    $savedEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

function Set-ProcessEnvironment([string]$Name, [string]$Value) {
    [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

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
    return [pscustomobject]@{
        ExitCode = if ($timedOut) { -1 } else { $process.ExitCode }
        TimedOut = $timedOut
        Output = $stdout.GetAwaiter().GetResult() +
            $stderr.GetAwaiter().GetResult()
    }
}

function Test-ExactRejectionIntoGeneric(
    [string]$Output,
    [string]$Template,
    [string]$Function
) {
    $escapedFunction = [regex]::Escape($Function)
    $rejectionPattern =
        "MIR machine function=$escapedFunction " +
        "template=$([regex]::Escape($Template)) reject="
    $exactSelectionPattern =
        "MIR selection function=$escapedFunction " +
        "selector=scheduled-machine-cfg"
    $genericSelectionPattern =
        "MIR selection function=$escapedFunction " +
        "selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|" +
        "regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
    return $Output -match $rejectionPattern -and
        $Output -notmatch $exactSelectionPattern -and
        $Output -match $genericSelectionPattern
}

function Assert-RunCase(
    [string]$Name,
    [string[]]$Sources,
    [string[]]$Defines,
    [string[]]$Expected,
    [int]$ExpectedExit,
    [bool]$StackCheck,
    [bool]$Peep,
    [string]$ExactTemplate = "",
    [string]$ExactFunction = "",
    [bool]$RequireExact = $false,
    [bool]$RequireRejected = $false,
    [string]$RequiredGenericFunction = "",
    [string]$RequiredSelectorFunction = "",
    [string]$RequiredSelector = "",
    [string]$RequiredCandidate = "",
    [string[]]$RunArguments = @(),
    [string[]]$AssemblyPatterns = @(),
    [string[]]$ForbiddenAssemblyPatterns = @(),
    [bool]$OddUpperRuntime = $false,
    [string]$DebugMode = ""
) {
    $configuration = @(
        if ($StackCheck) { "stack" } else { "nostack" }
        if ($Peep) { "peep" } else { "nopeep" }
    ) -join "-"
    if ($DebugMode) {
        $configuration += "-debug-$DebugMode"
    }
    $buildDir = Join-Path $tempRoot "$Name-$configuration"
    $outputBase = ($Name -replace '[^A-Za-z0-9]', '').ToUpperInvariant()
    if ($outputBase.Length -gt 8) {
        $outputBase = $outputBase.Substring(0, 8)
    }
    $arguments = @(
        "dcc-input=$($Sources -join ',')",
        "dcc-output=$outputBase",
        "dcc-build-dir=$buildDir",
        "dcc-peep=$([string]$Peep)",
        "dcc-stack-check=$([string]$StackCheck)",
        "dcc-stack-bytes=512"
    )
    if ($DebugMode) {
        $arguments += "dcc-debug=$DebugMode"
    }
    if ($OddUpperRuntime) {
        $runtime = Get-Content -LiteralPath (
            Join-Path $repoRoot "DCCRTL.MAC") -Raw
        $start = $runtime.IndexOf("        public  __ctu")
        $end = $runtime.IndexOf("; ---- _isspace", $start)
        if ($start -lt 0 -or $end -lt 0) {
            throw "$Name could not locate __ctu in DCCRTL.MAC"
        }
        $replacement = @"
        public  __ctu
__ctu:
        ld      hl,'!'
        ld      de,0
        ret

"@
        $runtime = $runtime.Substring(0, $start) + $replacement +
            $runtime.Substring($end)
        $runtimePath =
            Join-Path $tempRoot "$Name-$configuration-runtime.MAC"
        Set-Content -LiteralPath $runtimePath -Value $runtime `
            -Encoding ascii -NoNewline
        $arguments += "dcc-runtime=$runtimePath"
    }
    foreach ($define in $Defines) {
        $arguments += "dcc-define=$define"
    }

    $savedCostReport =
        [Environment]::GetEnvironmentVariable(
            "DCC_MIR_COST_REPORT", "Process")
    if ($RequiredCandidate) {
        Set-ProcessEnvironment "DCC_MIR_COST_REPORT" "1"
    }
    try {
        $build = Invoke-WithTimeout $dccmake $arguments $repoRoot 60
    } finally {
        Set-ProcessEnvironment "DCC_MIR_COST_REPORT" $savedCostReport
    }
    if ($build.TimedOut -or $build.ExitCode -ne 0) {
        throw "$Name failed to build ($configuration):`n$($build.Output)"
    }
    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        throw "$Name build did not create $buildDir ($configuration):`n" +
            $build.Output
    }
    $assemblyPath = Join-Path $buildDir "$outputBase.MAC"
    if (-not (Test-Path -LiteralPath $assemblyPath -PathType Leaf)) {
        throw "$Name build did not create $assemblyPath ($configuration)"
    }
    $assembly = Get-Content -LiteralPath $assemblyPath -Raw
    if ($RequiredGenericFunction) {
        $requiredGenericPattern =
            "MIR selection function=$([regex]::Escape($RequiredGenericFunction)) " +
            "selector=spilled-scalar-cfg"
        if ($build.Output -notmatch $requiredGenericPattern) {
            throw "$Name did not use the required generic emitter for " +
                "$RequiredGenericFunction`:`n$($build.Output)"
        }
    }
    if ($RequiredSelectorFunction) {
        $requiredSelectorPattern =
            "MIR selection function=$([regex]::Escape($RequiredSelectorFunction)) " +
            "selector=$([regex]::Escape($RequiredSelector)) result=mir"
        if ($build.Output -notmatch $requiredSelectorPattern) {
            throw "$Name did not use selector '$RequiredSelector' for " +
                "$RequiredSelectorFunction`:`n$($build.Output)"
        }
    }
    if ($RequiredCandidate) {
        $requiredCandidatePattern =
            "MIR cost-selected function=" +
            "$([regex]::Escape($RequiredSelectorFunction)) " +
            "candidate=$([regex]::Escape($RequiredCandidate)) " +
            "selector=$([regex]::Escape($RequiredSelector)) "
        if ($build.Output -notmatch $requiredCandidatePattern) {
            throw "$Name did not select candidate '$RequiredCandidate' for " +
                "$RequiredSelectorFunction`:`n$($build.Output)"
        }
    }
    if ($ExactTemplate) {
        $templatePattern =
            "template=$([regex]::Escape($ExactTemplate)) reject="
        if ($ExactFunction) {
            $templatePattern =
                "MIR machine function=$([regex]::Escape($ExactFunction)) " +
                $templatePattern
        }
        $rejected = $build.Output -match $templatePattern
        $selectionPattern =
            "MIR selection function=$([regex]::Escape($ExactFunction)) " +
            "selector=scheduled-machine-cfg"
        $selected = if ($ExactFunction) { $build.Output -match $selectionPattern }
            else { $assembly.Contains(";@dcc.mir exact-kernel") }
        $acceptPattern =
            "MIR machine function=$([regex]::Escape($ExactFunction)) " +
            "template=$([regex]::Escape($ExactTemplate)) accept=emitted"
        $acceptedTemplate = $ExactFunction -and
            $build.Output -match $acceptPattern
        $rejectedIntoGeneric = $ExactFunction -and
            (Test-ExactRejectionIntoGeneric $build.Output $ExactTemplate $ExactFunction)
        if ($RequireRejected -and -not $rejectedIntoGeneric) {
            throw "$Name did not reject '$ExactTemplate' for '$ExactFunction' into generic code:`n$($build.Output)"
        }
        if ($RequireExact -and (-not $selected -or -not $acceptedTemplate)) {
            throw "$Name did not select required exact template " +
                "'$ExactTemplate' ($configuration):`n$($build.Output)"
        }
        if (-not $RequireExact -and -not $selected -and -not $rejected) {
            throw "$Name neither selected nor explicitly rejected exact " +
                "template '$ExactTemplate' ($configuration):`n" +
                $build.Output
        }
    }
    foreach ($pattern in $AssemblyPatterns) {
        if ($assembly -notmatch $pattern) {
            throw "$Name assembly did not match '$pattern' " +
                "($configuration)"
        }
    }
    foreach ($pattern in $ForbiddenAssemblyPatterns) {
        if ($assembly -match $pattern) {
            throw "$Name assembly unexpectedly matched '$pattern' " +
                "($configuration)"
        }
    }
    $run = Invoke-WithTimeout $emulator (
        @("-p", "-s:0", "$outputBase.COM") + $RunArguments
    ) $buildDir $RunTimeout
    if ($run.TimedOut) {
        throw "$Name timed out ($configuration)"
    }
    if ($run.ExitCode -ne $ExpectedExit) {
        throw "$Name exited $($run.ExitCode), expected $ExpectedExit " +
            "($configuration):`n$($run.Output)"
    }
    foreach ($text in $Expected) {
        if (-not $run.Output.Contains($text)) {
            throw "$Name did not emit '$text' ($configuration):`n" +
                $run.Output
        }
    }
    $executionKey = "$Name|$configuration"
    if (-not $executedConfigurations.Add($executionKey)) {
        throw "duplicate MIR clobber execution: $executionKey"
    }
}

function Assert-RequestedExecutionCounts {
    if ($Cases.Count -eq 0) {
        $expectedTotal = 0
        foreach ($definition in $caseDefinitions) {
            $stackCount = if (
                $definition.PSObject.Properties.Name -contains "StackModes"
            ) { @($definition.StackModes).Count } else { 2 }
            $debugCount = if (
                $definition.PSObject.Properties.Name -contains "DebugModes"
            ) { @($definition.DebugModes).Count } else { 0 }
            $expectedTotal += $stackCount * 2 * (1 + $debugCount)
        }
        $expectedTotal += 8 * $FuzzSeeds.Count
        $expectedTotal += 28 + 20 + 8 + 4 + 8 + 4 + 8
        if ($executedConfigurations.Count -ne $expectedTotal) {
            throw "full MIR clobber run executed " +
                "$($executedConfigurations.Count) target configurations, " +
                "expected $expectedTotal"
        }
        return
    }
    $specialCounts = @{
        fuzz = -1
        minimax = 28
        oldloops = 20
        lazywide = 8
        inlines = 4
        pairedbytes = 8
        vlaend = 4
        vlaok = 4
    }
    foreach ($requested in $Cases) {
        $expected = 0
        $patterns = @("^$([regex]::Escape($requested))\|")
        if ($specialCounts.ContainsKey($requested)) {
            $expected = $specialCounts[$requested]
            $patterns = switch ($requested) {
                "fuzz" { @("^fuzz-", "^fuzzforced-") }
                "minimax" { @("^minimax-") }
                "oldloops" { @("^oldloop-") }
                "lazywide" { @("^lazywide-") }
                "pairedbytes" { @("^pairedbytes\|", "^pairedbytes-near\|") }
                "vlaend" { @("^vlaend\|", "^vlaok\|") }
                "vlaok" { @("^vlaend\|", "^vlaok\|") }
                default { @("^$([regex]::Escape($requested))\|") }
            }
            if ($requested -eq "fuzz") {
                $expected = 8 * $FuzzSeeds.Count
                foreach ($definition in @($caseDefinitions |
                    Where-Object { $_.Name.StartsWith("fuzz-") })) {
                    $stackCount = if (
                        $definition.PSObject.Properties.Name -contains
                            "StackModes"
                    ) { @($definition.StackModes).Count } else { 2 }
                    $debugCount = if (
                        $definition.PSObject.Properties.Name -contains
                            "DebugModes"
                    ) { @($definition.DebugModes).Count } else { 0 }
                    $expected += $stackCount * 2 * (1 + $debugCount)
                }
            }
        } else {
            $definition = $caseDefinitions |
                Where-Object { $_.Name -eq $requested } |
                Select-Object -First 1
            if ($null -eq $definition) {
                throw "no execution expectation for MIR clobber case: $requested"
            }
            $stackCount = if (
                $definition.PSObject.Properties.Name -contains "StackModes"
            ) { @($definition.StackModes).Count } else { 2 }
            $debugCount = if (
                $definition.PSObject.Properties.Name -contains "DebugModes"
            ) { @($definition.DebugModes).Count } else { 0 }
            $expected = $stackCount * 2 * (1 + $debugCount)
            if ($requested -in @("regbyte", "arbiter")) {
                $expected += 4
                $patterns += "^$([regex]::Escape($requested))-forced\|"
            }
        }
        $actual = @($executedConfigurations | Where-Object {
            $key = $_
            @($patterns | Where-Object { $key -match $_ }).Count -gt 0
        }).Count
        if ($actual -ne $expected) {
            throw "MIR clobber case '$requested' executed $actual " +
                "configurations, expected $expected"
        }
    }
}

function Assert-ForcedRegionalSafe(
    [string]$Name,
    [string]$Source,
    [string]$Function,
    [string]$Expected,
    [bool]$StackCheck,
    [bool]$Peep
) {
    $configuration = @(
        if ($StackCheck) { "stack" } else { "nostack" }
        if ($Peep) { "peep" } else { "nopeep" }
    ) -join "-"
    $buildDir = Join-Path $tempRoot "$Name-forced-$configuration"
    $outputBase = ($Name -replace '[^A-Za-z0-9]', '').ToUpperInvariant()
    if ($outputBase.Length -gt 8) {
        $outputBase = $outputBase.Substring(0, 8)
    }
    $arguments = @(
        "dcc-input=$Source",
        "dcc-output=$outputBase",
        "dcc-build-dir=$buildDir",
        "dcc-peep=$([string]$Peep)",
        "dcc-stack-check=$([string]$StackCheck)",
        "dcc-stack-bytes=512"
    )

    Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $Function
    Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" "regional"
    Set-ProcessEnvironment "DCC_MIR_COST_REPORT" "1"
    try {
        $build = Invoke-WithTimeout $dccmake $arguments $repoRoot 60
    } finally {
        Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $null
        Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $null
        Set-ProcessEnvironment "DCC_MIR_COST_REPORT" $null
    }
    if ($build.TimedOut) {
        throw "$Name forced regional build timed out ($configuration)"
    }
    if ($build.ExitCode -ne 0) {
        if ($build.Output -notmatch '(?i)regional.*(invalid|reject|safe|valid)') {
            throw "$Name forced regional failed without an explicit " +
                "validation rejection ($configuration):`n$($build.Output)"
        }
        if (-not $executedConfigurations.Add(
                "$Name-forced|$configuration")) {
            throw "duplicate MIR forced-regional execution: " +
                "$Name-forced|$configuration"
        }
        return
    }

    $run = Invoke-WithTimeout $emulator @(
        "-p", "-s:0", "$outputBase.COM"
    ) $buildDir $RunTimeout
    if ($run.TimedOut -or $run.ExitCode -ne 0 -or
        -not $run.Output.Contains($Expected)) {
        throw "$Name forced regional emitted unsafe code " +
            "($configuration):`n$($run.Output)"
    }
    if (-not $executedConfigurations.Add(
            "$Name-forced|$configuration")) {
        throw "duplicate MIR forced-regional execution: " +
            "$Name-forced|$configuration"
    }
}

$fixtureRoot = Join-Path $repoRoot "tests/mir-clobber"
$caseDefinitions = @(
    [pscustomobject]@{
        Name = "qualgen"
        Sources = @(Join-Path $tempRoot "qualgen.c")
        Defines = @()
        Expected = @("MIR generated qualifier checks=576 failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "qualexpr"
        Sources = @(Join-Path $fixtureRoot "qualexpr.c")
        Defines = @()
        Expected = @("MIR qualifier expressions failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "aliasmem"
        Sources = @(Join-Path $fixtureRoot "aliasmem.c")
        Defines = @()
        Expected = @("MIR alias failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "domloop"
        Sources = @(Join-Path $fixtureRoot "domloop.c")
        Defines = @()
        Expected = @("MIR dominance failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "semantics"
        Sources = @(Join-Path $fixtureRoot "semfix.c")
        Defines = @()
        Expected = @("MIR semantics failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "cacheq"
        Sources = @(Join-Path $fixtureRoot "cacheq.c")
        Defines = @()
        Expected = @("1072")
        Exit = 0
    },
    [pscustomobject]@{
        Name = "bclong"
        Sources = @(Join-Path $fixtureRoot "bclong.c")
        Defines = @()
        Expected = @("sum=65568")
        Exit = 0
        ExactTemplate = "affine-local-fill-call-reports"
        ExactFunction = "main"
    },
    [pscustomobject]@{
        Name = "gdo"
        Sources = @(Join-Path $fixtureRoot "gdo.c")
        Defines = @()
        Expected = @("GDO failures=0")
        Exit = 0
        ExactTemplate = "constant-do-while-schedule"
        ExactFunction = "test_do_while_behavior"
    },
    [pscustomobject]@{
        Name = "gfp"
        Sources = @(Join-Path $fixtureRoot "gfp.c")
        Defines = @()
        Expected = @("BDOS:2:81", "hello world")
        Exit = 0
        ExactTemplate = "function-pointer-runtime"
        ExactFunction = "main"
    },
    [pscustomobject]@{
        Name = "gup"
        Sources = @(Join-Path $fixtureRoot "gup.c")
        Defines = @()
        Expected = @("GUP=!!!!")
        Exit = 0
        ExactTemplate = "fortran-uppercase"
        ExactFunction = "upcase"
        OddUpperRuntime = $true
    },
    [pscustomobject]@{
        Name = "gbc"
        Sources = @(Join-Path $fixtureRoot "gbc.c")
        Defines = @()
        Expected = @("checks=7 failures=0", "RESULT: PASS")
        Exit = 0
        ExactTemplate = "long-index-call-runner"
        ExactFunction = "main"
    },
    [pscustomobject]@{
        Name = "fcabs"
        Sources = @(Join-Path $fixtureRoot "fcabs.c")
        Defines = @()
        Expected = @("cmp=0,1,1,1,0", "calls=1")
        Exit = 0
        ExactTemplate = "float-comparison-report"
        ExactFunction = "compare_float"
    },
    [pscustomobject]@{
        Name = "regbyte"
        Sources = @(Join-Path $fixtureRoot "regbyte.c")
        Defines = @()
        Expected = @("regional-byte 1729 1123 79")
        Exit = 0
    },
    [pscustomobject]@{
        Name = "arbiter"
        Sources = @(Join-Path $fixtureRoot "arbiter.c")
        Defines = @()
        Expected = @("arbiter=655")
        Exit = 0
    },
    [pscustomobject]@{
        Name = "iyexact"
        Sources = @(Join-Path $fixtureRoot "iyexact.c")
        Defines = @()
        Expected = @(
            "step 1 value 102", "step 2 value 100",
            "step 3 value 309", "step 4 value 0", "GIY done"
        )
        Exit = 0
        ExactTemplate = "word-table-runner-schedule"
        ExactFunction = "main"
        RequireExact = $true
        AssemblyPatterns = @(";@dcc\.reg claim=iy")
    },
    [pscustomobject]@{
        Name = "iynear"
        Sources = @(Join-Path $fixtureRoot "iyexact.c")
        Defines = @("MIR_CLOBBER_IY_START=1")
        Expected = @(
            "step 2 value 100", "step 3 value 309",
            "step 4 value 0", "GIY done"
        )
        Exit = 0
        ExactTemplate = "word-table-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "ptrcond"
        Sources = @(Join-Path $repoRoot "tests/tptrcnd.c")
        Defines = @()
        Expected = @("tptrcnd start", "PASS")
        Exit = 0
        ExactTemplate = "pointer-condition-main"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "ptrcondv"
        Sources = @(Join-Path $repoRoot "tests/tptrcnd.c")
        Defines = @("MIR_CLOBBER_IF_COUNT=42")
        Expected = @(
            "FAIL if_count got 41 expected 42", "FAILED 1"
        )
        Exit = 1
        ExactTemplate = "pointer-condition-main"
        ExactFunction = "main"
        RequireRejected = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "ptrcondc"
        Sources = @(Join-Path $repoRoot "tests/tptrcnd.c")
        Defines = @("MIR_CLOBBER_IF_I010=7006")
        Expected = @(
            "FAIL if_i010", "FAIL if_count got 40 expected 41", "FAILED 2"
        )
        Exit = 1
        ExactTemplate = "pointer-condition-main"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "unionval"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @()
        Expected = @(
            "return/assign 7 5000 11 5018", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "unionvalv"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_MAKE_B=5001")
        Expected = @(
            "return/assign 7 5001 11 5019", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "unionname"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_LOCAL_NAME_W=1")
        Expected = @("local name 120 121 122", "tunion completed")
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "uniondest"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_COPY_TO_A=1")
        Expected = @(
            "ptr copy 7 5000 11 5018", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "unioncopy"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_ASSIGN_LOCAL=1")
        Expected = @(
            "return/assign 6 4000 10 4016", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "unionsum"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_FINAL_SUM_A=1")
        Expected = @(
            "ptr copy 5 3000 9 5018", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "unionfield"
        Sources = @(Join-Path $repoRoot "tests/tunion2.c")
        Defines = @("MIR_CLOBBER_FINAL_B_FROM_A=1")
        Expected = @(
            "ptr copy 5 5000 9 3014", "tunion completed"
        )
        Exit = 0
        ExactTemplate = "union-value-runner-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "bitreport"
        Sources = @(Join-Path $repoRoot "tests/tbitfld.c")
        Defines = @()
        Expected = @(
            "return 6 31 255 1000 1292", "tbitfield completed"
        )
        Exit = 0
        ExactTemplate = "bitfield-report-sequence"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "bitreportv"
        Sources = @(Join-Path $repoRoot "tests/tbitfld.c")
        Defines = @("MIR_CLOBBER_MAKE_D=1001")
        Expected = @(
            "return 6 31 255 1001 1293", "tbitfield completed"
        )
        Exit = 0
        ExactTemplate = "bitfield-report-sequence"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "widediv"
        Sources = @(Join-Path $repoRoot "tests/tstdlib.c")
        Defines = @()
        Expected = @("tstdlib: all tests passed")
        Exit = 0
        ExactTemplate = "wide-div-result-check"
        ExactFunction = "check_ldiv"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "widedivv"
        Sources = @(Join-Path $repoRoot "tests/tstdlib.c")
        Defines = @("MIR_CLOBBER_LDIV_NO_IDENTITY=1")
        Expected = @("tstdlib: all tests passed")
        Exit = 0
        ExactTemplate = "wide-div-result-check"
        ExactFunction = "check_ldiv"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "extralit"
        Sources = @(Join-Path $repoRoot "tests/tclit.c")
        Defines = @()
        Expected = @("test tclit completed with great success")
        Exit = 0
        ExactTemplate = "extra-literal-checks"
        ExactFunction = "check_value_literals_extra"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "extralitv"
        Sources = @(Join-Path $repoRoot "tests/tclit.c")
        Defines = @("MIR_CLOBBER_POINTER_LITERAL=78")
        Expected = @("test tclit completed with great success")
        Exit = 0
        ExactTemplate = "extra-literal-checks"
        ExactFunction = "check_value_literals_extra"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "finalcal"
        Sources = @(Join-Path $fixtureRoot "finalcal.c")
        Defines = @()
        Expected = @("tstdlib: all tests passed")
        Exit = 0
        ExactTemplate = "final-call-check-schedule"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "finalcalv"
        Sources = @(Join-Path $fixtureRoot "finalcal.c")
        Defines = @("MIR_CLOBBER_FINAL_EXTRA=1")
        Expected = @("tstdlib: all tests passed")
        Exit = 0
        ExactTemplate = "final-call-check-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "argvprnt"
        Sources = @(Join-Path $fixtureRoot "argvprnt.c")
        Defines = @()
        Expected = @(
            "argc: 1", "argv[ 0 ]: ''",
            "targs completed with great success"
        )
        Exit = 0
        ExactTemplate = "argv-print-schedule"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "argvprnv"
        Sources = @(Join-Path $fixtureRoot "argvprnt.c")
        Defines = @("MIR_CLOBBER_ARGV_EXTRA=1")
        Expected = @("argv extra", "targs completed with great success")
        Exit = 0
        ExactTemplate = "argv-print-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "execarg"
        Sources = @(Join-Path $fixtureRoot "execarg.c")
        Defines = @()
        Expected = @(
            "parent: exec missing file", "parent: execv missing file",
            "parent: exec self as child", "child: tail=' XCHILD'",
            "child: argc=2", "child: argv[1]='XCHILD'", "child: pass"
        )
        Exit = 0
        ExactTemplate = "exec-argument-schedule"
        ExactFunction = "main"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "execargv"
        Sources = @(Join-Path $fixtureRoot "execarg.c")
        Defines = @("MIR_CLOBBER_EXEC_EXTRA=1")
        Expected = @("parent: extra control", "child: pass")
        Exit = 0
        ExactTemplate = "exec-argument-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "limits"
        Sources = @(Join-Path $repoRoot "tests/tlimits.c")
        Defines = @()
        Expected = @("Results: 3/3 tests passed.")
        Exit = 0
        ExactTemplate = "endgame-width-runner"
        ExactFunction = "main"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "bytemath"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @()
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "bytemathv"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_SWAP=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "abortfil"
        Sources = @(Join-Path $fixtureRoot "abortfil.c")
        Defines = @()
        Expected = @("abort file ok")
        Exit = 0
        ExactTemplate = "abort-file-runner"
        ExactFunction = "main"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "abortfilv"
        Sources = @(Join-Path $fixtureRoot "abortfil.c")
        Defines = @("MIR_CLOBBER_ABORT_EXTRA=1")
        Expected = @("abort extra control", "abort file ok")
        Exit = 0
        ExactTemplate = "abort-file-runner"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "limitsv"
        Sources = @(Join-Path $fixtureRoot "limitsv.c")
        Defines = @()
        Expected = @(
            "limits extra control", "Results: 3/3 tests passed."
        )
        Exit = 0
        ExactTemplate = "endgame-width-runner"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "boundary"
        Sources = @(Join-Path $fixtureRoot "boundary.c")
        Defines = @()
        Args = @("3")
        Expected = @(
            "tbig: validating 4 records", "sequential verify: 4 ok, 0 bad",
            "tbig completed with great success"
        )
        Exit = 0
        ExactTemplate = "endgame-boundary-runner"
        ExactFunction = "main"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "boundaryv"
        Sources = @(Join-Path $fixtureRoot "boundary.c")
        Defines = @("MIR_CLOBBER_BOUNDARY_EXTRA=1")
        Args = @("3")
        Expected = @(
            "boundary extra control", "tbig completed with great success"
        )
        Exit = 0
        ExactTemplate = "endgame-boundary-runner"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "widen"
        Sources = @(Join-Path $fixtureRoot "widen.c")
        Defines = @()
        Expected = @("widen failures=0")
        Exit = 0
        ExactTemplate = "widen-edge-runner-schedule"
        ExactFunction = "test_widen_mul_edges"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "widensv"
        Sources = @(Join-Path $fixtureRoot "widen.c")
        Defines = @("MIR_CLOBBER_WIDEN_EXTRA=1")
        Expected = @("widen failures=0")
        Exit = 0
        ExactTemplate = "widen-edge-runner-schedule"
        ExactFunction = "test_widen_mul_edges"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "errnoex"
        Sources = @(Join-Path $fixtureRoot "errnoex.c")
        Defines = @()
        Expected = @("terrno passed")
        Exit = 0
        ExactTemplate = "errno-exercise-schedule"
        ExactFunction = "main"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "errnoexv"
        Sources = @(Join-Path $fixtureRoot "errnoex.c")
        Defines = @("MIR_CLOBBER_ERRNO_EXTRA=1")
        Expected = @("errno extra control", "terrno passed")
        Exit = 0
        ExactTemplate = "errno-exercise-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "errnoexc"
        Sources = @(Join-Path $fixtureRoot "errnoex.c")
        Defines = @("MIR_CLOBBER_ERRNO_CLOSE_VALUE=1")
        Expected = @("terrno passed")
        Exit = 0
        ExactTemplate = "errno-exercise-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatal"
        Sources = @(Join-Path $fixtureRoot "fatal.c")
        Defines = @()
        Expected = @("adaint:0: boom near ''")
        Exit = 1
        StackModes = @($false)
        ExactTemplate = "no-stack-fatal-report"
        ExactFunction = "die"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "fatalv"
        Sources = @(Join-Path $fixtureRoot "fatal.c")
        Defines = @("MIR_CLOBBER_FATAL_EXTRA=1")
        Expected = @("fatal extra control", "adaint:0: boom near ''")
        Exit = 1
        StackModes = @($false)
        ExactTemplate = "no-stack-fatal-report"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatfor"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @()
        Expected = @("forint:boom near pc=1 'LINE'")
        Exit = 1
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "fatforv"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @("MIR_CLOBBER_FORTRAN_TEMP=1")
        Expected = @("forint:boom near pc=1 'LINE'")
        Exit = 1
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatfore"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @("MIR_CLOBBER_FORTRAN_EXIT=1")
        Expected = @("forint:boom near pc=1 'LINE'")
        Exit = 2
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatfors"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @("MIR_CLOBBER_FORTRAN_STDOUT=1")
        Expected = @("forint:boom near pc=1 'LINE'")
        Exit = 1
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatforr"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @("MIR_CLOBBER_FORTRAN_REVERSE=1")
        Expected = @("forint:boom near pc=-1 'LINE'")
        Exit = 1
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "fatforg"
        Sources = @(Join-Path $fixtureRoot "fatfor.c")
        Defines = @("MIR_CLOBBER_FORTRAN_RANGE=1")
        Expected = @("forint:boom near pc=1 'LINE'")
        Exit = 1
        ExactTemplate = "fortran-fatal-schedule"
        ExactFunction = "die"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "intel"
        Sources = @(Join-Path $fixtureRoot "intel.c")
        Defines = @()
        Expected = @("intel=1,2,3,4")
        Exit = 0
        ExactTemplate = "intel-hex-load-schedule"
        ExactFunction = "load_intel"
        RequireExact = $true
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "intelv"
        Sources = @(Join-Path $fixtureRoot "intel.c")
        Defines = @("MIR_CLOBBER_INTEL_EXTRA=1")
        Expected = @("intel extra control", "intel=1,2,3,4")
        Exit = 0
        ExactTemplate = "intel-hex-load-schedule"
        ExactFunction = "load_intel"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "structv"
        Sources = @(Join-Path $repoRoot "tests/tstructi.c")
        Defines = @("MIR_CLOBBER_G_PAIR_A=30")
        Expected = @("global pair 30 1000 7 1037")
        Exit = 0
        ExactTemplate = "struct-init-reports"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "stval"
        Sources = @(Join-Path $repoRoot "tests/tstructv.c")
        Defines = @()
        Expected = @(
            "assign/arg 3 1000 7 1010",
            "ptr big 2 41 7084", "tstructval5 completed"
        )
        Exit = 0
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "stvalsum"
        Sources = @(Join-Path $repoRoot "tests/tstructv.c")
        Defines = @("MIR_CLOBBER_STRUCT_SUM_X=1")
        Expected = @(
            "return 4 2000 8 1010", "tstructval5 completed"
        )
        Exit = 0
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
    },
    [pscustomobject]@{
        Name = "stvalsrc"
        Sources = @(Join-Path $repoRoot "tests/tstructv.c")
        Defines = @("MIR_CLOBBER_STRUCT_COPY_SOURCE=1")
        Expected = @(
            "assign/arg 3 1000 7 622", "tstructval5 completed"
        )
        Exit = 0
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
    },
    [pscustomobject]@{
        Name = "stvaldst"
        Sources = @(Join-Path $repoRoot "tests/tstructv.c")
        Defines = @("MIR_CLOBBER_STRUCT_COPY_DEST=1")
        Expected = @(
            "assign/arg 3 1000 7 0", "tstructval5 completed"
        )
        Exit = 0
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
    },
    [pscustomobject]@{
        Name = "stvalfirst"
        Sources = @(Join-Path $repoRoot "tests/tstructv.c")
        Defines = @("MIR_CLOBBER_STRUCT_FIRST_COPY=1")
        Expected = @(
            "assign/arg 8 600 14 622", "tstructval5 completed"
        )
        Exit = 0
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
    },
    [pscustomobject]@{
        Name = "stringv"
        Sources = @(Join-Path $repoRoot "tests/tstri2.c")
        Defines = @("MIR_CLOBBER_G_NAME_V=1001")
        Expected = @("global name 294 1001 120")
        Exit = 0
        ExactTemplate = "string-init-reports"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "floatv"
        Sources = @(Join-Path $repoRoot "tests/tc89fini.c")
        Defines = @("MIR_CLOBBER_ARR_LIT0_EXPECT=1.75f")
        Expected = @(
            "FAIL arr_lit0 got 1.500000 expected 1.750000",
            "tc89flinit FAILED: 1"
        )
        Exit = 1
        ExactTemplate = "float-init-checks"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "bitfield"
        Sources = @(Join-Path $repoRoot "tests/tbfinit.c")
        Defines = @("MIR_CLOBBER_GMIX_A=2")
        Expected = @("FAIL gmix.a got 2 expected 1")
        Exit = 0
        ExactTemplate = "bitfield-init-checks"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "callid"
        Sources = @(Join-Path $repoRoot "tests/tclit.c")
        Defines = @("MIR_CLOBBER_ALT_LITERAL_CHECK=1")
        Expected = @(
            "FAIL struct literal member a got=22 want=23"
        )
        Exit = 1
        ExactTemplate = "value-literal-checks"
        ExactFunction = "check_value_literals"
        RequireRejected = $true
    }
)

try {
    $selectionControl =
        "; MIR machine function=target template=shape reject=operand`n" +
        "; MIR selection function=target selector=spilled-scalar-cfg result=mir"
    if (-not (Test-ExactRejectionIntoGeneric $selectionControl "shape" "target") -or
        (Test-ExactRejectionIntoGeneric (
            $selectionControl -replace "function=target selector=spilled",
                "function=other selector=spilled") "shape" "target") -or
        (Test-ExactRejectionIntoGeneric (
            $selectionControl -replace "selector=spilled-scalar-cfg",
                "selector=scheduled-machine-cfg") "shape" "target")) {
        throw "MIR exact-rejection selection evidence controls failed"
    }
    $knownCases = @($caseDefinitions.Name) + @(
        "fuzz", "inlines", "lazywide", "minimax", "oldloops", "pairedbytes",
        "vlaend", "vlaok")
    foreach ($requested in $Cases) {
        if ($requested -notin $knownCases) { throw "Unknown MIR clobber case: $requested" }
    }
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_COMPLETE" "1"
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_EMIT" "1"
    Set-ProcessEnvironment "DCC_MIR_MACHINE_REPORT" "1"
    Set-ProcessEnvironment "DCC_MIR_SELECT_REPORT" "1"

    if ($Cases.Count -eq 0 -or "fuzz" -in $Cases) {
        Set-ProcessEnvironment "DCC_MIR_CACHE_VERIFY" "1"
        foreach ($seed in $FuzzSeeds) {
            $fuzzSource = Join-Path $tempRoot "fz$seed.c"
            & (Join-Path $PSScriptRoot "new-mir-fuzz-source.ps1") -OutputPath $fuzzSource -Seed $seed
            $caseDefinitions += [pscustomobject]@{
                Name = "fuzz-$seed"; Sources = @($fuzzSource); Defines = @()
                Expected = @("MIR fuzz seed=$seed checks=96 failures=0"); Exit = 0
                DebugModes = @("true", "lines")
            }
            $caseDefinitions += [pscustomobject]@{
                Name = "fuzz-mutant-$seed"; Sources = @($fuzzSource); Defines = @("FUZZ_MUTATE=1")
                Expected = @("MIR fuzz seed=$seed checks=96 failures=96"); Exit = 1
            }
        }
    }

    if ($Cases.Count -eq 0 -or "qualgen" -in $Cases) {
        $seeds = @(0, 1, 127, 255, 256, 32767, 32768, 65535)
        $variants = @("plain", "cast", "typedef", "return", "conditional", "roundtrip")
        $source = [System.Text.StringBuilder]::new()
        [void]$source.AppendLine('#include <stdio.h>')
        $expected = [System.Collections.Generic.List[int]]::new()
        $functionNames = [System.Collections.Generic.List[string]]::new()
        foreach ($width in @(8, 16)) {
            $element = if ($width -eq 8) { "unsigned char" } else { "unsigned int" }
            [void]$source.AppendLine("typedef volatile $element *Q$width;")
            [void]$source.AppendLine("volatile $element *r$width($element *pointer) { return pointer; }")
            foreach ($variant in $variants) {
                $name = "q$($functionNames.Count)"
                $functionNames.Add($name)
                $expression = switch ($variant) {
                    "plain" { "plain" }
                    "cast" { "((volatile $element *)plain)" }
                    "typedef" { "((Q$width)plain)" }
                    "return" { "r$width(plain)" }
                    "conditional" { "(flag ? plain : observed)" }
                    "roundtrip" { "(($element *)(volatile $element *)plain)" }
                }
                [void]$source.AppendLine("unsigned int $name(unsigned int seed, int index, int flag) {")
                [void]$source.AppendLine("$element data[4], other[4]; int slot;")
                [void]$source.AppendLine("$element *plain = data; volatile $element *observed = other;")
                [void]$source.AppendLine("for (slot = 0; slot < 4; ++slot) { data[slot] = ($element)(seed + (unsigned int)slot * 257U); other[slot] = ($element)(seed + (unsigned int)slot * 257U + 19U); }")
                [void]$source.AppendLine("return (unsigned int)((unsigned int)$expression[index] + seed) ^ (unsigned int)((unsigned int)$expression[index + 1] * 257U); }")
            }
        }
        foreach ($seed in $seeds) {
            foreach ($index in 0..2) {
                foreach ($flag in 0..1) {
                    foreach ($width in @(8, 16)) {
                        $mask = if ($width -eq 8) { 255 } else { 65535 }
                        foreach ($variant in $variants) {
                            $bias = if ($variant -eq "conditional" -and $flag -eq 0) { 19 } else { 0 }
                            $left = ($seed + $index * 257 + $bias) -band $mask
                            $right = ($seed + ($index + 1) * 257 + $bias) -band $mask
                            $expected.Add((($left + $seed) -band 65535) -bxor (($right * 257) -band 65535))
                        }
                    }
                }
            }
        }
        [void]$source.AppendLine("static unsigned int seeds[8] = { $($seeds -join ',') };")
        [void]$source.AppendLine("static unsigned int expected[576] = { $($expected -join ',') };")
        [void]$source.AppendLine('static int checks, failures;')
        [void]$source.AppendLine('static void check(unsigned int actual) { if (actual != expected[checks]) { printf("FAIL generated %d got=%u expected=%u\n", checks, actual, expected[checks]); ++failures; } ++checks; }')
        [void]$source.AppendLine('int main(void) { int sample, index, flag; for (sample = 0; sample < 8; ++sample) for (index = 0; index < 3; ++index) for (flag = 0; flag < 2; ++flag) {')
        foreach ($name in $functionNames) {
            [void]$source.AppendLine("check($name(seeds[sample], index, flag));")
        }
        [void]$source.AppendLine('} printf("MIR generated qualifier checks=%d failures=%d\n", checks, failures); return failures != 0; }')
        [System.IO.File]::WriteAllText((Join-Path $tempRoot "qualgen.c"),
            $source.ToString(), [System.Text.Encoding]::ASCII)
    }

    foreach ($proofCase in @(
        @{
            Name = "qualexpr"; Source = "qualexpr.c"
            Expectations = @(
                @{ Function = "castadd"; Loads = 3; Volatile = 3; Width = 1 },
                @{ Function = "casttype"; Loads = 3; Volatile = 3; Width = 1 },
                @{ Function = "castdrop"; Loads = 1; Volatile = 0; Width = 1 },
                @{ Function = "nested"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "voidcast"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "deepcast"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "retread"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "indread"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "indlocal"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "indglobal"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "indstar"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "abstr"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "deepind"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "deepabs"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "deepglob"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "deeploc"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "arrcall"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "fldcall"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "flddeep"; Loads = 3; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "castabi"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "chainret"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "nestcast"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "rawcall"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "rawparam"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "rawabs"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "abscb"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "indword"; Loads = 1; Volatile = 1; Width = 2 },
                @{ Function = "indplain"; Loads = 1; Volatile = 0; Width = 1 },
                @{ Function = "retdeep"; Loads = 2; Volatile = 1; ByteVolatile = 1 },
                @{ Function = "inclone"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "recast"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "plainret"; Loads = 1; Volatile = 0; Width = 1 },
                @{ Function = "inread"; Loads = 1; Volatile = 1; Width = 1 },
                @{ Function = "choose"; Loads = 1; Volatile = 1; Width = 1 }
            )
        },
        @{
            Name = "semantics"; Source = "semfix.c"
            Expectations = @(
                @{ Function = "vread"; Loads = 3; Volatile = 3; Width = 1 },
                @{ Function = "vword"; Loads = 2; Volatile = 2; Width = 1 },
                @{ Function = "nread"; Loads = 1; Volatile = 0; Width = 1 }
            )
        },
        @{
            Name = "aliasmem"; Source = "aliasmem.c"
            Expectations = @(
                @{ Function = "vmember"; Loads = 3; Volatile = 3; Width = 1 },
                @{ Function = "vmword"; Loads = 2; Volatile = 2; Width = 1 },
                @{ Function = "vnested"; Loads = 3; Volatile = 3; Width = 1 },
                @{ Function = "vindirect"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vtypedef"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vold"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vglobal"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vlocal"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vpfield"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vdfield"; Loads = 9; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "vchange"; Loads = 6; Volatile = 3; ByteVolatile = 0 },
                @{ Function = "vboth"; Loads = 6; Volatile = 6; ByteVolatile = 3 },
                @{ Function = "vstatic"; Loads = 6; Volatile = 3; ByteVolatile = 3 },
                @{ Function = "nlocal"; Loads = 2; Volatile = 0 },
                @{ Function = "nindirect"; Loads = 6; Volatile = 0 },
                @{ Function = "nmember"; Loads = 1; Volatile = 0; Width = 1 },
                @{ Function = "nmword"; Loads = 1; Volatile = 0; Width = 2 },
                @{ Function = "vmstore"; Loads = 2; Volatile = 2; Width = 1; Opcode = "storeind" }
            )
        }
    )) {
        if ($Cases.Count -gt 0 -and $proofCase.Name -notin $Cases) {
            continue
        }
        Set-ProcessEnvironment "DCC_MIR_REPORT" "1"
        try {
            $proof = Invoke-WithTimeout $dccCommand @(
                "-c", (Join-Path $fixtureRoot $proofCase.Source),
                "-o", (Join-Path $tempRoot "SEMANTIC.MAC")
            ) $repoRoot 60
        } finally {
            [Environment]::SetEnvironmentVariable("DCC_MIR_REPORT",
                $savedEnvironment["DCC_MIR_REPORT"], "Process")
        }
        if ($proof.TimedOut -or $proof.ExitCode -ne 0) {
            throw "MIR $($proofCase.Name) proof failed:`n$($proof.Output)"
        }
        foreach ($expectation in $proofCase.Expectations) {
            $function = $expectation.Function
            $opcode = if ($expectation.Opcode) { $expectation.Opcode } else { "loadind" }
            $body = [regex]::Match($proof.Output,
                "(?s); MIR function=$function .*?; MIR summary function=$function ")
            $loads = [regex]::Matches($body.Value, "\b$opcode\b").Count
            if (-not $body.Success -or $loads -ne $expectation.Loads) {
                throw "$function has $loads MIR loads, expected " +
                    "$($expectation.Loads):`n$($body.Value)"
            }
            $volatileLoads = [regex]::Matches(
                $body.Value, "\b$opcode\b[^\r\n]*\bmem=\d+v\b").Count
            if ($volatileLoads -ne $expectation.Volatile) {
                throw "$function has $volatileLoads volatile MIR loads, " +
                    "expected $($expectation.Volatile):`n$($body.Value)"
            }
            if ($expectation.Width) {
                $correctWidth = [regex]::Matches($body.Value,
                    "\b$opcode\b[^\r\n]*\bmem=$($expectation.Width)v?\b").Count
                if ($correctWidth -ne $loads) {
                    throw "$function has an incorrect memory access width:`n$($body.Value)"
                }
            }
            if ($expectation.ContainsKey("ByteVolatile")) {
                $volatileBytes = [regex]::Matches($body.Value,
                    "\b$opcode\b[^\r\n]*\bmem=1v\b").Count
                if ($volatileBytes -ne $expectation.ByteVolatile) {
                    throw "$function has incorrect pointer-level volatility:`n$($body.Value)"
                }
            }
        }
    }

    foreach ($case in $caseDefinitions) {
        if ($Cases.Count -gt 0 -and $case.Name -notin $Cases -and
            -not ($case.Name.StartsWith("fuzz-") -and "fuzz" -in $Cases)) {
            continue
        }
        $stackModes = if ($case.PSObject.Properties.Name -contains
            "StackModes") {
            @($case.StackModes)
        } else {
            @($true, $false)
        }
        foreach ($stackCheck in $stackModes) {
            foreach ($peep in @($true, $false)) {
                Assert-RunCase -Name $case.Name -Sources $case.Sources `
                    -Defines $case.Defines -Expected $case.Expected `
                    -ExpectedExit $case.Exit -StackCheck $stackCheck `
                    -Peep $peep -ExactTemplate $case.ExactTemplate `
                    -ExactFunction $case.ExactFunction `
                    -RequireExact ([bool]$case.RequireExact) `
                    -RequireRejected ([bool]$case.RequireRejected) `
                    -RequiredGenericFunction $case.RequiredGenericFunction `
                    -RequiredSelectorFunction $case.RequiredSelectorFunction `
                    -RequiredSelector $case.RequiredSelector `
                    -RequiredCandidate $case.RequiredCandidate `
                    -RunArguments $case.Args `
                    -AssemblyPatterns $case.AssemblyPatterns `
                    -OddUpperRuntime ([bool]$case.OddUpperRuntime)
                foreach ($debugMode in $case.DebugModes) {
                    Assert-RunCase -Name $case.Name -Sources $case.Sources `
                        -Defines $case.Defines -Expected $case.Expected `
                        -ExpectedExit $case.Exit -StackCheck $stackCheck `
                        -Peep $peep -DebugMode $debugMode `
                        -RequiredGenericFunction $case.RequiredGenericFunction `
                        -RequiredSelectorFunction $case.RequiredSelectorFunction `
                        -RequiredSelector $case.RequiredSelector `
                        -RunArguments $case.Args
                }
            }
        }
    }

    if ($Cases.Count -eq 0 -or "fuzz" -in $Cases) {
        $forcedFunctions = @($FuzzSeeds | ForEach-Object {
            ($_ + ($_ -shr 8)) % 12
        })
        if (@($forcedFunctions | Where-Object { ($_ % 2) -eq 0 }).Count -eq 0 -or
            @($forcedFunctions | Where-Object { ($_ % 2) -ne 0 }).Count -eq 0) {
            throw "Forced fuzz functions must cover both 8-bit and 16-bit data"
        }
        try {
            foreach ($seed in $FuzzSeeds) {
                $fuzzFunction = "fuzz$(($seed + ($seed -shr 8)) % 12)"
                Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $fuzzFunction
                foreach ($candidate in @("spilled-baseline", "spilled-all")) {
                    Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $candidate
                    foreach ($stackCheck in @($true, $false)) {
                        foreach ($peep in @($true, $false)) {
                            Assert-RunCase -Name "fuzzforced-$seed-$candidate" `
                                -Sources @(Join-Path $tempRoot "fz$seed.c") -Defines @() `
                                -Expected @("MIR fuzz seed=$seed checks=96 failures=0") `
                                -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                                -RequiredGenericFunction $fuzzFunction `
                                -RequiredSelectorFunction $fuzzFunction `
                                -RequiredSelector "spilled-scalar-cfg" `
                                -RequiredCandidate $candidate
                        }
                    }
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $savedEnvironment["DCC_MIR_SELECT_FUNCTION"]
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $savedEnvironment["DCC_MIR_SELECT_CANDIDATE"]
        }
    }
    if ($Cases.Count -eq 0 -or "minimax" -in $Cases) {
        Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" "MinMax"
        try {
            foreach ($candidate in @("spilled-baseline", "spilled-all", "spilled-address-remat")) {
                Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $candidate
                foreach ($stackCheck in @($true, $false)) {
                    foreach ($peep in @($true, $false)) {
                        foreach ($debugMode in @("", "lines")) {
                            Assert-RunCase -Name "minimax-$candidate" `
                                -Sources @(Join-Path $repoRoot "tests/ttt.c") -Defines @() `
                                -Expected @("6493 moves", "1 iterations") -ExpectedExit 0 `
                                -StackCheck $stackCheck -Peep $peep -DebugMode $debugMode `
                                -RequiredGenericFunction "MinMax" `
                                -RequiredSelectorFunction "MinMax" `
                                -RequiredSelector "spilled-scalar-cfg" `
                                -RequiredCandidate $candidate
                        }
                    }
                }
            }
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $null
            foreach ($stackCheck in @($true, $false)) {
                foreach ($peep in @($true, $false)) {
                    Assert-RunCase -Name "minimax-debug" `
                        -Sources @(Join-Path $repoRoot "tests/ttt.c") `
                        -Defines @() `
                        -Expected @("6493 moves", "1 iterations") `
                        -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                        -DebugMode "true" -RequiredGenericFunction "MinMax" `
                        -RequiredSelectorFunction "MinMax" `
                        -RequiredSelector "spilled-scalar-cfg"
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $savedEnvironment["DCC_MIR_SELECT_FUNCTION"]
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" $savedEnvironment["DCC_MIR_SELECT_CANDIDATE"]
        }
    }
    if ($Cases.Count -eq 0 -or "oldloops" -in $Cases) {
        try {
            foreach ($function in @(
                "countdown", "accumulate", "divide7", "repeated", "compare")) {
                Set-ProcessEnvironment "DCC_MIR_EMIT_FUNCTION" $function
                foreach ($stackCheck in @($true, $false)) {
                    foreach ($peep in @($true, $false)) {
                        Assert-RunCase -Name "oldloop-$function" `
                            -Sources @(Join-Path $fixtureRoot "oldloops.c") `
                            -Defines @() -Expected @("0 15 7 30 11 22") `
                            -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                            -RequiredSelectorFunction $function `
                            -RequiredSelector "specialized"
                    }
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_EMIT_FUNCTION" `
                $savedEnvironment["DCC_MIR_EMIT_FUNCTION"]
        }
    }
    if ($Cases.Count -eq 0 -or "lazywide" -in $Cases) {
        try {
            foreach ($function in @("passthru", "callit")) {
                Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" $function
                Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" "homed-lazy"
                foreach ($stackCheck in @($true, $false)) {
                    foreach ($peep in @($true, $false)) {
                        Assert-RunCase -Name "lazywide-$function" `
                            -Sources @(Join-Path $fixtureRoot "lzywide.c") `
                            -Defines @() -Expected @("lazy wide passed") `
                            -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                            -RequiredSelectorFunction $function `
                            -RequiredSelector "homed-scalar-cfg" `
                            -RequiredCandidate "homed-lazy"
                    }
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" `
                $savedEnvironment["DCC_MIR_SELECT_FUNCTION"]
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" `
                $savedEnvironment["DCC_MIR_SELECT_CANDIDATE"]
        }
    }
    if ($Cases.Count -eq 0 -or "inlines" -in $Cases) {
        Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" "main"
        Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" "spilled-all"
        try {
            foreach ($stackCheck in @($true, $false)) {
                foreach ($peep in @($true, $false)) {
                    Assert-RunCase -Name "inlines" `
                        -Sources @(Join-Path $fixtureRoot "inlines.c") `
                        -Defines @() -Expected @("inline stores passed") `
                        -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                        -RunArguments @("2", "3", "29") `
                        -RequiredSelectorFunction "main" `
                        -RequiredSelector "spilled-scalar-cfg" `
                        -RequiredCandidate "spilled-all" `
                        -AssemblyPatterns @(";@dcc.mir inline-simple-store")
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" `
                $savedEnvironment["DCC_MIR_SELECT_FUNCTION"]
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" `
                $savedEnvironment["DCC_MIR_SELECT_CANDIDATE"]
        }
    }
    if ($Cases.Count -eq 0 -or "pairedbytes" -in $Cases) {
        Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" "read_pair"
        Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" "regional"
        try {
            foreach ($stackCheck in @($true, $false)) {
                foreach ($peep in @($true, $false)) {
                    Assert-RunCase -Name "pairedbytes" `
                        -Sources @(Join-Path $fixtureRoot "pairbyte.c") `
                        -Defines @() -Expected @("paired bytes passed") `
                        -ExpectedExit 0 -StackCheck $stackCheck -Peep $peep `
                        -RequiredSelectorFunction "read_pair" `
                        -RequiredSelector "regional-homed-scalar-cfg" `
                        -RequiredCandidate "regional" `
                        -AssemblyPatterns @(";@dcc.mir paired-byte-call")
                }
            }
        } finally {
            Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" `
                $null
            Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" `
                $null
        }
        foreach ($stackCheck in @($true, $false)) {
            foreach ($peep in @($true, $false)) {
                Assert-RunCase -Name "pairedbytes-near" `
                    -Sources @(Join-Path $fixtureRoot "pairbyte.c") `
                    -Defines @("MIR_CLOBBER_PAIRED_GAP=1") `
                    -Expected @("paired bytes passed") -ExpectedExit 0 `
                    -StackCheck $stackCheck -Peep $peep `
                    -RequiredGenericFunction "read_pair" `
                    -ForbiddenAssemblyPatterns @(";@dcc.mir paired-byte-call")
            }
        }
        Set-ProcessEnvironment "DCC_MIR_SELECT_FUNCTION" `
            $savedEnvironment["DCC_MIR_SELECT_FUNCTION"]
        Set-ProcessEnvironment "DCC_MIR_SELECT_CANDIDATE" `
            $savedEnvironment["DCC_MIR_SELECT_CANDIDATE"]
    }
    if ($Cases.Count -eq 0 -or
        "vlaend" -in $Cases -or "vlaok" -in $Cases) {
        foreach ($peep in @($true, $false)) {
            Assert-RunCase -Name "vlaend" `
                -Sources @(Join-Path $fixtureRoot "vlaend.c") -Defines @() `
                -Expected @("small=0,7", "stack overflow") -ExpectedExit 255 `
                -StackCheck $true -Peep $peep `
                -ExactTemplate "vla-endpoint-reduction" `
                -ExactFunction "vreduce"
            Assert-RunCase -Name "vlaok" `
                -Sources @(Join-Path $fixtureRoot "vlaend.c") -Defines @() `
                -Expected @("small=0,7", "large=30003") -ExpectedExit 0 `
                -StackCheck $false -Peep $peep `
                -ExactTemplate "vla-endpoint-reduction" `
                -ExactFunction "vreduce"
        }
    }
    if ($Cases.Count -eq 0 -or
        "regbyte" -in $Cases -or "arbiter" -in $Cases) {
        foreach ($stackCheck in @($true, $false)) {
            foreach ($peep in @($true, $false)) {
                if ($Cases.Count -eq 0 -or "regbyte" -in $Cases) {
                    Assert-ForcedRegionalSafe "regbyte" `
                        (Join-Path $fixtureRoot "regbyte.c") "late" `
                        "regional-byte 1729 1123 79" $stackCheck $peep
                }
                if ($Cases.Count -eq 0 -or "arbiter" -in $Cases) {
                    Assert-ForcedRegionalSafe "arbiter" `
                        (Join-Path $fixtureRoot "arbiter.c") "arbiter" `
                        "arbiter=655" $stackCheck $peep
                }
            }
        }
    }
    Assert-RequestedExecutionCounts
    if ($ExecutionManifest) {
        $manifestPath = if (
            [System.IO.Path]::IsPathRooted($ExecutionManifest)
        ) {
            [System.IO.Path]::GetFullPath($ExecutionManifest)
        } else {
            [System.IO.Path]::GetFullPath(
                (Join-Path $repoRoot $ExecutionManifest))
        }
        $manifestParent = Split-Path -Parent $manifestPath
        if ($manifestParent) {
            New-Item -ItemType Directory -Path $manifestParent -Force |
                Out-Null
        }
        @($executedConfigurations | Sort-Object) |
            ConvertTo-Json |
            Set-Content -LiteralPath $manifestPath -Encoding utf8
    }
    Write-Host "MIR emission-clobber regressions passed " `
        "$($executedConfigurations.Count) target configurations" `
        -ForegroundColor Green
} catch {
    $failureRoot = Join-Path $repoRoot ("build/mir-clobber-failure-" + [guid]::NewGuid())
    if (Test-Path -LiteralPath $tempRoot) {
        Copy-Item -LiteralPath $tempRoot -Destination $failureRoot -Recurse
        Write-Host "Failure source and build artifacts retained: $failureRoot"
    }
    throw
} finally {
    foreach ($name in $environmentNames) {
        Set-ProcessEnvironment $name $savedEnvironment[$name]
    }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}
