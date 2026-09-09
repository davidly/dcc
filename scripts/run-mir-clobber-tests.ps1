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
    "DCC_MIR_MACHINE_MUTATE",
    "DCC_MIR_MACHINE_MUTATE_FUNCTION",
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
    if ([string]::IsNullOrEmpty($Value)) {
        [Environment]::SetEnvironmentVariable($Name, $null, "Process")
    } else {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
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

function Assert-MachineMutationFailure(
    [string]$Spec,
    [string]$Expected
) {
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $Spec
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" "main"
    try {
        $result = Invoke-WithTimeout $dccCommand @(
            "-c", (Join-Path $fixtureRoot "logserie.c"),
            "-o", (Join-Path $tempRoot "BADMUT.MAC")
        ) $repoRoot 60
    } finally {
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $null
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" $null
    }
    if ($result.TimedOut -or $result.ExitCode -eq 0 -or
        $result.Output -notmatch [regex]::Escape($Expected)) {
        throw "MIR mutation '$Spec' did not fail with '$Expected':`n" +
            $result.Output
    }
}

function Assert-MachineMutationIgnored {
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $null
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" "main"
    try {
        $result = Invoke-WithTimeout $dccCommand @(
            "-c", (Join-Path $fixtureRoot "logserie.c"),
            "-o", (Join-Path $tempRoot "NOMUTATE.MAC")
        ) $repoRoot 60
    } finally {
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $null
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" $null
    }
    if ($result.TimedOut -or $result.ExitCode -ne 0) {
        throw "MIR mutation without a specification was not ignored:`n" +
            $result.Output
    }
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
    [string[]]$FixturePaths = @(),
    [string[]]$AssemblyPatterns = @(),
    [string[]]$ForbiddenAssemblyPatterns = @(),
    [bool]$OddUpperRuntime = $false,
    [int]$StackBytes = 512,
    [string]$MachineMutation = "",
    [string]$MachineMutationFunction = "",
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
        "dcc-stack-bytes=$StackBytes"
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
    if ($MachineMutation) {
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $MachineMutation
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" `
            $MachineMutationFunction
    }
    try {
        $build = Invoke-WithTimeout $dccmake $arguments $repoRoot 60
    } finally {
        Set-ProcessEnvironment "DCC_MIR_COST_REPORT" $savedCostReport
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $null
        Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" $null
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
    foreach ($fixturePath in $FixturePaths) {
        Copy-Item -LiteralPath $fixturePath -Destination $buildDir
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
        catalanmut = 444
        fuzz = -1
        minimax = 28
        minimaxmut = 320
        ndivmut = 504
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
                "catalanmut" { @("^catexact\|", "^ct") }
                "fuzz" { @("^fuzz-", "^fuzzforced-") }
                "minimax" { @("^minimax-") }
                "minimaxmut" { @("^mmexact\|", "^mx") }
                "ndivmut" { @("^nd") }
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
        Name = "assigncv"
        Sources = @(Join-Path $fixtureRoot "assigncv.c")
        Defines = @()
        Expected = @("assignment coverage failures=0")
        Exit = 0
        DebugModes = @("true", "lines")
    },
    [pscustomobject]@{
        Name = "fmadddbg"
        Sources = @(Join-Path $repoRoot "tests/tfmadd.c")
        Defines = @()
        Expected = @(
            "10.000000", "36.000000", "-7.000000", "-17.000000",
            "2.000000", "3.000000", "3.750000", "3.800000",
            "10000000000.000000"
        )
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
        Name = "attnold"
        Sources = @(Join-Path $repoRoot "tests/attnc11.c")
        Defines = @("MIR_CLOBBER_DISABLE_PROJECTION_CACHE=1")
        Expected = @("accuracy  14/14")
        Exit = 0
        ExactTemplate = "fixed-forward-attention"
        ExactFunction = "forward_attention"
        RequireExact = $true
        FixturePaths = @(
            (Join-Path $repoRoot "tests/ATTN.WTS"),
            (Join-Path $repoRoot "tests/ATTN.IN")
        )
    },
    [pscustomobject]@{
        Name = "attnnear"
        Sources = @(Join-Path $repoRoot "tests/attnc11.c")
        Defines = @()
        Expected = @("accuracy  14/14")
        Exit = 0
        ExactTemplate = "fixed-forward-attention"
        ExactFunction = "forward_attention"
        RequireRejected = $true
        FixturePaths = @(
            (Join-Path $repoRoot "tests/ATTN.WTS"),
            (Join-Path $repoRoot "tests/ATTN.IN")
        )
    },
    [pscustomobject]@{
        Name = "globwalk"
        Sources = @(Join-Path $repoRoot "tests/treg.c")
        Defines = @("MIR_CLOBBER_GLOBAL_WALK=1")
        Expected = @("success")
        Exit = 0
        ExactTemplate = "fixed-byte-walk-checks"
        ExactFunction = "test_global_walk"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "globwalkv"
        Sources = @(Join-Path $repoRoot "tests/treg.c")
        Defines = @(
            "MIR_CLOBBER_GLOBAL_WALK=1",
            "MIR_CLOBBER_WALK_MULTIPLIER=4"
        )
        Expected = @("success")
        Exit = 0
        ExactTemplate = "fixed-byte-walk-checks"
        ExactFunction = "test_global_walk"
        RequireRejected = $true
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
        Name = "bytemask"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_MASK=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "byteop"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_OPCODE=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "bytecomp"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_COMPLEMENT=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "byteadd"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_ADD_ORDER=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "byteovf"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_OVERFLOW_ORDER=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "bytelogic"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @("MIR_CLOBBER_BYTE_MATH_LOGIC_ORDER=1")
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "logser"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @()
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireExact = $true
    },
    [pscustomobject]@{
        Name = "loginit"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @("MIR_CLOBBER_LOG_INIT_ORDER=1")
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "lognum"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @("MIR_CLOBBER_LOG_NUMERATOR_ORDER=1")
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "logden"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @("MIR_CLOBBER_LOG_DENOMINATOR_ORDER=1")
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "logcmp"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @("MIR_CLOBBER_LOG_COMPARE_ORDER=1")
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
    },
    [pscustomobject]@{
        Name = "logdigit"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @("MIR_CLOBBER_LOG_DIGIT_ORDER=1")
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
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

$logSeriesMutationCases = @(
    @{ Name = "lmarray"; Mutation = "18:type:2" },
    @{ Name = "lmlocal"; Mutation = "3:identity:120" },
    @{ Name = "lmzero"; Mutation = "1:immediate:1" },
    @{ Name = "lmfrac"; Mutation = "43:immediate:3" },
    @{ Name = "lmstate"; Mutation = "90:immediate:1" },
    @{ Name = "lmzcall"; Mutation = "99:type:3" },
    @{ Name = "lmadd"; Mutation = "106:type:2" },
    @{ Name = "lmseries"; Mutation = "113:immediate:2" },
    @{ Name = "lmprefix"; Mutation = "134:immediate:-1" },
    @{ Name = "lmouter"; Mutation = "137:immediate:1" },
    @{ Name = "lminner"; Mutation = "180:immediate:9999" },
    @{ Name = "lmdigit"; Mutation = "209:memory_size:2" },
    @{ Name = "lmreturn"; Mutation = "248:immediate:1" },
    @{ Name = "la24"; Mutation = "24:type:2" },
    @{ Name = "la68"; Mutation = "68:type:2" },
    @{ Name = "la97"; Mutation = "97:type:2" },
    @{ Name = "la102"; Mutation = "102:type:2" },
    @{ Name = "la104"; Mutation = "104:type:2" },
    @{ Name = "la107"; Mutation = "107:type:2" },
    @{ Name = "la207"; Mutation = "207:type:2" },
    @{ Name = "ll35"; Mutation = "35:identity:120" },
    @{ Name = "ll47"; Mutation = "47:identity:120" },
    @{ Name = "ll67"; Mutation = "67:identity:120" },
    @{ Name = "ll80"; Mutation = "80:identity:120" },
    @{ Name = "ll86"; Mutation = "86:identity:120" },
    @{ Name = "ll92"; Mutation = "92:identity:120" },
    @{ Name = "ll129"; Mutation = "129:identity:120" },
    @{ Name = "ll139"; Mutation = "139:identity:120" },
    @{ Name = "ll147"; Mutation = "147:identity:120" },
    @{ Name = "ll166"; Mutation = "166:identity:120" },
    @{ Name = "ll184"; Mutation = "184:identity:120" },
    @{ Name = "ll191"; Mutation = "191:identity:120" },
    @{ Name = "ll195"; Mutation = "195:identity:120" },
    @{ Name = "ll211"; Mutation = "211:identity:120" },
    @{ Name = "ll218"; Mutation = "218:identity:120" },
    @{ Name = "ll220"; Mutation = "220:identity:120" },
    @{ Name = "ll224"; Mutation = "224:identity:120" },
    @{ Name = "ll228"; Mutation = "228:identity:120" },
    @{ Name = "ll229"; Mutation = "229:identity:120" },
    @{ Name = "ll232"; Mutation = "232:identity:120" },
    @{ Name = "ll242"; Mutation = "242:identity:120" },
    @{ Name = "lx160"; Mutation = "16:immediate:0" },
    @{ Name = "lx200"; Mutation = "20:immediate:0" },
    @{ Name = "lx20m"; Mutation = "20:memory_size:0" },
    @{ Name = "lx22t"; Mutation = "22:type:1" },
    @{ Name = "lx23m"; Mutation = "23:memory_size:0" },
    @{ Name = "lx260"; Mutation = "26:immediate:0" },
    @{ Name = "lx26m"; Mutation = "26:memory_size:0" },
    @{ Name = "lx29m"; Mutation = "29:memory_size:0" },
    @{ Name = "lx340"; Mutation = "34:immediate:0" },
    @{ Name = "lx43t"; Mutation = "43:type:1" },
    @{ Name = "lx610"; Mutation = "61:immediate:0" },
    @{ Name = "lx650"; Mutation = "65:immediate:0" },
    @{ Name = "lx65t"; Mutation = "65:type:1" },
    @{ Name = "lx700"; Mutation = "70:immediate:0" },
    @{ Name = "lx70m"; Mutation = "70:memory_size:0" },
    @{ Name = "lx730"; Mutation = "73:immediate:0" },
    @{ Name = "lx75m"; Mutation = "75:memory_size:0" },
    @{ Name = "lx780"; Mutation = "78:immediate:0" },
    @{ Name = "lx850"; Mutation = "85:immediate:0" },
    @{ Name = "lx100"; Mutation = "100:immediate:0" },
    @{ Name = "lx111"; Mutation = "111:immediate:1" },
    @{ Name = "lx112"; Mutation = "112:immediate:0" },
    @{ Name = "lx114"; Mutation = "114:immediate:0" },
    @{ Name = "lx119"; Mutation = "119:immediate:1" },
    @{ Name = "lx120"; Mutation = "120:immediate:0" },
    @{ Name = "lx122"; Mutation = "122:immediate:0" },
    @{ Name = "lx123"; Mutation = "123:immediate:0" },
    @{ Name = "lx128"; Mutation = "128:immediate:0" },
    @{ Name = "lx134t"; Mutation = "134:type:1" },
    @{ Name = "lx164"; Mutation = "164:immediate:0" },
    @{ Name = "lx168"; Mutation = "168:immediate:0" },
    @{ Name = "lx183"; Mutation = "183:immediate:0" },
    @{ Name = "lx193"; Mutation = "193:immediate:0" },
    @{ Name = "lx197"; Mutation = "197:immediate:0" },
    @{ Name = "lx209"; Mutation = "209:immediate:0" },
    @{ Name = "lx209m"; Mutation = "209:memory_size:0" },
    @{ Name = "lx210m"; Mutation = "210:memory_size:0" },
    @{ Name = "lx212"; Mutation = "212:immediate:0" },
    @{ Name = "lx215"; Mutation = "215:immediate:0" },
    @{ Name = "lx216"; Mutation = "216:immediate:1" },
    @{ Name = "lx221"; Mutation = "221:immediate:0" },
    @{ Name = "lx226"; Mutation = "226:immediate:0" },
    @{ Name = "lx231"; Mutation = "231:immediate:0" },
    @{ Name = "lx241"; Mutation = "241:immediate:0" }
)
foreach ($mutationCase in $logSeriesMutationCases) {
    $caseDefinitions += [pscustomobject]@{
        Name = $mutationCase.Name
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @()
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
        MachineMutation = $mutationCase.Mutation
        MachineMutationFunction = "main"
    }
}

$logSeriesSsaMutations = @(
    "5:src1:999", "5:src2:999", "16:src1:999", "16:src2:999",
    "17:src1:999", "20:src1:999", "20:src2:999", "23:src1:999",
    "23:src2:999", "26:src1:999", "26:src2:999", "29:src1:999",
    "29:src2:999", "34:src1:999", "34:src2:999", "35:src1:999",
    "47:src1:999", "49:src1:999", "49:src2:999", "50:src1:999",
    "50:src2:999", "61:src1:999", "61:src2:999", "62:src1:999",
    "65:src1:999", "65:src2:999", "67:src1:999", "70:src1:999",
    "70:src2:999", "73:src1:999", "73:src2:999", "75:src1:999",
    "75:src2:999", "78:src1:999", "78:src2:999", "80:src1:999",
    "85:src1:999", "85:src2:999", "86:src1:999", "96:src1:999",
    "96:src2:999", "100:src1:999", "101:src1:999", "111:src1:999",
    "112:src1:999", "112:src2:999", "114:src1:999", "114:src2:999",
    "119:src1:999", "120:src1:999", "120:src2:999", "122:src1:999",
    "122:src2:999", "123:src1:999", "123:src2:999", "128:src1:999",
    "128:src2:999", "129:src1:999", "147:src1:999", "149:src1:999",
    "149:src2:999", "164:src1:999", "164:src2:999", "165:src1:999",
    "168:src1:999", "168:src2:999", "169:src1:999", "176:src1:999",
    "176:src2:999", "177:src1:999", "183:src1:999", "183:src2:999",
    "193:src1:999", "193:src2:999", "194:src1:999", "197:src1:999",
    "197:src2:999", "198:src1:999", "205:src1:999", "205:src2:999",
    "206:src1:999", "209:src1:999", "209:src2:999", "210:src1:999",
    "212:src1:999", "212:src2:999", "215:src1:999", "215:src2:999",
    "216:src1:999", "221:src1:999", "221:src2:999", "226:src1:999",
    "226:src2:999", "228:src1:999", "231:src1:999", "231:src2:999",
    "232:src1:999", "241:src1:999", "241:src2:999", "242:src1:999",
    "249:src1:999"
)
$logSsaIndex = 0
foreach ($mutation in $logSeriesSsaMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "ls$($logSsaIndex.ToString('000'))"
        Sources = @(Join-Path $fixtureRoot "logserie.c")
        Defines = @()
        Expected = @("0.6931471805599453094172321214581765680755001343602552541206800094933936219696947156058633269964186875")
        Exit = 0
        ExactTemplate = "log-series-driver-schedule"
        ExactFunction = "main"
        RequireRejected = $true
        MachineMutation = $mutation
        MachineMutationFunction = "main"
    }
    ++$logSsaIndex
}

$byteMathMutationCases = @(
    @{ Name = "bmparam"; Mutation = "1:type:1" },
    @{ Name = "bmparam2"; Mutation = "2:type:1" },
    @{ Name = "bmmask"; Mutation = "4:immediate:225" },
    @{ Name = "bmunary"; Mutation = "5:immediate:1" },
    @{ Name = "bmcast"; Mutation = "7:type:2" },
    @{ Name = "bmstore"; Mutation = "9:identity:120" },
    @{ Name = "bmcmp"; Mutation = "10:immediate:193" },
    @{ Name = "bmcmpc"; Mutation = "12:immediate:1" },
    @{ Name = "bmcmpop"; Mutation = "13:immediate:0" },
    @{ Name = "bmmaddr"; Mutation = "16:memory_size:2" },
    @{ Name = "bmmem"; Mutation = "17:memory_size:2" },
    @{ Name = "bmcall"; Mutation = "21:identity:120" },
    @{ Name = "bmdec"; Mutation = "26:type:2" },
    @{ Name = "bmdload"; Mutation = "30:identity:120" },
    @{ Name = "bmdcast"; Mutation = "31:immediate:1" },
    @{ Name = "bmdcmp"; Mutation = "32:immediate:0" },
    @{ Name = "bmbcd"; Mutation = "35:immediate:2" },
    @{ Name = "bmdload2"; Mutation = "39:identity:120" },
    @{ Name = "bmdcast2"; Mutation = "40:immediate:1" },
    @{ Name = "bmdcmp2"; Mutation = "41:immediate:0" },
    @{ Name = "bmphi1"; Mutation = "44:immediate:2" },
    @{ Name = "bmphi0"; Mutation = "47:immediate:1" },
    @{ Name = "bmopload"; Mutation = "63:identity:120" },
    @{ Name = "bmrhsld"; Mutation = "65:identity:120" },
    @{ Name = "bmdcall"; Mutation = "67:type:2" },
    @{ Name = "bmsload"; Mutation = "72:identity:120" },
    @{ Name = "bmscast"; Mutation = "73:immediate:1" },
    @{ Name = "bmscmp"; Mutation = "74:immediate:0" },
    @{ Name = "bmrload"; Mutation = "77:identity:120" },
    @{ Name = "bmrcast"; Mutation = "78:immediate:1" },
    @{ Name = "bmsub"; Mutation = "79:immediate:0" },
    @{ Name = "bmrtype"; Mutation = "80:type:2" },
    @{ Name = "bmrstor"; Mutation = "81:identity:120" },
    @{ Name = "bmadd"; Mutation = "99:immediate:45" },
    @{ Name = "bmcarry"; Mutation = "115:immediate:1" },
    @{ Name = "bmover"; Mutation = "129:immediate:38" },
    @{ Name = "bmor"; Mutation = "170:immediate:38" },
    @{ Name = "bmand"; Mutation = "186:immediate:124" },
    @{ Name = "bmxor"; Mutation = "196:immediate:38" },
    @{ Name = "bmneg"; Mutation = "209:immediate:124" },
    @{ Name = "bmzero"; Mutation = "217:immediate:0" }
)
foreach ($mutationCase in $byteMathMutationCases) {
    $caseDefinitions += [pscustomobject]@{
        Name = $mutationCase.Name
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @()
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
        MachineMutation = $mutationCase.Mutation
        MachineMutationFunction = "op_math"
    }
}

$byteMathSsaMutations = @(
    "5:src1:999", "6:src1:999", "6:src2:999", "7:src1:999",
    "12:src1:999", "13:src1:999", "13:src2:999", "14:src1:999",
    "28:src1:999", "31:src1:999", "32:src1:999", "32:src2:999",
    "33:src1:999", "40:src1:999", "41:src1:999", "41:src2:999",
    "42:src1:999", "49:src1:999", "49:src2:999", "53:src1:999",
    "53:src2:999", "54:src1:999", "61:src1:999", "61:src2:999",
    "62:src1:999", "73:src1:999", "74:src1:999", "74:src2:999",
    "75:src1:999", "78:src1:999", "79:src1:999", "79:src2:999",
    "80:src1:999", "84:src1:999", "89:src1:999", "90:src1:999",
    "90:src2:999", "91:src1:999", "96:src1:999", "98:src1:999",
    "99:src1:999", "99:src2:999", "103:src1:999", "104:src1:999",
    "104:src2:999", "106:src1:999", "108:src1:999", "116:src1:999",
    "116:src2:999", "118:src1:999", "118:src2:999", "119:src1:999",
    "120:src2:999", "127:src1:999", "128:src1:999", "129:src1:999",
    "129:src2:999", "131:src1:999", "131:src2:999", "132:src1:999",
    "133:src1:999", "138:src1:999", "139:src1:999", "140:src1:999",
    "140:src2:999", "142:src1:999", "142:src2:999", "143:src1:999",
    "150:src1:999", "150:src2:999", "151:src2:999", "155:src2:999",
    "161:src1:999", "162:src1:999", "162:src2:999", "163:src1:999",
    "169:src1:999", "170:src1:999", "170:src2:999", "171:src1:999",
    "172:src2:999", "177:src1:999", "178:src1:999", "178:src2:999",
    "179:src1:999", "185:src1:999", "186:src1:999", "186:src2:999",
    "187:src1:999", "188:src2:999", "195:src1:999", "196:src1:999",
    "196:src2:999", "197:src1:999", "198:src2:999", "208:src1:999",
    "209:src1:999", "209:src2:999", "210:src1:999", "211:src2:999",
    "217:src1:999", "218:src1:999", "219:src2:999"
)
$byteSsaIndex = 0
foreach ($mutation in $byteMathSsaMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "bs$($byteSsaIndex.ToString('000'))"
        Sources = @(Join-Path $fixtureRoot "bytemath.c")
        Defines = @()
        Expected = @("byte math failures=0")
        Exit = 0
        ExactTemplate = "byte-math-flags"
        ExactFunction = "op_math"
        RequireRejected = $true
        MachineMutation = $mutation
        MachineMutationFunction = "op_math"
    }
    ++$byteSsaIndex
}

$multidimMutationCases = @(
    @{ Name = "mdroot"; Mutation = "1:identity:120" },
    @{ Name = "mdlayout"; Mutation = "2:immediate:999" },
    @{ Name = "mdmember"; Mutation = "11:immediate:999" },
    @{ Name = "mdstride"; Mutation = "4:immediate:999" },
    @{ Name = "mdcheck"; Mutation = "50:identity:120" },
    @{ Name = "mdstring"; Mutation = "37:type:2" },
    @{ Name = "mdbyte"; Mutation = "46:type:1" },
    @{ Name = "mdword"; Mutation = "291:type:1" },
    @{ Name = "mdaddr"; Mutation = "45:memory_size:2" },
    @{ Name = "mdbloop"; Mutation = "100:immediate:0" },
    @{ Name = "mdcloop"; Mutation = "393:immediate:0" },
    @{ Name = "mdinit"; Mutation = "9:src2:999" },
    @{ Name = "mdalias"; Mutation = "180:src1:999" },
    @{ Name = "mdwalias"; Mutation = "348:src1:999" },
    @{ Name = "mdreturn"; Mutation = "664:identity:120" },
    @{ Name = "mdsummary"; Mutation = "666:immediate:-1" }
)
$caseDefinitions += [pscustomobject]@{
    Name = "mdexact"
    Sources = @(Join-Path $repoRoot "tests/t2darr.c")
    Defines = @()
    Expected = @("PASS multidim_array")
    Exit = 0
    ExactTemplate = "multidim-array-runner"
    ExactFunction = "main"
    RequireExact = $true
}
foreach ($mutationCase in $multidimMutationCases) {
    $caseDefinitions += [pscustomobject]@{
        Name = $mutationCase.Name
        Sources = @(Join-Path $repoRoot "tests/t2darr.c")
        Defines = @()
        Expected = @("PASS multidim_array")
        Exit = 0
        ExactTemplate = "multidim-array-runner"
        ExactFunction = "main"
        RequireRejected = $true
        MachineMutation = $mutationCase.Mutation
        MachineMutationFunction = "main"
    }
}

$multidimSweepMutations = @(
    "168:immediate:999", "172:immediate:999", "252:immediate:999",
    "336:immediate:999", "340:immediate:999", "417:immediate:999",
    "516:immediate:999", "520:immediate:999", "524:immediate:999",
    "549:immediate:999", "552:immediate:999", "6:immediate:999",
    "254:immediate:999", "256:immediate:999", "419:immediate:999",
    "421:immediate:999", "423:immediate:999", "551:immediate:999",
    "554:immediate:999", "556:immediate:999", "18:src2:999",
    "27:src2:999", "36:src2:999", "258:src2:999", "266:src2:999",
    "274:src2:999", "282:src2:999", "559:src2:999", "571:src2:999",
    "583:src2:999", "595:src2:999", "170:src2:999",
    "174:src2:999", "187:src2:999", "180:src2:999",
    "184:src1:999", "184:src2:999", "209:src1:999",
    "209:src2:999", "213:src1:999", "213:src2:999",
    "223:src2:999", "227:src2:999", "231:src1:999",
    "231:src2:999", "233:src1:999", "233:src2:999",
    "236:src2:999", "338:src2:999", "342:src2:999",
    "348:src2:999", "352:src1:999", "352:src2:999",
    "354:src2:999", "375:src1:999", "375:src2:999",
    "379:src1:999", "379:src2:999", "419:src1:999",
    "419:src2:999", "421:src1:999", "421:src2:999",
    "423:src1:999", "423:src2:999", "518:src2:999",
    "522:src2:999", "526:src2:999", "534:src1:999",
    "534:src2:999", "538:src1:999", "538:src2:999",
    "542:src1:999", "542:src2:999", "666:type:1", "675:type:1",
    "672:src1:999", "679:src1:999"
)
$multidimSweepIndex = 0
foreach ($mutation in $multidimSweepMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "ms$($multidimSweepIndex.ToString('000'))"
        Sources = @(Join-Path $repoRoot "tests/t2darr.c")
        Defines = @()
        Expected = @("PASS multidim_array")
        Exit = 0
        ExactTemplate = "multidim-array-runner"
        ExactFunction = "main"
        RequireRejected = $true
        MachineMutation = $mutation
        MachineMutationFunction = "main"
    }
    ++$multidimSweepIndex
}

$narrowedDivmodMutations = @(
    "45:type:1", "48:type:1", "90:type:1", "43:immediate:999",
    "43:type:1", "46:type:1", "45:src1:999", "48:src1:999",
    "50:type:1", "51:immediate:999", "51:src1:999", "51:src2:999",
    "51:type:1", "52:immediate:999", "52:src1:999", "52:type:1",
    "53:src1:999", "57:src1:999", "57:src2:999", "57:type:1",
    "60:immediate:999", "60:src1:999", "60:type:1", "61:immediate:999",
    "61:src1:999", "61:src2:999", "62:src1:999", "67:immediate:999",
    "65:src1:999", "65:src2:999", "65:immediate:999", "65:memory_size:3",
    "68:src1:999", "68:src2:999", "68:memory_size:3", "71:type:1",
    "72:immediate:999", "72:src1:999", "72:src2:999", "72:type:1",
    "73:src1:999", "80:immediate:999", "86:immediate:999", "78:src1:999",
    "78:src2:999", "78:immediate:999", "81:src1:999", "81:src2:999",
    "81:memory_size:3", "84:src1:999", "84:src2:999", "84:immediate:999",
    "87:src1:999", "87:src2:999", "87:memory_size:3", "90:src1:999",
    "97:immediate:999", "92:src1:999", "92:src2:999", "94:src1:999",
    "94:src2:999", "95:src1:999", "95:src2:999", "92:type:1",
    "94:type:1", "95:type:1", "98:immediate:999", "98:src1:999",
    "98:src2:999", "99:src1:999", "102:immediate:999", "102:src1:999",
    "102:src2:999", "102:type:1", "103:src1:999", "104:immediate:999",
    "104:src1:999", "104:type:1", "105:src1:999", "113:immediate:999",
    "113:src1:999", "113:src2:999", "111:type:1", "112:type:1",
    "113:type:1", "114:src1:999", "115:src1:999", "118:src1:999",
    "118:src2:999", "118:immediate:999", "118:memory_size:3",
    "121:immediate:999", "121:src1:999", "119:type:1", "121:type:1",
    "122:immediate:999", "122:src1:999", "122:src2:999", "122:type:1",
    "123:immediate:999", "123:src1:999", "123:type:1", "124:src1:999",
    "124:src2:999", "124:memory_size:3", "125:type:1", "129:immediate:999",
    "129:src1:999", "130:immediate:999", "130:src1:999", "130:src2:999",
    "129:type:1", "130:type:1", "131:src1:999", "131:src2:999",
    "131:immediate:999", "131:memory_size:3", "132:src1:999",
    "132:memory_size:3", "132:type:1", "133:immediate:999", "133:src1:999",
    "133:type:1", "134:immediate:999", "134:src1:999"
)
$caseDefinitions += [pscustomobject]@{
    Name = "ndexact"
    Sources = @(Join-Path $repoRoot "tests/tdmfuse.c")
    Defines = @()
    Expected = @("checks=66 failures=0", "RESULT: PASS")
    Exit = 0
    ExactTemplate = "narrowed-divmod-loop"
    ExactFunction = "test_while_register_narrowed"
    RequireExact = $true
}
$narrowedDivmodIndex = 0
foreach ($mutation in $narrowedDivmodMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "nd$($narrowedDivmodIndex.ToString('000'))"
        Sources = @(Join-Path $repoRoot "tests/tdmfuse.c")
        Defines = @()
        Expected = @("checks=66 failures=0", "RESULT: PASS")
        Exit = 0
        RequiredGenericFunction = "test_while_register_narrowed"
        RequiredSelectorFunction = "test_while_register_narrowed"
        RequiredSelector = "spilled-scalar-cfg"
        MachineMutation = $mutation
        MachineMutationFunction = "test_while_register_narrowed"
    }
    ++$narrowedDivmodIndex
}

