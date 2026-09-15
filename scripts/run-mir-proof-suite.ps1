#Requires -Version 7
<#
.SYNOPSIS
Run the complete AST/MIR correctness, mutation, release, and coverage gates.

.DESCRIPTION
Provides one phase-gated entry point for the independently maintained proof
layers. The default run builds the canonical tools, runs script and static
audits, normal and sanitized MIR host tests, debugger-host tests, isolated
compiler mutants, strict stack/no-stack release gates, the extended MIR census,
and the instrumented compiler-coverage workflow.

Artifacts are retained below a unique build/mir-proof-suite-* directory.

.PARAMETER Jobs
Maximum parallelism for ordinary builds, CTest, release tests, and coverage.

.PARAMETER MutationJobs
Number of isolated compiler-mutant workers. Defaults to Jobs divided by
MutationBuildJobs.

.PARAMETER MutationBuildJobs
Parallel build jobs available to each compiler-mutant worker.

.PARAMETER RunTimeout
Per-target timeout passed to the release suites.

.PARAMETER OutputDirectory
Artifact root. By default, a unique directory is created under build/.

.PARAMETER List
Print the ordered gate list without running commands.

.PARAMETER RequireComplete
Require exact AST/MIR coverage completion during the final coverage phase.

.PARAMETER All
Explicitly request the full proof suite. Accepted for discoverability; the
default run already executes every gate.

.EXAMPLE
pwsh ./scripts/run-mir-proof-suite.ps1

.EXAMPLE
pwsh ./scripts/run-mir-proof-suite.ps1 -All

.EXAMPLE
pwsh ./scripts/run-mir-proof-suite.ps1 -Jobs 24 -MutationJobs 6 -MutationBuildJobs 4 -RequireComplete
#>

[CmdletBinding()]
param(
    [ValidateRange(1, 1024)]
    [int]$Jobs = [Environment]::ProcessorCount,
    [ValidateRange(0, 1024)]
    [int]$MutationJobs = 0,
    [ValidateRange(1, 1024)]
    [int]$MutationBuildJobs = 4,
    [ValidateRange(1, 3600)]
    [int]$RunTimeout = 30,
    [string]$OutputDirectory = "",
    [switch]$List,
    [switch]$RequireComplete,
    [switch]$All
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$python = if (Get-Command python3 -ErrorAction SilentlyContinue) {
    "python3"
} else {
    "python"
}
$pwsh = (Get-Process -Id $PID).Path
if (-not $pwsh) {
    $pwsh = (Get-Command pwsh -ErrorAction Stop).Source
}
$cmake = "cmake"
$ctest = "ctest"
$sh = "sh"
$phaseNames = @(
    "canonical tool build",
    "script tests and static audits",
    "independent release CMake build",
    "normal MIR host tests",
    "ASan/UBSan MIR host tests",
    "debugger-host tests",
    "isolated compiler mutation campaign",
    "strict stack release suite",
    "strict no-stack release suite",
    "extended generated-MIR census",
    "instrumented compiler coverage and mutation campaigns"
)

if ($List) {
    for ($index = 0; $index -lt $phaseNames.Count; ++$index) {
        "{0}. {1}" -f ($index + 1), $phaseNames[$index]
    }
    exit 0
}

function Clear-AmbientProofControls {
    $preserved = @(
        "DCC_DIR", "DCC_HOME", "DCC_INCLUDE", "DCC_LIB", "DCC_RUNTIME",
        "DCC_PROCESS_SCOPE"
    )
    foreach ($item in @(Get-ChildItem Env:)) {
        if (($item.Name -eq "DCC" -or $item.Name -eq "DCCMAKE" -or
             $item.Name.StartsWith(
                 "DCC_", [System.StringComparison]::OrdinalIgnoreCase
             )) -and
            $item.Name -notin $preserved) {
            Remove-Item "Env:$($item.Name)"
        }
    }
}

function Assert-CommandAvailable {
    param([Parameter(Mandatory)][string]$Command)

    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Command"
    }
}

function Convert-ToShellPath {
    param([Parameter(Mandatory)][string]$Path)

    if (-not $IsWindows) {
        return $Path
    }
    $output = @(
        & $script:sh -lc 'cygpath -u -- "$1"' sh $Path 2>&1
    )
    $status = $LASTEXITCODE
    $converted = ($output -join "`n").Trim()
    if ($status -ne 0 -or -not $converted) {
        throw "Could not convert path for POSIX shell: $Path"
    }
    return $converted
}

