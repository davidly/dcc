#Requires -Version 7
<#
.SYNOPSIS
Build and run the extended c-testsuite single-exec corpus with dcc.

.DESCRIPTION
Discovers tests under tests/extended-tests/c-testsuite/tests/single-exec,
filters them by C standard tags, builds each source with dcc through the normal
dcc -> dccpeep -> M80 -> dccrtlstrip -> M80 -> L80 pipeline, runs the produced
.COM under ntvcm, and compares captured stdout+stderr with the matching
.expected file.

.PARAMETER C89
  Run tests tagged c89. This is the default when no standard flag is supplied.

.PARAMETER C99
  Run the C99 target-standard set. Per c-testsuite rules, c89 tests are also
  valid C99 tests, so this selects tests tagged c89 or c99.

.PARAMETER C11
  Run the C11 target-standard set. Per c-testsuite rules, c89 and c99 tests are
  also valid C11 tests, so this selects tests tagged c89, c99, or c11.

.PARAMETER All
    Run every imported single-exec test, including any case without a direct
    c89/c99/c11 tag.

.PARAMETER Test
  Optional specific test basename(s), such as 00001 or 00151, useful for
  smoke-testing the runner.

.PARAMETER Mode
  Which optimization pass(es) to build and verify (default: fast):
    fast   - optimized (runs dccpeep)
    nopeep - unoptimized (skips dccpeep)
    full   - builds and verifies both modes against the same expected output

.PARAMETER RunTimeout
    Per-test timeout, in seconds, for each build pass and each emulator run.

.PARAMETER SkipFile
    JSON file listing target-inapplicable extended tests to ignore.

.EXAMPLE
  pwsh ./scripts/runall-extended.ps1 -C89
  pwsh ./scripts/runall-extended.ps1 -C99 -Serial
  pwsh ./scripts/runall-extended.ps1 -C11 -Mode nopeep
  pwsh ./scripts/runall-extended.ps1 -C89 -Test 00001
#>

param(
    [string]$Emulator = "ntvcm",
    [switch]$NoStackCheck,
    [string]$BuildDir = "build/extended-tests",
    [string]$SuiteDir = "tests/extended-tests/c-testsuite/tests/single-exec",
    [ValidateSet("fast", "nopeep", "full")]
    [string]$Mode = "fast",
    [switch]$C89,
    [switch]$C99,
    [switch]$C11,
    [switch]$All,
    [string[]]$Test = @(),
    [switch]$Help,
    [int]$RunTimeout = 60,
    [string]$SkipFile = "tests/extended-tests/_extended_test_overrides.json",
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

foreach ($extraArg in $ExtraArgs) {
    if ($extraArg -match '^-mode=(fast|nopeep|full)$') {
        $Mode = $Matches[1]
    }
    elseif ($extraArg -match '^-mode=') {
        Write-Error "Invalid mode '$($extraArg.Substring(6))'. Valid modes are: fast, nopeep, full."
        exit 1
    }
    else {
        Write-Error "Unknown argument: $extraArg"
        exit 1
    }
}

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    return
}

$defaultSkipFile = "tests/extended-tests/_extended_test_overrides.json"
if ([string]::IsNullOrWhiteSpace($SkipFile)) { $SkipFile = $defaultSkipFile }

$ErrorActionPreference = "Stop"
$Parallel = -not $Serial
$StackCheck = -not $NoStackCheck

if (-not ($C89 -or $C99 -or $C11 -or $All)) {
    $C89 = $true
}

$requestedStandards = [System.Collections.Generic.List[string]]::new()
if ($All -or $C89) { $requestedStandards.Add("c89") }
if ($All -or $C99) { $requestedStandards.Add("c99") }
if ($All -or $C11) { $requestedStandards.Add("c11") }
$requestedStandards = @($requestedStandards | Select-Object -Unique)
$standardSummary = if ($All) { "all" } else { $requestedStandards -join ", " }