$minimaxMutations = @(
    "5:type:1", "7:src1:999", "7:src2:999", "7:immediate:999",
    "7:type:1", "6:type:1", "8:src1:999", "8:memory_size:3",
    "10:immediate:999", "40:immediate:999", "32:immediate:999",
    "36:immediate:999", "46:immediate:999", "58:immediate:999",
    "61:immediate:999", "67:immediate:999", "70:immediate:999",
    "90:immediate:999", "86:immediate:999", "16:src1:999",
    "16:src2:999", "16:immediate:999", "16:memory_size:3",
    "17:src1:999", "17:memory_size:3", "18:src1:999", "18:type:1",
    "33:src1:999", "37:src1:999", "47:src1:999", "59:src1:999",
    "62:src1:999", "68:src1:999", "71:src1:999", "76:src1:999",
    "82:src1:999", "82:src2:999", "82:type:1", "248:src1:999",
    "93:src1:999", "93:src2:999", "93:immediate:999",
    "93:memory_size:3", "94:src1:999", "94:memory_size:3", "94:type:1",
    "100:src1:999", "100:src2:999", "100:immediate:999",
    "100:memory_size:3", "102:src1:999", "102:src2:999",
    "102:memory_size:3", "120:src1:999", "120:src2:999",
    "120:immediate:999", "120:memory_size:3", "123:src1:999",
    "123:src2:999", "123:memory_size:3", "104:src1:999",
    "106:src1:999", "112:src1:999", "114:src1:999", "115:type:1",
    "117:src1:999", "143:src1:999", "143:src2:999", "147:src1:999",
    "157:src1:999", "167:src1:999", "177:src1:999", "199:src1:999",
    "199:src2:999", "203:src1:999", "213:src1:999", "223:src1:999",
    "233:src1:999", "252:src1:999"
)
$caseDefinitions += [pscustomobject]@{
    Name = "mmexact"
    Sources = @(Join-Path $repoRoot "tests/ttt.c")
    Defines = @()
    Expected = @("6493 moves", "1 iterations")
    Exit = 0
    ExactTemplate = "recursive-byte-minimax-schedule"
    ExactFunction = "MinMax"
    RequireExact = $true
}
$minimaxMutationIndex = 0
foreach ($mutation in $minimaxMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "mx$($minimaxMutationIndex.ToString('000'))"
        Sources = @(Join-Path $repoRoot "tests/ttt.c")
        Defines = @()
        Expected = @("6493 moves", "1 iterations")
        Exit = 0
        ExactTemplate = "recursive-byte-minimax-schedule"
        ExactFunction = "MinMax"
        RequireRejected = $true
        MachineMutation = $mutation
        MachineMutationFunction = "MinMax"
    }
    ++$minimaxMutationIndex
}