function Resolve-AvailableClang {
    $unversioned = Get-Command "clang" -ErrorAction SilentlyContinue
    if ($unversioned) {
        return $unversioned.Source
    }
    $versioned = @(Get-Command "clang-*" -ErrorAction SilentlyContinue |
        Where-Object {
            [System.IO.Path]::GetFileNameWithoutExtension($_.Name) -match
                "^clang-\d+$"
        } |
        Sort-Object {
            $name = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
            [int]($name -replace "^clang-", "")
        } -Descending)
    if ($versioned.Count -gt 0) {
        return $versioned[0].Source
    }
    return ""
}

function Resolve-LlvmPeer {
    param(
        [Parameter(Mandatory)][string]$Compiler,
        [Parameter(Mandatory)][string]$Tool
    )

    $compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
    $compilerName = [System.IO.Path]::GetFileNameWithoutExtension($compilerPath)
    $suffix = if ($compilerName -match "^clang(?:-cl)?-(\d+)$") {
        "-$($Matches[1])"
    } else {
        ""
    }
    $extension = [System.IO.Path]::GetExtension($compilerPath)
    $directory = Split-Path -Parent $compilerPath
    foreach ($name in @("$Tool$suffix", $Tool)) {
        foreach ($candidate in @(
            (Join-Path $directory "$name$extension"),
            $name
        )) {
            $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
            if ($resolved) {
                return $resolved.Source
            }
        }
    }
    return ""
}

function Get-LlvmMajor {
    param([Parameter(Mandatory)][string]$Command)

    $output = @(& $Command "--version" 2>&1)
    $exitCode = $LASTEXITCODE
    $version = $output | Select-Object -First 1
    if ($exitCode -ne 0 -or "$version" -notmatch "\bversion\s+(\d+)") {
        throw "Could not determine LLVM version for $Command"
    }
    return [int]$Matches[1]
}

function Get-CommandVersionLine {
    param([Parameter(Mandatory)][string]$Command)

    $resolved = Get-Command $Command -ErrorAction SilentlyContinue
    if (-not $resolved) {
        return ""
    }
    $output = @(& $resolved.Source "--version" 2>&1)
    if ($LASTEXITCODE -ne 0 -or $output.Count -eq 0) {
        return ""
    }
    return "$($output | Select-Object -First 1)".Trim()
}

function Format-CoverageToolStatus {
    param(
        [Parameter(Mandatory)][string]$Label,
        [string]$Command,
        [Parameter(Mandatory)][string]$Expected
    )

    if (-not $Command) {
        return "  ${Label}: expected $Expected; found not resolved"
    }
    $resolved = Get-Command $Command -ErrorAction SilentlyContinue
    if (-not $resolved) {
        return "  ${Label}: expected $Expected; found $Command (not found)"
    }
    $versionLine = Get-CommandVersionLine $resolved.Source
    if ($versionLine) {
        return "  ${Label}: expected $Expected; found $($resolved.Source) [$versionLine]"
    }
    return "  ${Label}: expected $Expected; found $($resolved.Source) [version unavailable]"
}

function Throw-CoverageToolPreflight {
    param(
        [Parameter(Mandatory)][string]$Summary,
        [Parameter(Mandatory)][string[]]$StatusLines
    )

    throw ((@(
        $Summary,
        "Expected versus found:"
    ) + $StatusLines + @(
        "",
        "Set matching LLVM tools explicitly, for example:",
        "  export CC=/path/to/clang-18 LLVM_COV=/path/to/llvm-cov-18 LLVM_PROFDATA=/path/to/llvm-profdata-18"
    )) -join "`n")
}