$requestedNames = @{}
foreach ($name in $Test) {
    if ($name) {
        $requestedNames[[System.IO.Path]::GetFileNameWithoutExtension($name).ToLowerInvariant()] = $true
    }
}

$requestedMode = $Mode
$requestedBuildDir = $BuildDir
$requestedEmulator = $Emulator
$buildScriptPath = Join-Path $PSScriptRoot "ma.ps1"
$repoRoot = (Get-Location).Path
$Mode = $requestedMode
$BuildDir = $requestedBuildDir
$Emulator = $requestedEmulator

$ignoredTests = @{}
$expectedExitCodes = @{}
$resolvedSkipFile = $null
foreach ($candidate in @($SkipFile, (Join-Path $repoRoot $SkipFile), (Join-Path $repoRoot $defaultSkipFile))) {
    if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        $resolvedSkipFile = (Resolve-Path -LiteralPath $candidate).ProviderPath
        break
    }
}
if ($resolvedSkipFile) {
    $skipConfig = Get-Content -LiteralPath $resolvedSkipFile -Raw | ConvertFrom-Json
    foreach ($skipEntry in @($skipConfig.tests)) {
        $skipName = if ($skipEntry.name) { $skipEntry.name.ToString() } else { "" }
        if ($skipName -and $skipEntry.ignore) {
            $ignoredTests[$skipName.ToLowerInvariant()] = if ($skipEntry.reason) { $skipEntry.reason.ToString() } else { "ignored" }
        }
        if ($skipName -and $null -ne $skipEntry.expected_exit_code) {
            $expectedExitCodes[$skipName.ToLowerInvariant()] = [int]$skipEntry.expected_exit_code
        }
    }
}
elseif ($SkipFile) {
    Write-Warning "Extended test skip file not found: $SkipFile"
}

if ($StackCheck) {
    $env:DCC_FORCE_STACK_CHECK = "1"
    Write-Host "--- stack-check: building every extended test with -fstack-check (default; use -NoStackCheck to disable) ---" -ForegroundColor Cyan
}
else {
    Remove-Item env:DCC_FORCE_STACK_CHECK -ErrorAction SilentlyContinue
    Write-Host "--- stack-check disabled (-NoStackCheck) ---" -ForegroundColor DarkGray
}

function Test-IsNtvcmEmulator {
    param([string]$Command)
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($Command)
    return ($leaf -ieq "ntvcm")
}

function Get-TestTags {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    return @(Get-Content -LiteralPath $Path |
        ForEach-Object { $_.Trim().ToLowerInvariant() } |
        Where-Object { $_ -and -not $_.StartsWith("#") })
}

function Test-MatchesRequestedStandard {
    param([string[]]$Tags, [string[]]$Standards)
    foreach ($standard in $Standards) {
        if ($standard -eq "c89" -and ($Tags -contains "c89")) { return $true }
        if ($standard -eq "c99" -and (($Tags -contains "c89") -or ($Tags -contains "c99"))) { return $true }
        if ($standard -eq "c11" -and (($Tags -contains "c89") -or ($Tags -contains "c99") -or ($Tags -contains "c11"))) { return $true }
    }
    return $false
}

function Normalize-TestOutput {
    param([AllowNull()][string]$Text)
    if ($null -eq $Text) { return "" }
    return (($Text -replace "`r`n", "`n") -replace "`r", "`n").TrimEnd("`n")
}

function Test-MatchesExpected {
    param([string]$Actual, [string]$Expected)
    return ((Normalize-TestOutput $Actual) -ceq (Normalize-TestOutput $Expected))
}

