#Requires -Version 7
param(
    [int]$RunTimeout = 30,
    [string]$Emulator = "ntvcm",
    [string[]]$Cases = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$dccmake = Join-Path $repoRoot "dccmake"
$emulator = (Get-Command $Emulator -ErrorAction Stop).Source
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dcc-mir-clobber-tests-" + [guid]::NewGuid())
$environmentNames = @(
    "DCC_MIR_COST_REPORT",
    "DCC_MIR_MACHINE_REPORT",
    "DCC_MIR_REPORT",
    "DCC_MIR_REQUIRE_COMPLETE",
    "DCC_MIR_REQUIRE_EMIT",
    "DCC_MIR_SELECT_CANDIDATE",
    "DCC_MIR_SELECT_FUNCTION",
    "DCC_MIR_SELECT_REPORT"
)
$savedEnvironment = @{}

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
    [string[]]$AssemblyPatterns = @(),
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

    $build = Invoke-WithTimeout $dccmake $arguments $repoRoot 60
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
        $selected = $assembly.Contains(";@dcc.mir exact-kernel") -or
            ($ExactFunction -and $build.Output -match $selectionPattern)
        if ($RequireExact -and -not $selected) {
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
    $run = Invoke-WithTimeout $emulator @(
        "-p", "-s:0", "$outputBase.COM"
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
}

$fixtureRoot = Join-Path $repoRoot "tests/mir-clobber"
$caseDefinitions = @(
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
        Name = "structv"
        Sources = @(Join-Path $repoRoot "tests/tstructi.c")
        Defines = @("MIR_CLOBBER_G_PAIR_A=30")
        Expected = @("global pair 30 1000 7 1037")
        Exit = 0
        ExactTemplate = "struct-init-reports"
        ExactFunction = "main"
    },
    [pscustomobject]@{
        Name = "stringv"
        Sources = @(Join-Path $repoRoot "tests/tstri2.c")
        Defines = @("MIR_CLOBBER_G_NAME_V=1001")
        Expected = @("global name 294 1001 120")
        Exit = 0
        ExactTemplate = "string-init-reports"
        ExactFunction = "main"
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
    },
    [pscustomobject]@{
        Name = "bitfield"
        Sources = @(Join-Path $repoRoot "tests/tbfinit.c")
        Defines = @("MIR_CLOBBER_GMIX_A=2")
        Expected = @("FAIL gmix.a got 2 expected 1")
        Exit = 0
        ExactTemplate = "bitfield-init-checks"
        ExactFunction = "main"
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
    }
)

try {
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_COMPLETE" "1"
    Set-ProcessEnvironment "DCC_MIR_REQUIRE_EMIT" "1"
    Set-ProcessEnvironment "DCC_MIR_MACHINE_REPORT" "1"
    Set-ProcessEnvironment "DCC_MIR_SELECT_REPORT" "1"

    if ($Cases.Count -eq 0 -or "semantics" -in $Cases) {
        Set-ProcessEnvironment "DCC_MIR_REPORT" "1"
        try {
            $proof = Invoke-WithTimeout (Join-Path $repoRoot "dcc") @(
                "-c", (Join-Path $fixtureRoot "semfix.c"),
                "-o", (Join-Path $tempRoot "SEMANTIC.MAC")
            ) $repoRoot 60
        } finally {
            [Environment]::SetEnvironmentVariable("DCC_MIR_REPORT",
                $savedEnvironment["DCC_MIR_REPORT"], "Process")
        }
        if ($proof.TimedOut -or $proof.ExitCode -ne 0) {
            throw "MIR semantics proof failed:`n$($proof.Output)"
        }
        foreach ($expectation in @(
            @{ Function = "vread"; Loads = 3; Volatile = 3 },
            @{ Function = "vword"; Loads = 2; Volatile = 2 },
            @{ Function = "nread"; Loads = 1; Volatile = 0 }
        )) {
            $function = $expectation.Function
            $body = [regex]::Match($proof.Output,
                "(?s); MIR function=$function .*?; MIR summary function=$function ")
            $loads = [regex]::Matches($body.Value, '\bloadind\b').Count
            if (-not $body.Success -or $loads -ne $expectation.Loads) {
                throw "$function has $loads MIR loads, expected " +
                    "$($expectation.Loads):`n$($body.Value)"
            }
            $volatileLoads = [regex]::Matches(
                $body.Value, '\bloadind\b[^\r\n]*\bmem=\d+v\b').Count
            if ($volatileLoads -ne $expectation.Volatile) {
                throw "$function has $volatileLoads volatile MIR loads, " +
                    "expected $($expectation.Volatile):`n$($body.Value)"
            }
        }
    }

    foreach ($case in $caseDefinitions) {
        if ($Cases.Count -gt 0 -and $case.Name -notin $Cases) {
            continue
        }
        foreach ($stackCheck in @($true, $false)) {
            foreach ($peep in @($true, $false)) {
                Assert-RunCase -Name $case.Name -Sources $case.Sources `
                    -Defines $case.Defines -Expected $case.Expected `
                    -ExpectedExit $case.Exit -StackCheck $stackCheck `
                    -Peep $peep -ExactTemplate $case.ExactTemplate `
                    -ExactFunction $case.ExactFunction `
                    -RequireExact ([bool]$case.RequireExact) `
                    -AssemblyPatterns $case.AssemblyPatterns `
                    -OddUpperRuntime ([bool]$case.OddUpperRuntime)
                foreach ($debugMode in $case.DebugModes) {
                    Assert-RunCase -Name $case.Name -Sources $case.Sources `
                        -Defines $case.Defines -Expected $case.Expected `
                        -ExpectedExit $case.Exit -StackCheck $stackCheck `
                        -Peep $peep -DebugMode $debugMode
                }
            }
        }
    }

    if ($Cases.Count -eq 0 -or "vlaend" -in $Cases) {
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
    Write-Host "MIR emission-clobber regressions passed" `
        -ForegroundColor Green
} finally {
    foreach ($name in $environmentNames) {
        Set-ProcessEnvironment $name $savedEnvironment[$name]
    }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}