$catalanMutations = @(
    "11:type:1", "12:src1:999", "12:src2:999", "12:immediate:999",
    "12:memory_size:3", "14:type:1", "15:src1:999", "15:src2:999",
    "15:memory_size:3", "18:src1:999", "18:src2:999", "18:immediate:999",
    "18:memory_size:3", "21:src1:999", "21:src2:999", "21:memory_size:3",
    "29:src1:999", "31:src1:999", "31:src2:999", "36:immediate:999",
    "36:src1:999", "37:src1:999", "39:type:1", "41:immediate:999",
    "41:src1:999", "41:type:1", "42:immediate:999", "42:src1:999",
    "42:src2:999", "43:src1:999", "158:type:1", "165:immediate:999",
    "165:src1:999", "165:src2:999", "166:src1:999", "176:src1:999",
    "178:src1:999", "178:src2:999", "183:immediate:999", "183:src1:999",
    "184:src1:999", "188:immediate:999", "188:src1:999",
    "189:immediate:999", "189:src1:999", "189:src2:999", "190:src1:999",
    "312:immediate:999", "312:src1:999", "312:src2:999", "313:src1:999",
    "320:src1:999", "320:src2:999", "320:immediate:999", "321:src1:999",
    "321:memory_size:3", "321:type:1", "323:type:1", "333:src1:999",
    "337:src1:999", "355:immediate:999", "355:src1:999", "355:src2:999",
    "356:src1:999", "359:immediate:999", "359:src1:999", "359:src2:999",
    "360:src1:999", "373:immediate:999", "373:src1:999", "373:src2:999",
    "374:src1:999", "380:src1:999", "380:src2:999",
    "382:immediate:999", "382:src1:999", "382:src2:999", "383:src1:999",
    "386:immediate:999", "386:src1:999", "386:src2:999", "387:src1:999",
    "399:src1:999", "399:src2:999", "399:immediate:999", "400:src1:999",
    "400:memory_size:3", "402:immediate:999", "402:src1:999",
    "402:src2:999", "405:immediate:999", "405:src1:999", "405:src2:999",
    "406:immediate:999", "406:src1:999", "407:immediate:999",
    "407:src1:999", "407:src2:999", "407:type:1", "412:immediate:999",
    "412:src1:999", "412:src2:999", "414:src1:999",
    "417:immediate:999", "417:src1:999", "417:src2:999", "418:src1:999",
    "434:src1:999", "434:type:1", "436:src1:999"
)
$caseDefinitions += [pscustomobject]@{
    Name = "catexact"
    Sources = @(Join-Path $repoRoot "tests/catalan.c")
    Defines = @()
    Expected = @("0.9159655941772190150546035149323841107741493742816721342664981196217630197762547694793565129261151062")
    Exit = 0
    StackBytes = 768
    ExactTemplate = "catalan-driver-schedule"
    ExactFunction = "main"
    RequireExact = $true
}
$catalanMutationIndex = 0
foreach ($mutation in $catalanMutations) {
    $caseDefinitions += [pscustomobject]@{
        Name = "ct$($catalanMutationIndex.ToString('000'))"
        Sources = @(Join-Path $repoRoot "tests/catalan.c")
        Defines = @()
        Expected = @("0.9159655941772190150546035149323841107741493742816721342664981196217630197762547694793565129261151062")
        Exit = 0
        StackBytes = 768
        RequiredGenericFunction = "main"
        RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"
        MachineMutation = $mutation
        MachineMutationFunction = "main"
    }
    ++$catalanMutationIndex
}

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
        "catalanmut", "fuzz", "inlines", "lazywide", "minimax", "minimaxmut", "ndivmut", "oldloops", "pairedbytes",
        "vlaend", "vlaok")
    foreach ($requested in $Cases) {
        if ($requested -notin $knownCases) { throw "Unknown MIR clobber case: $requested" }
    }
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE" $null
    Set-ProcessEnvironment "DCC_MIR_MACHINE_MUTATE_FUNCTION" $null
    Assert-MachineMutationIgnored
    Assert-MachineMutationFailure "invalid" `
        "invalid DCC_MIR_MACHINE_MUTATE specification"
    Assert-MachineMutationFailure "-1:immediate:0" `
        "invalid DCC_MIR_MACHINE_MUTATE specification"
    Assert-MachineMutationFailure "x:immediate:0" `
        "invalid DCC_MIR_MACHINE_MUTATE specification"
    Assert-MachineMutationFailure "2147483648:immediate:0" `
        "invalid DCC_MIR_MACHINE_MUTATE specification"
    Assert-MachineMutationFailure "999:immediate:0" `
        "invalid DCC_MIR_MACHINE_MUTATE specification"
    Assert-MachineMutationFailure "1:type:not-a-number" `
        "invalid DCC_MIR_MACHINE_MUTATE value"
    Assert-MachineMutationFailure "1:immediate:999999999999999999999999" `
        "invalid DCC_MIR_MACHINE_MUTATE value"
    Assert-MachineMutationFailure "1:type:2147483648" `
        "DCC_MIR_MACHINE_MUTATE integer field is out of range"
    Assert-MachineMutationFailure "1:type:-2147483649" `
        "DCC_MIR_MACHINE_MUTATE integer field is out of range"
    if ($IsWindows) {
        Assert-MachineMutationFailure "1:immediate:2147483648" `
            "DCC_MIR_MACHINE_MUTATE value is out of range"
        Assert-MachineMutationFailure "1:immediate:-2147483649" `
            "DCC_MIR_MACHINE_MUTATE value is out of range"
    }
    Assert-MachineMutationFailure "1:unknown:0" `
        "unknown DCC_MIR_MACHINE_MUTATE field"
    Assert-MachineMutationFailure "3:identity:0" `
        "unknown DCC_MIR_MACHINE_MUTATE field"
    Assert-MachineMutationFailure "3:identity:256" `
        "unknown DCC_MIR_MACHINE_MUTATE field"
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
            -not (($case.Name -eq "catexact" -or $case.Name.StartsWith("ct")) -and
                "catalanmut" -in $Cases) -and
            -not ($case.Name.StartsWith("fuzz-") -and "fuzz" -in $Cases) -and
            -not (($case.Name -eq "mmexact" -or $case.Name.StartsWith("mx")) -and
                "minimaxmut" -in $Cases) -and
            -not ($case.Name.StartsWith("nd") -and "ndivmut" -in $Cases)) {
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
                    -FixturePaths $case.FixturePaths `
                    -AssemblyPatterns $case.AssemblyPatterns `
                    -OddUpperRuntime ([bool]$case.OddUpperRuntime) `
                    -StackBytes $(if ($case.StackBytes) {
                        [int]$case.StackBytes
                    } else { 512 }) `
                    -MachineMutation $case.MachineMutation `
                    -MachineMutationFunction $case.MachineMutationFunction
                foreach ($debugMode in $case.DebugModes) {
                    Assert-RunCase -Name $case.Name -Sources $case.Sources `
                        -Defines $case.Defines -Expected $case.Expected `
                        -ExpectedExit $case.Exit -StackCheck $stackCheck `
                        -Peep $peep -DebugMode $debugMode `
                        -RequiredGenericFunction $case.RequiredGenericFunction `
                        -RequiredSelectorFunction $case.RequiredSelectorFunction `
                        -RequiredSelector $case.RequiredSelector `
                        -RunArguments $case.Args `
                        -FixturePaths $case.FixturePaths
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