function Invoke-ProcessWithTimeout {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSeconds
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    foreach ($arg in $Arguments) {
        [void]$psi.ArgumentList.Add($arg)
    }
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false

    try {
        $process = [System.Diagnostics.Process]::Start($psi)
    }
    catch {
        return [pscustomobject]@{
            ExitCode = 1
            TimedOut = $false
            Output   = "ERROR starting $FilePath : $_"
        }
    }

    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = $false

    if ($TimeoutSeconds -gt 0) {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            try { $process.Kill($true) } catch { try { $process.Kill() } catch { } }
            $process.WaitForExit()
        }
    }
    else {
        $process.WaitForExit()
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $output = (($stdout + $stderr) -replace "`r`n", "`n") -replace "`r", "`n"

    return [pscustomobject]@{
        ExitCode = if ($timedOut) { -1 } else { $process.ExitCode }
        TimedOut = $timedOut
        Output   = $output
    }
}

function Invoke-ExtendedTest {
    param(
        [object]$Case,
        [string[]]$Modes,
        [string]$BuildDir,
        [string]$RepoRoot,
        [string]$BuildScriptPath,
        [string]$Emulator,
        [string[]]$EmulatorRunArgs,
        [int]$RunTimeout
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $casePassed = $true

    $caseBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $RepoRoot $BuildDir }

    if (-not (Test-Path $caseBuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $caseBuildDir -Force | Out-Null
    }

    $expected = ""
    if (Test-Path -LiteralPath $Case.ExpectedPath -PathType Leaf) {
        $expected = Get-Content -LiteralPath $Case.ExpectedPath -Raw
    }
    else {
        $lines.Add("    ERROR: no expected output at $($Case.ExpectedPath)")
        $casePassed = $false
    }

    foreach ($buildMode in $Modes) {
        if (-not $casePassed) { break }
        $displayMode = if ($buildMode -eq "peep") { "fast" } else { $buildMode }
        $buildArgs = @(
            "-NoProfile",
            "-File", $BuildScriptPath,
            $Case.Name,
            "-Mode", $buildMode,
            "-BuildDir", $caseBuildDir,
            "-Emulator", $Emulator,
            "-SourcePath", $Case.SourcePath
        )
        $buildResult = Invoke-ProcessWithTimeout -FilePath "pwsh" -Arguments $buildArgs -WorkingDirectory $RepoRoot -TimeoutSeconds $RunTimeout

        if ($buildResult.TimedOut) {
            $lines.Add("  Building $($Case.Name) ($displayMode)... TIMEOUT after ${RunTimeout}s")
            $casePassed = $false
            break
        }
        if ($buildResult.ExitCode -ne 0) {
            $lines.Add("  Building $($Case.Name) ($displayMode)... FAILED")
            foreach ($line in @($buildResult.Output -split "`n" | Where-Object { $_ } | Select-Object -First 20)) {
                $lines.Add("    BUILD> $line")
            }
            $casePassed = $false
            break
        }
        $lines.Add("  Building $($Case.Name) ($displayMode)... done")

        $upper = $Case.Name.ToUpperInvariant()
        $comFile = Join-Path $caseBuildDir "$upper.COM"
        if (-not (Test-Path -LiteralPath $comFile -PathType Leaf)) {
            $lines.Add("    ERROR: $comFile not found")
            $casePassed = $false
            continue
        }

        $nativeArgs = @($EmulatorRunArgs) + @("$upper.COM")
        $runResult = Invoke-ProcessWithTimeout -FilePath $Emulator -Arguments $nativeArgs -WorkingDirectory $caseBuildDir -TimeoutSeconds $RunTimeout
        $runExitCode = $runResult.ExitCode
        $actual = $runResult.Output

        if ($runResult.TimedOut) {
            $lines.Add("    TIMEOUT running $($Case.Name) after ${RunTimeout}s")
            $casePassed = $false
            continue
        }

        if ($runExitCode -ne $Case.ExpectedExitCode) {
            $lines.Add("    ERROR: emulator exited with code $runExitCode")
            $casePassed = $false
        }

        if (-not (Test-MatchesExpected -Actual $actual -Expected $expected)) {
            $lines.Add("    OUTPUT MISMATCH (vs $($Case.ExpectedPath))")
            $expLines = @((Normalize-TestOutput $expected) -split "`n")
            $actLines = @((Normalize-TestOutput $actual) -split "`n")
            $maxLen = [Math]::Max($expLines.Count, $actLines.Count)
            $shown = 0
            for ($i = 0; $i -lt $maxLen -and $shown -lt 40; $i++) {
                $e = if ($i -lt $expLines.Count) { $expLines[$i] } else { $null }
                $a = if ($i -lt $actLines.Count) { $actLines[$i] } else { $null }
                if ($null -eq $a) {
                    $lines.Add("    DIFF- $e")
                    $shown++
                }
                elseif ($null -eq $e) {
                    $lines.Add("    DIFF+ $a")
                    $shown++
                }
                elseif ($e -cne $a) {
                    $lines.Add("    DIFF- $e")
                    $lines.Add("    DIFF+ $a")
                    $shown += 2
                }
            }
            if ($shown -ge 40) { $lines.Add("    DIFF... truncated") }
            $casePassed = $false
        }
        else {
            $lines.Add("    Output matches expected")
        }
    }

    $sw.Stop()
    return [pscustomobject]@{
        Name    = $Case.Name
        Tags    = $Case.Tags
        Passed  = $casePassed
        Elapsed = $sw.Elapsed
        Lines   = $lines.ToArray()
    }
}

if (-not (Test-Path -LiteralPath $SuiteDir -PathType Container)) {
    Write-Error "Extended test suite not found: $SuiteDir"
    exit 1
}

$allCases = [System.Collections.Generic.List[object]]::new()
$skippedCases = [System.Collections.Generic.List[object]]::new()
foreach ($source in @(Get-ChildItem -LiteralPath $SuiteDir -Filter "*.c" -File | Sort-Object Name)) {
    $name = $source.BaseName
    if ($requestedNames.Count -gt 0 -and -not $requestedNames.ContainsKey($name.ToLowerInvariant())) { continue }

    $tagsPath = Join-Path $SuiteDir "$name.c.tags"
    $tags = @(Get-TestTags $tagsPath)
    if (-not $All -and -not (Test-MatchesRequestedStandard -Tags $tags -Standards $requestedStandards)) { continue }

    $ignoreKey = $name.ToLowerInvariant()
    if ($ignoredTests.ContainsKey($ignoreKey)) {
        $skippedCases.Add([pscustomobject]@{
            Name   = $name
            Tags   = ($tags -join ",")
            Reason = $ignoredTests[$ignoreKey]
        })
        continue
    }

    $allCases.Add([pscustomobject]@{
        Name         = $name
        SourcePath   = $source.FullName
        ExpectedPath = (Join-Path $SuiteDir "$name.c.expected")
        Tags         = ($tags -join ",")
        ExpectedExitCode = if ($expectedExitCodes.ContainsKey($ignoreKey)) { $expectedExitCodes[$ignoreKey] } else { 0 }
    })
}

if ($allCases.Count -eq 0) {
    if ($skippedCases.Count -gt 0) {
        Write-Host "No runnable extended tests matched standards [$standardSummary]; $($skippedCases.Count) matched test(s) were skipped." -ForegroundColor Yellow
        foreach ($case in $skippedCases) {
            Write-Host "  SKIP $($case.Name): $($case.Reason)" -ForegroundColor DarkYellow
        }
        exit 0
    }
    Write-Error "No extended tests matched standards [$standardSummary] in $SuiteDir"
    exit 1
}

$selectedCount = $allCases.Count + $skippedCases.Count

$modes = switch ($Mode) {
    "fast"   { @("peep") }
    "nopeep" { @("nopeep") }
    "full"   { @("peep", "nopeep") }
}

$optimisationSummary = switch ($Mode) {
    "fast"   { "fast" }
    "nopeep" { "nopeep" }
    "full"   { "full (fast + nopeep)" }
}

$emulatorRunArgs = @()
if (Test-IsNtvcmEmulator $Emulator) {
    $emulatorRunArgs = @("-s:0")
}

Write-Host "Found $selectedCount selected extended test(s)" -ForegroundColor Cyan
Write-Host "Runnable: $($allCases.Count)" -ForegroundColor Cyan
if ($skippedCases.Count -gt 0) {
    Write-Host "Skipped $($skippedCases.Count) target-inapplicable test(s)" -ForegroundColor Yellow
}
Write-Host "Standards: $standardSummary" -ForegroundColor Cyan
Write-Host "Mode: $optimisationSummary" -ForegroundColor Cyan
Write-Host "Per-test timeout: ${RunTimeout}s" -ForegroundColor Cyan
if ($ignoredTests.Count -gt 0) {
    Write-Host "Skip file: $resolvedSkipFile ($($ignoredTests.Count) ignored test(s))" -ForegroundColor Cyan
}
if ($Parallel) {
    Write-Host "(parallel, throttle = $ThrottleLimit)" -ForegroundColor Cyan
}

$suiteStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$results = @()

function Show-ExtendedResult {
    param($Result, [int]$Index, [int]$Total)
    $elapsed = $Result.Elapsed
    $elapsedStr = if ($elapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $elapsed } else { "{0:0.00}s" -f $elapsed.TotalSeconds }
    $counter = if ($Total -gt 0) { "[{0,3}/{1}]" -f $Index, $Total } else { "" }
    $status = if ($Result.Passed) { "PASS" } else { "FAIL" }
    $line = "{0} {1}  {2,-8} {3,8} | {4}" -f $counter, $status, $Result.Name, $elapsedStr, $Result.Tags
    Write-Host $line -ForegroundColor $(if ($Result.Passed) { "Green" } else { "Red" })
    if (-not $Result.Passed) {
        foreach ($detail in $Result.Lines) {
            if ($detail -match 'FAILED|MISMATCH|WARNING|ERROR|TIMEOUT|^    DIFF-|^    DIFF\+') {
                $color = if ($detail -match '^    DIFF\+') { "Green" } else { "Red" }
                Write-Host "        $($detail.Trim())" -ForegroundColor $color
            }
        }
    }
}

$totalToRun = $allCases.Count
if ($Parallel) {
    $repoRoot = (Get-Location).Path
    $normalizeDef = ${function:Normalize-TestOutput}.ToString()
    $matchDef = ${function:Test-MatchesExpected}.ToString()
    $processDef = ${function:Invoke-ProcessWithTimeout}.ToString()
    $invokeDef = ${function:Invoke-ExtendedTest}.ToString()
    $stackCheckOn = [bool]$StackCheck
    $runArgs = @($emulatorRunArgs)
    $maPath = $buildScriptPath

    $done = 0
    $allCases | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        $case = $_
        Set-Location $using:repoRoot
        if ($using:stackCheckOn) { $env:DCC_FORCE_STACK_CHECK = "1" }
        ${function:Normalize-TestOutput} = $using:normalizeDef
        ${function:Test-MatchesExpected} = $using:matchDef
        ${function:Invoke-ProcessWithTimeout} = $using:processDef
        ${function:Invoke-ExtendedTest} = $using:invokeDef
        $caseBuildDir = Join-Path $using:BuildDir $case.Name
        Invoke-ExtendedTest -Case $case -Modes $using:modes -BuildDir $caseBuildDir -RepoRoot $using:repoRoot `
            -BuildScriptPath $using:maPath -Emulator $using:Emulator -EmulatorRunArgs $using:runArgs `
            -RunTimeout $using:RunTimeout
    } | ForEach-Object {
        $result = $_
        $results += $result
        $done++
        Show-ExtendedResult -Result $result -Index $done -Total $totalToRun
    }
}
else {
    $done = 0
    foreach ($case in $allCases) {
        $done++
        $result = Invoke-ExtendedTest -Case $case -Modes $modes -BuildDir $BuildDir -RepoRoot $repoRoot `
            -BuildScriptPath $buildScriptPath -Emulator $Emulator -EmulatorRunArgs $emulatorRunArgs `
            -RunTimeout $RunTimeout
        $results += $result
        Show-ExtendedResult -Result $result -Index $done -Total $totalToRun
    }
}