foreach ($command in @(
    $python, $pwsh, $cmake, $ctest, $sh, "ntvcm"
)) {
    Assert-CommandAvailable $command
}
$coverageCompiler = if ($env:CC) {
    $env:CC
} else {
    Resolve-AvailableClang
}
if (-not $coverageCompiler) {
    Throw-CoverageToolPreflight `
        "Could not resolve a Clang compiler for the coverage phase." `
        @(
            (Format-CoverageToolStatus `
                "CC" $env:CC `
                "clang or clang-<major> on PATH, or CC=/path/to/clang-<major>")
        )
}
Assert-CommandAvailable $coverageCompiler
$coverageCompiler = (Get-Command $coverageCompiler -ErrorAction Stop).Source
$clangMajor = Get-LlvmMajor $coverageCompiler
$llvmCov = ""
if ($env:LLVM_COV) {
    $resolvedLlvmCov = Get-Command $env:LLVM_COV -ErrorAction SilentlyContinue
    if (-not $resolvedLlvmCov) {
        Throw-CoverageToolPreflight `
            "Could not resolve llvm-cov from LLVM_COV." `
            @(
                (Format-CoverageToolStatus `
                    "CC" $coverageCompiler "clang LLVM $clangMajor"),
                (Format-CoverageToolStatus `
                    "LLVM_COV" $env:LLVM_COV "llvm-cov LLVM $clangMajor")
            )
    }
    $llvmCov = $resolvedLlvmCov.Source
} else {
    $llvmCov = Resolve-LlvmPeer $coverageCompiler "llvm-cov"
}
$llvmProfdata = ""
if ($env:LLVM_PROFDATA) {
    $resolvedLlvmProfdata = Get-Command $env:LLVM_PROFDATA -ErrorAction SilentlyContinue
    if (-not $resolvedLlvmProfdata) {
        Throw-CoverageToolPreflight `
            "Could not resolve llvm-profdata from LLVM_PROFDATA." `
            @(
                (Format-CoverageToolStatus `
                    "CC" $coverageCompiler "clang LLVM $clangMajor"),
                (Format-CoverageToolStatus `
                    "LLVM_PROFDATA" $env:LLVM_PROFDATA "llvm-profdata LLVM $clangMajor")
            )
    }
    $llvmProfdata = $resolvedLlvmProfdata.Source
} else {
    $llvmProfdata = Resolve-LlvmPeer $coverageCompiler "llvm-profdata"
}
if (-not $llvmCov -or -not $llvmProfdata) {
    if (-not (Get-Command xcrun -ErrorAction SilentlyContinue)) {
        Throw-CoverageToolPreflight `
            "Could not resolve the LLVM coverage companions required for the final phase." `
            @(
                (Format-CoverageToolStatus "CC" $coverageCompiler "clang LLVM $clangMajor"),
                (Format-CoverageToolStatus "LLVM_COV" $llvmCov "llvm-cov LLVM $clangMajor"),
                (Format-CoverageToolStatus "LLVM_PROFDATA" $llvmProfdata "llvm-profdata LLVM $clangMajor")
            )
    }
} else {
    Assert-CommandAvailable $llvmCov
    Assert-CommandAvailable $llvmProfdata
    $covMajor = Get-LlvmMajor $llvmCov
    $profdataMajor = Get-LlvmMajor $llvmProfdata
    if ($clangMajor -ne $covMajor -or $clangMajor -ne $profdataMajor) {
        Throw-CoverageToolPreflight `
            "Coverage tools must all match Clang LLVM $clangMajor." `
            @(
                (Format-CoverageToolStatus "CC" $coverageCompiler "clang LLVM $clangMajor"),
                (Format-CoverageToolStatus "LLVM_COV" $llvmCov "llvm-cov LLVM $clangMajor"),
                (Format-CoverageToolStatus "LLVM_PROFDATA" $llvmProfdata "llvm-profdata LLVM $clangMajor")
            )
    }
}
Clear-AmbientProofControls

if ($MutationBuildJobs -gt $Jobs) {
    $MutationBuildJobs = $Jobs
}
if ($MutationJobs -eq 0) {
    $MutationJobs = [Math]::Max(
        1, [Math]::Floor($Jobs / $MutationBuildJobs))
}

