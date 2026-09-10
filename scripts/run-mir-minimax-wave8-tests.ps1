#Requires -Version 7
param(
    [ValidateRange(1, 2)][int]$Jobs = 1,
    [ValidateRange(1, 120)][int]$RunTimeout = 30,
    [string]$Emulator = "ntvcm"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$dccmake = Join-Path $repoRoot "dccmake"
$source = Join-Path $repoRoot "tests/mmw8.c"
$buildRoot = Join-Path $repoRoot "build/mir-minimax-wave8"
$expected =
    "MINIMAX-WAVE8 checks=50 failures=0 signature=1385923871 moves=6493"
$template = "recursive-byte-minimax-schedule"
$function = "MinMax"
$mutations = @(
    "13:src1:999",
    "25:src1:999",
    "30:src1:999",
    "44:src1:999",
    "55:src1:999",
    "89:src1:999",
    "97:src1:999",
    "128:src1:999",
    "131:src1:999",
    "136:src1:999",
    "144:src1:999",
    "154:src1:999",
    "159:src1:999",
    "165:src1:999",
    "174:src1:999",
    "187:src1:999",
    "192:src1:999",
    "200:src1:999",
    "210:src1:999",
    "215:src1:999",
    "221:src1:999",
    "230:src1:999",
    "14:type:1",
    "16:type:1",
    "17:type:1",
    "91:type:1",
    "93:type:1",
    "98:type:1",
    "100:type:1",
    "118:type:1",
    "120:type:1",
    "104:immediate:3",
    "114:immediate:0"
)

function Invoke-WithTimeout(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$WorkingDirectory,
    [int]$TimeoutSeconds,
    [hashtable]$Environment = @{}
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
    foreach ($entry in $Environment.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            $startInfo.Environment.Remove($entry.Key)
        } else {
            $startInfo.Environment[$entry.Key] = [string]$entry.Value
        }
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

function Assert-Configuration(
    [string]$Name,
    [bool]$StackCheck,
    [bool]$Peep,
    [string]$Mutation = "",
    [string]$DebugMode = "",
    [bool]$ExpectGeneric = $false
) {
    $configuration = @(
        if ($StackCheck) { "stack" } else { "nostack" }
        if ($Peep) { "peep" } else { "nopeep" }
        if ($DebugMode) { "debug-$DebugMode" }
    ) -join "-"
    $buildDir = Join-Path $buildRoot "$Name-$configuration"
    $outputBase = "MW8" + $script:caseIndex.ToString("D5")
    ++$script:caseIndex
    $arguments = @(
        "dcc-input=$source",
        "dcc-output=$outputBase",
        "dcc-build-dir=$buildDir",
        "dcc-peep=$Peep",
        "dcc-stack-check=$StackCheck",
        "dcc-stack-bytes=512"
    )
    if ($DebugMode) {
        $arguments += "dcc-debug=$DebugMode"
    }
    $environment = @{
        DCC_MIR_REQUIRE_COMPLETE = "1"
        DCC_MIR_REQUIRE_EMIT = "1"
        DCC_MIR_MACHINE_REPORT = "1"
        DCC_MIR_SELECT_REPORT = "1"
        DCC_MIR_MACHINE_MUTATE = if ($Mutation) { $Mutation } else { $null }
        DCC_MIR_MACHINE_MUTATE_FUNCTION =
            if ($Mutation) { $function } else { $null }
    }
    $build = Invoke-WithTimeout $dccmake $arguments $repoRoot 90 $environment
    if ($build.TimedOut -or $build.ExitCode -ne 0) {
        throw "$Name failed to build ($configuration):`n$($build.Output)"
    }

    $escapedFunction = [regex]::Escape($function)
    $escapedTemplate = [regex]::Escape($template)
    $accepted =
        $build.Output -match
        "MIR machine function=$escapedFunction template=$escapedTemplate accept=emitted"
    $scheduled =
        $build.Output -match
        "MIR selection function=$escapedFunction selector=scheduled-machine-cfg result=mir"
    if ($Mutation) {
        $rejected =
            $build.Output -match
            "MIR machine function=$escapedFunction template=$escapedTemplate reject="
        $generic =
            $build.Output -match
            ("MIR selection function=$escapedFunction selector=" +
             "(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|" +
             "regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir")
        if (-not $rejected -or $accepted -or $scheduled -or -not $generic) {
            throw "$Name did not reject into generated generic MIR " +
                "($configuration):`n$($build.Output)"
        }
    } elseif ($ExpectGeneric) {
        $generic =
            $build.Output -match
            ("MIR selection function=$escapedFunction selector=" +
             "(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|" +
             "regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir")
        if ($scheduled -or -not $generic) {
            throw "$Name did not use generated generic MIR " +
                "($configuration):`n$($build.Output)"
        }
    } elseif (-not $accepted -or -not $scheduled) {
        throw "$Name did not select the exact MinMax schedule " +
            "($configuration):`n$($build.Output)"
    }

    $run = Invoke-WithTimeout $Emulator @(
        "-p", "-s:0", "$outputBase.COM"
    ) $buildDir $RunTimeout
    if ($run.TimedOut -or $run.ExitCode -ne 0 -or
        $run.Output -notmatch [regex]::Escape($expected) -or
        $run.Output -match "(?m)^FAIL ") {
        throw "$Name failed its runtime oracle ($configuration):`n$($run.Output)"
    }
}

if ($Jobs -gt 1) {
    Write-Host "MinMax mutation cases are serialized to isolate compiler environment."
}
if (Test-Path -LiteralPath $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$script:caseIndex = 0
$count = 0

foreach ($stackCheck in @($true, $false)) {
    foreach ($peep in @($true, $false)) {
        Assert-Configuration "accept" $stackCheck $peep
        ++$count
        foreach ($mutation in $mutations) {
            $name = "reject-" + ($mutation -replace ":", "-")
            Assert-Configuration $name $stackCheck $peep $mutation
            ++$count
        }
    }
}
foreach ($debugMode in @("lines", "true")) {
    foreach ($stackCheck in @($true, $false)) {
        foreach ($peep in @($true, $false)) {
            Assert-Configuration "accept" $stackCheck $peep "" $debugMode `
                ($debugMode -eq "true")
            ++$count
        }
    }
}

Write-Host "MinMax wave-8 regressions passed $count target configurations"