if ($Parallel) {
    $failedForRetry = @($results | Where-Object { -not $_.Passed })
    if ($failedForRetry.Count -gt 0) {
        Write-Host ""
        Write-Host "Retrying $($failedForRetry.Count) failed extended test(s) serially to confirm failures..." -ForegroundColor Yellow
        foreach ($failedResult in $failedForRetry) {
            $case = @($allCases | Where-Object { $_.Name -eq $failedResult.Name } | Select-Object -First 1)
            if ($case.Count -eq 0) { continue }

            $retryBuildDir = Join-Path $BuildDir $case[0].Name
            $retryResult = Invoke-ExtendedTest -Case $case[0] -Modes $modes -BuildDir $retryBuildDir -RepoRoot $repoRoot `
                -BuildScriptPath $buildScriptPath -Emulator $Emulator -EmulatorRunArgs $emulatorRunArgs `
                -RunTimeout $RunTimeout

            $retryStatus = if ($retryResult.Passed) { "PASS" } else { "FAIL" }
            $retryColor = if ($retryResult.Passed) { "Green" } else { "Red" }
            Write-Host ("  RETRY {0}  {1,-8}" -f $retryStatus, $retryResult.Name) -ForegroundColor $retryColor
            if (-not $retryResult.Passed) {
                foreach ($detail in $retryResult.Lines) {
                    if ($detail -match 'FAILED|MISMATCH|WARNING|ERROR|TIMEOUT|^    DIFF-|^    DIFF\+') {
                        $color = if ($detail -match '^    DIFF\+') { "Green" } else { "Red" }
                        Write-Host "        $($detail.Trim())" -ForegroundColor $color
                    }
                }
            }

            for ($ri = 0; $ri -lt $results.Count; $ri++) {
                if ($results[$ri].Name -eq $retryResult.Name) {
                    $results[$ri] = $retryResult
                    break
                }
            }
        }
    }
}