if ($OutputDirectory) {
    $outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
        [System.IO.Path]::GetFullPath($OutputDirectory)
    } else {
        [System.IO.Path]::GetFullPath(
            (Join-Path $repoRoot $OutputDirectory))
    }
} else {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $outputRoot = Join-Path $repoRoot (
        "build/mir-proof-suite-$stamp-$PID")
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$suiteStartedAt = Get-Date
$script:phaseRecords = for ($index = 0; $index -lt $phaseNames.Count; ++$index) {
    [pscustomobject]@{
        Phase = $index + 1
        Name = $phaseNames[$index]
        StartedAt = $null
        CompletedAt = $null
    }
}

$script:phase = 0
function Get-ProofPhaseRecord {
    param([Parameter(Mandatory)][int]$Phase)

    return $script:phaseRecords[$Phase - 1]
}

function Start-ProofPhaseRecord {
    param(
        [Parameter(Mandatory)][int]$Phase,
        [datetime]$StartedAt = (Get-Date)
    )

    $record = Get-ProofPhaseRecord $Phase
    if (-not $record.StartedAt) {
        $record.StartedAt = $StartedAt
    }
}

function Complete-ProofPhaseRecord {
    param(
        [Parameter(Mandatory)][int]$Phase,
        [datetime]$CompletedAt = (Get-Date),
        [switch]$Overwrite
    )

    $record = Get-ProofPhaseRecord $Phase
    if (-not $record.StartedAt) {
        $record.StartedAt = $CompletedAt
    }
    if (-not $record.CompletedAt -or $Overwrite) {
        $record.CompletedAt = $CompletedAt
    }
}

function Start-ProofPhase {
    param([Parameter(Mandatory)][string]$Name)

    if ($script:phase -gt 0) {
        Complete-ProofPhaseRecord $script:phase
    }
    ++$script:phase
    Start-ProofPhaseRecord $script:phase
    Write-Host (
        "`n[{0}/{1}] {2}" -f
        $script:phase, $phaseNames.Count, $Name) -ForegroundColor Cyan
}

function Invoke-ProofCommand {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Command,
        [Parameter(Mandatory)][string[]]$Arguments,
        [hashtable]$Environment = @{}
    )

    Write-Host "  -> $Name"
    $saved = @{}
    try {
        foreach ($entry in $Environment.GetEnumerator()) {
            $saved[$entry.Key] =
                [Environment]::GetEnvironmentVariable(
                    $entry.Key, "Process")
            [Environment]::SetEnvironmentVariable(
                $entry.Key, [string]$entry.Value, "Process")
        }
        Push-Location $repoRoot
        try {
            & $Command @Arguments
            if ($LASTEXITCODE -ne 0) {
                throw "$Name failed with exit code $LASTEXITCODE"
            }
        } finally {
            Pop-Location
        }
    } finally {
        foreach ($entry in $saved.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable(
                $entry.Key, $entry.Value, "Process")
        }
    }
}

function Invoke-ProofTaskGroup {
    param(
        [Parameter(Mandatory)][object[]]$Tasks,
        [ValidateRange(1, 1024)][int]$MaxConcurrency = 1024
    )

    $logRoot = Join-Path $outputRoot "logs"
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    $worker = {
        param($RepoRoot, $Task, $LogPath)

        $ErrorActionPreference = "Stop"
        Set-Location $RepoRoot
        foreach ($command in $Task.Commands) {
            $saved = @{}
            try {
                foreach ($entry in $command.Environment.GetEnumerator()) {
                    $saved[$entry.Key] =
                        [Environment]::GetEnvironmentVariable(
                            $entry.Key, "Process")
                    [Environment]::SetEnvironmentVariable(
                        $entry.Key, [string]$entry.Value, "Process")
                }
                "-> $($command.Name)" | Out-File -FilePath $LogPath -Append
                $arguments = @($command.Arguments)
                & $command.Command @arguments *>> $LogPath
                if ($LASTEXITCODE -ne 0) {
                    return [pscustomobject]@{
                        Passed = $false
                        Detail = "$($command.Name) failed with exit code $LASTEXITCODE"
                    }
                }
            } catch {
                $_ | Out-String | Out-File -FilePath $LogPath -Append
                return [pscustomobject]@{
                    Passed = $false
                    Detail = "$($command.Name) failed: $($_.Exception.Message)"
                }
            } finally {
                foreach ($entry in $saved.GetEnumerator()) {
                    [Environment]::SetEnvironmentVariable(
                        $entry.Key, $entry.Value, "Process")
                }
            }
        }
        return [pscustomobject]@{ Passed = $true; Detail = "passed" }
    }

    $active = @()
    $nextTask = 0
    $failed = @()
    while ($active.Count -gt 0 -or
           ($failed.Count -eq 0 -and $nextTask -lt $Tasks.Count)) {
        while ($failed.Count -eq 0 -and $nextTask -lt $Tasks.Count -and
               $active.Count -lt $MaxConcurrency) {
            $task = $Tasks[$nextTask++]
            $safeName = $task.Name -replace "[^A-Za-z0-9_.-]", "-"
            $logPath = Join-Path $logRoot "$safeName.log"
            Start-ProofPhaseRecord $task.Phase
            Write-Host (
                "  -> start [{0}/{1}] {2}" -f
                $task.Phase, $phaseNames.Count, $task.Name)
            $job = Start-Job -ScriptBlock $worker -ArgumentList @(
                $repoRoot, $task, $logPath)
            $active += [pscustomobject]@{
                Job = $job
                Task = $task
                LogPath = $logPath
            }
        }

        if ($active.Count -eq 0) {
            break
        }
        $completedJob = Wait-Job -Job @($active.Job) -Any
        $item = $active | Where-Object { $_.Job.Id -eq $completedJob.Id } |
            Select-Object -First 1
        $result = @(Receive-Job -Job $completedJob)
        Remove-Job -Job $completedJob -Force
        $active = @($active | Where-Object {
            $_.Job.Id -ne $completedJob.Id
        })
        $outcome = $result | Where-Object {
            $_.PSObject.Properties.Name -contains "Passed"
        } | Select-Object -Last 1
        if ($null -eq $outcome -or -not $outcome.Passed) {
            $detail = if ($outcome) {
                $outcome.Detail
            } else {
                "worker exited without a result"
            }
            Write-Host (
                "  !! fail [{0}/{1}] {2}: {3}" -f
                $item.Task.Phase, $phaseNames.Count,
                $item.Task.Name, $detail) -ForegroundColor Red
            if (Test-Path $item.LogPath) {
                Get-Content -LiteralPath $item.LogPath
            }
            $failed += $item.Task.Name
        } else {
            Complete-ProofPhaseRecord $item.Task.Phase -Overwrite
            Write-Host (
                "  <- pass [{0}/{1}] {2}" -f
                $item.Task.Phase, $phaseNames.Count,
                $item.Task.Name) -ForegroundColor Green
        }
    }
    if ($failed.Count -gt 0) {
        throw "Parallel proof tasks failed: $($failed -join ', ')"
    }
}

