#Requires -Version 7
param(
    [int]$RunTimeout = 30,
    [string]$Emulator = "ntvcm",
    [string[]]$Cases = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$dccmake = Join-Path $repoRoot "dccmake"
$dccCommand = if ($env:DCC) { $env:DCC } else { Join-Path $repoRoot "dcc" }
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