$passed = 0
$failed = 0
$failedCases = @()
foreach ($result in $results) {
    if ($result.Passed) {
        $passed++
    }
    else {
        $failed++
        $failedCases += $result.Name
    }
}

$suiteStopwatch.Stop()
$suiteElapsed = $suiteStopwatch.Elapsed
$suiteElapsedStr = if ($suiteElapsed.TotalSeconds -ge 60) {
    "{0:m\m\ s\.f\s}" -f $suiteElapsed
}
else {
    "{0:0.00}s" -f $suiteElapsed.TotalSeconds
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "EXTENDED TEST SUITE SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Total tests:  $selectedCount"
Write-Host "  Runnable:     $($allCases.Count)"
Write-Host "  Passed:       $passed" -ForegroundColor Green
Write-Host "  Failed:       $failed" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
Write-Host "  Skipped:      $($skippedCases.Count)"
Write-Host "  Total time:   $suiteElapsedStr"
Write-Host "  Standards:    $standardSummary"
Write-Host "  Optimisation: $optimisationSummary"
Write-Host "  Timeout:      ${RunTimeout}s per build/run"

if ($failed -gt 0) {
    Write-Host ""
    Write-Host "Failed tests:" -ForegroundColor Red
    foreach ($name in $failedCases) {
        Write-Host "  - $name" -ForegroundColor Red
    }
}

if ($skippedCases.Count -gt 0) {
    Write-Host ""
    Write-Host "Skipped target-inapplicable tests:" -ForegroundColor Yellow
    foreach ($case in $skippedCases) {
        Write-Host "  - $($case.Name): $($case.Reason)" -ForegroundColor Yellow
    }
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host ">>> SUCCESS: All extended tests passed <<<" -ForegroundColor Green
    exit 0
}
else {
    Write-Host ">>> FAILURE: $failed extended test(s) failed <<<" -ForegroundColor Red
    exit 1
}