$normalHost = Join-Path $outputRoot "mir-host"
$sanitizedHost = Join-Path $outputRoot "mir-host-sanitize"
$debugHost = Join-Path $outputRoot "debug-host"
$releaseCmake = Join-Path $outputRoot "cmake-release"
$mutationOutput = Join-Path $outputRoot "compiler-mutations"
$coverageOutput = Join-Path $outputRoot "compiler-coverage"

Start-ProofPhase $phaseNames[0]
Invoke-ProofCommand "build canonical tools" $pwsh @(
    "-NoLogo", "-NoProfile", "-File", "scripts/build-dcc.ps1"
)
Complete-ProofPhaseRecord 1

$preparationBuildJobs = [Math]::Max(
    1, [Math]::Floor($Jobs / [Math]::Min(4, $Jobs)))
Write-Host (
    "`n[2-6/$($phaseNames.Count)] concurrent preparation gates " +
    "(up to $preparationBuildJobs build jobs each)") -ForegroundColor Cyan
Invoke-ProofTaskGroup @(
    [pscustomobject]@{
        Phase = 2
        Name = "Python script tests"
        Commands = @([pscustomobject]@{
            Name = "Python script tests"
            Command = $python
            Arguments = @(
                "-m", "unittest", "discover", "-s", "scripts/tests",
                "-p", "test_*.py")
            Environment = @{}
        })
    },
    [pscustomobject]@{
        Phase = 2
        Name = "MIR fuzz generator proof"
        Commands = @([pscustomobject]@{
            Name = "MIR fuzz generator proof"
            Command = $pwsh
            Arguments = @(
                "-NoLogo", "-NoProfile", "-File",
                "scripts/test-mir-fuzz-source.ps1")
            Environment = @{}
        })
    },
    [pscustomobject]@{
        Phase = 2
        Name = "static and script audits"
        Commands = @(
            [pscustomobject]@{
                Name = "MSVC toolchain-detection test"
                Command = $pwsh
                Arguments = @(
                    "-NoLogo", "-NoProfile", "-File",
                    "scripts/tests/test_msvc_toolchain_detection.ps1")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "dccpeep tests"
                Command = $pwsh
                Arguments = @(
                    "-NoLogo", "-NoProfile", "-File",
                    "scripts/run-dccpeep-tests.ps1")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "IY runtime safety audit"
                Command = $python
                Arguments = @("scripts/rtl-iy-safety.py")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "runtime coverage audit"
                Command = $python
                Arguments = @("scripts/audit-runtime-coverage.py")
                Environment = @{}
            }
        )
    },
    [pscustomobject]@{
        Phase = 3
        Name = $phaseNames[2]
        Commands = @(
            [pscustomobject]@{
                Name = "configure independent release build"
                Command = $cmake
                Arguments = @(
                    "-S", "src/dcc", "-B", $releaseCmake,
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DDCC_RUNTIME_OUTPUT_DIRECTORY=$(Join-Path $releaseCmake 'bin')")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "build independent release compiler"
                Command = $cmake
                Arguments = @(
                    "--build", $releaseCmake, "--parallel",
                    "$preparationBuildJobs")
                Environment = @{}
            }
        )
    },
    [pscustomobject]@{
        Phase = 4
        Name = $phaseNames[3]
        Commands = @(
            [pscustomobject]@{
                Name = "configure normal MIR host tests"
                Command = $cmake
                Arguments = @(
                    "-S", "src/dcc", "-B", $normalHost,
                    "-DCMAKE_BUILD_TYPE=Debug", "-DDCC_BUILD_MIR_TESTS=ON",
                    "-DDCC_RUNTIME_OUTPUT_DIRECTORY=$(Join-Path $normalHost 'bin')")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "build normal MIR host tests"
                Command = $cmake
                Arguments = @(
                    "--build", $normalHost, "--parallel",
                    "$preparationBuildJobs")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "run normal MIR host tests"
                Command = $ctest
                Arguments = @(
                    "--test-dir", $normalHost, "--parallel",
                    "$preparationBuildJobs", "--output-on-failure")
                Environment = @{}
            }
        )
    },
    [pscustomobject]@{
        Phase = 5
        Name = $phaseNames[4]
        Commands = @(
            [pscustomobject]@{
                Name = "configure ASan/UBSan MIR host tests"
                Command = $cmake
                Arguments = @(
                    "-S", "src/dcc", "-B", $sanitizedHost,
                    "-DCMAKE_BUILD_TYPE=Debug", "-DDCC_BUILD_MIR_TESTS=ON",
                    "-DCMAKE_C_COMPILER=$coverageCompiler",
                    "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer",
                    "-DDCC_RUNTIME_OUTPUT_DIRECTORY=$(Join-Path $sanitizedHost 'bin')")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "build ASan/UBSan MIR host tests"
                Command = $cmake
                Arguments = @(
                    "--build", $sanitizedHost, "--parallel",
                    "$preparationBuildJobs")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "run ASan/UBSan MIR host tests"
                Command = $ctest
                Arguments = @(
                    "--test-dir", $sanitizedHost, "--parallel",
                    "$preparationBuildJobs", "--output-on-failure")
                Environment = @{
                    ASAN_OPTIONS = "detect_leaks=0"
                    UBSAN_OPTIONS = "halt_on_error=1"
                }
            }
        )
    },
    [pscustomobject]@{
        Phase = 6
        Name = $phaseNames[5]
        Commands = @(
            [pscustomobject]@{
                Name = "configure debugger-host tests"
                Command = $cmake
                Arguments = @(
                    "-S", "src/dcc_debug_host", "-B", $debugHost,
                    "-DBUILD_TESTING=ON", "-DCMAKE_BUILD_TYPE=Debug")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "build debugger-host tests"
                Command = $cmake
                Arguments = @(
                    "--build", $debugHost, "--parallel",
                    "$preparationBuildJobs")
                Environment = @{}
            },
            [pscustomobject]@{
                Name = "run debugger-host tests"
                Command = $ctest
                Arguments = @(
                    "--test-dir", $debugHost, "--parallel",
                    "$preparationBuildJobs", "--output-on-failure")
                Environment = @{}
            }
        )
    }
) -MaxConcurrency ([Math]::Min(6, $Jobs))
$script:phase = 6

Start-ProofPhase $phaseNames[6]
Invoke-ProofCommand "run isolated compiler mutants" $pwsh @(
    "-NoLogo", "-NoProfile", "-File",
    "scripts/run-mir-compiler-mutations.ps1",
    "-Jobs", "$MutationJobs",
    "-BuildJobs", "$MutationBuildJobs",
    "-OutputDirectory", $mutationOutput
)
Complete-ProofPhaseRecord 7

$strictMir = @{
    DCC_MIR_REQUIRE_COMPLETE = "1"
    DCC_MIR_REQUIRE_EMIT = "1"
}
$releaseJobs = [Math]::Max(1, [Math]::Floor($Jobs / 2))
Write-Host (
    "`n[8-9/$($phaseNames.Count)] concurrent strict release gates " +
    "($releaseJobs workers each)") -ForegroundColor Cyan
Invoke-ProofTaskGroup @(
    [pscustomobject]@{
        Phase = 8
        Name = $phaseNames[7]
        Commands = @([pscustomobject]@{
            Name = "run strict stack release suite"
            Command = $pwsh
            Arguments = @(
                "-NoLogo", "-NoProfile", "-File", "scripts/runall.ps1",
                "-Mode", "full", "-Extended",
                "-RunTimeout", "$RunTimeout", "-FailuresOnly",
                "-ThrottleLimit", "$releaseJobs")
            Environment = $strictMir
        })
    },
    [pscustomobject]@{
        Phase = 9
        Name = $phaseNames[8]
        Commands = @([pscustomobject]@{
            Name = "run strict no-stack release suite"
            Command = $pwsh
            Arguments = @(
                "-NoLogo", "-NoProfile", "-File", "scripts/runall.ps1",
                "-Mode", "full", "-Extended", "-NoStackCheck",
                "-RunTimeout", "$RunTimeout", "-FailuresOnly",
                "-ThrottleLimit", "$releaseJobs")
            Environment = $strictMir
        })
    }
) -MaxConcurrency ([Math]::Min(2, $Jobs))
$script:phase = 9

Start-ProofPhase $phaseNames[9]
Invoke-ProofCommand "run extended generated-MIR census" $python @(
    "scripts/mir-extended-census.py",
    "--mode", "both", "--require-complete",
    "--jobs", "$Jobs",
    "--output", (Join-Path $outputRoot "mir-extended.tsv")
)

Start-ProofPhase $phaseNames[10]
$coverageEnvironment = @{
    CC = Convert-ToShellPath $coverageCompiler
    DCC_COVERAGE_BUILD_DIR = Convert-ToShellPath $coverageOutput
    DCC_COVERAGE_JOBS = "$Jobs"
    DCC_COVERAGE_CLOBBER_JOBS = "$([Math]::Min(8, $Jobs))"
    DCC_COVERAGE_MUTATION_JOBS = "$Jobs"
    DCC_COVERAGE_CENSUS_JOBS = "$Jobs"
}
if ($llvmCov) {
    $coverageEnvironment.LLVM_COV = Convert-ToShellPath $llvmCov
}
if ($llvmProfdata) {
    $coverageEnvironment.LLVM_PROFDATA = Convert-ToShellPath $llvmProfdata
}
if ($RequireComplete) {
    $coverageEnvironment.DCC_COVERAGE_REQUIRE_COMPLETE = "1"
}
Invoke-ProofCommand "run instrumented coverage workflow" $sh @(
    "scripts/compiler-coverage.sh"
) $coverageEnvironment
Complete-ProofPhaseRecord 11

$suiteCompletedAt = Get-Date
$receiptPath = Join-Path $outputRoot "receipt.json"
$receipt = [ordered]@{
    startedAt = $suiteStartedAt.ToString("o")
    completedAt = $suiteCompletedAt.ToString("o")
    elapsedSeconds = [Math]::Round(
        ($suiteCompletedAt - $suiteStartedAt).TotalSeconds, 3)
    parameters = [ordered]@{
        Jobs = $Jobs
        MutationJobs = $MutationJobs
        MutationBuildJobs = $MutationBuildJobs
        RequireComplete = [bool]$RequireComplete
        All = [bool]$All
    }
    phases = @(
        foreach ($record in $script:phaseRecords) {
            [ordered]@{
                phase = $record.Phase
                name = $record.Name
                startedAt = if ($record.StartedAt) {
                    $record.StartedAt.ToString("o")
                } else {
                    $null
                }
                completedAt = if ($record.CompletedAt) {
                    $record.CompletedAt.ToString("o")
                } else {
                    $null
                }
            }
        }
    )
}
$receipt | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $receiptPath -Encoding utf8

Write-Host (
    "`nAll AST/MIR proof gates passed. Artifacts: $outputRoot"
) -ForegroundColor Green
