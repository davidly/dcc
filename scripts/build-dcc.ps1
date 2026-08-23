#Requires -Version 7
<#
.SYNOPSIS
Build dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c, dcc-debug-host, and its example I/O adapter on Windows, macOS, and Linux.

.DESCRIPTION
Compiles the C host tools with the native compiler for the current platform:
MSVC on Windows, clang on macOS, and gcc on Linux by default. The C++ debugger
host and example adapter shared library are built with CMake. Build artifacts
are placed under build/; all final tools and the example adapter are published
in the repository root.
On Linux, the C tools are linked -static by default (see -NoStatic); the CMake
debugger build uses its platform defaults. macOS has no static libSystem to
link against.

.PARAMETER OutputPath
  Output directory for build artifacts. Defaults to ./build.

.PARAMETER CC
  Override the C compiler used on macOS/Linux. Ignored on Windows, where MSVC
  cl.exe is used.

.PARAMETER NoStatic
  On Linux, link dcc/dccpeep/dccrtlstrip/dccmake dynamically instead of the
  default -static (useful if the static libc dev package, e.g. glibc-static
  on Fedora/RHEL, isn't installed). Ignored on macOS (no static linking
  there - Apple's libSystem has no static archive) and Windows.

.EXAMPLE
  pwsh ./scripts/build-dcc.ps1
  pwsh ./scripts/build-dcc.ps1 -OutputPath ./build-custom
  pwsh ./scripts/build-dcc.ps1 -CC clang
  pwsh ./scripts/build-dcc.ps1 -NoStatic
#>

param(
    [string]$OutputPath = "build",
    [string]$CC,
    [switch]$VerboseCommands,
    [switch]$NoStatic
)

$ErrorActionPreference = "Stop"

$script:VerboseCommands = [bool]$VerboseCommands

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $outputRoot = [System.IO.Path]::GetFullPath($OutputPath)
} else {
    $outputRoot = Join-Path $repoRoot $OutputPath
}

if (-not (Test-Path $outputRoot)) {
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
}

$outputPathDisplay = Resolve-Path -Path $outputRoot -Relative

function New-BuildDirectory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description,
        [switch]$QuietOutput
    )

    if ($script:VerboseCommands) {
        Write-Host ($FilePath + " " + ($Arguments -join " "))
    } elseif ($Description) {
        Write-Host "  $Description"
    }

    if ($QuietOutput -and -not $script:VerboseCommands) {
        $commandOutput = @(& $FilePath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            $commandOutput | ForEach-Object { Write-Host $_ }
        }
    } else {
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        $exitCode = $LASTEXITCODE
    }
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode"
    }
}

function Get-WindowsBuildToolsHelp {
        $toolchain = Get-MsvcToolchain
        @"
Could not find MSVC build tools.

Install one of these, then rerun this script:
    $($toolchain.InstallCommand)
    winget install --id Microsoft.VisualStudio.Community -e

If Visual Studio is already installed, open Visual Studio Installer and add:
    Desktop development with C++

This script looks for $($toolchain.VcVars) and requires the $($toolchain.Description).
"@
}

function Get-UnixBuildToolsHelp {
        param([string]$Compiler)

        if ($IsMacOS) {
                return @"
C compiler '$Compiler' was not found.

Install Apple's Command Line Tools, then rerun this script:
    xcode-select --install

After installation, verify the compiler is available:
    clang --version

You can also pass a compiler explicitly:
    pwsh ./scripts/build-dcc.ps1 -CC clang
"@
        }

        return @"
C compiler '$Compiler' was not found.

Install a C build toolchain, then rerun this script. Common Linux commands:
    Debian/Ubuntu: sudo apt update && sudo apt install build-essential
    Fedora:        sudo dnf groupinstall "Development Tools"
    RHEL/CentOS:   sudo dnf groupinstall "Development Tools"
    Arch:          sudo pacman -S base-devel
    openSUSE:      sudo zypper install -t pattern devel_C_C++
    Alpine:        sudo apk add build-base

After installation, verify the compiler is available:
    gcc --version

You can also pass a compiler explicitly:
    pwsh ./scripts/build-dcc.ps1 -CC clang
"@
}

function Get-MsvcToolchain {
    $isWindowsArm64 = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq [System.Runtime.InteropServices.Architecture]::Arm64
    if ($isWindowsArm64) {
        return [pscustomobject]@{
            Component = "Microsoft.VisualStudio.Component.VC.Tools.ARM64"
            VcVars = "vcvarsarm64.bat"
            Description = "MSVC ARM64 C++ toolchain"
            InstallCommand = 'winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --includeRecommended --quiet --wait"'
        }
    }

    return [pscustomobject]@{
        Component = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
        VcVars = "vcvars64.bat"
        Description = "MSVC x64 C++ toolchain"
        InstallCommand = 'winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait"'
    }
}

function Get-MsvcVarsPath {
    $toolchain = Get-MsvcToolchain
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires $toolchain.Component -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "VC\Auxiliary\Build\$($toolchain.VcVars)"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $versions = @("18", "2022", "2019", "17")
    $editions = @("Community", "Professional", "Enterprise", "BuildTools")
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }

    foreach ($root in $roots) {
        foreach ($version in $versions) {
            foreach ($edition in $editions) {
                $candidate = Join-Path $root "Microsoft Visual Studio\$version\$edition\VC\Auxiliary\Build\$($toolchain.VcVars)"
                if (Test-Path $candidate) {
                    return $candidate
                }
            }
        }
    }

    return $null
}

function Initialize-Msvc {
    $toolchain = Get-MsvcToolchain
    $vcvars = Get-MsvcVarsPath
    if (-not $vcvars) {
        throw (Get-WindowsBuildToolsHelp)
    }

    Write-Host "Found $($toolchain.Description): $vcvars"
    $envBlock = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $envBlock) {
        if ($line -match "^([^=]+)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    # cl.exe with no input files prints its "usage:" complaint to stderr
    # *before* its version/copyright banner to stdout (confirmed: that's
    # cl's own output order, not a stream-merging artifact), so grabbing
    # just the first line of merged output shows the usage line instead of
    # the version banner this is meant to display. Pick out the banner line
    # specifically instead of relying on line position.
    $clOutput = cl 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC cl.exe not found in PATH after vcvars setup."
    }
    $clVersion = ($clOutput | Where-Object { $_ -match "Compiler Version" } | Select-Object -First 1)
    if (-not $clVersion) {
        $clVersion = $clOutput | Select-Object -First 1
    }
    Write-Host "MSVC cl.exe: $clVersion"
}

function Remove-MisplacedArtifacts {
    Write-Host "Cleaning previous misplaced build artifacts..."

    $patterns = if ($IsWindows) {
        @("*.obj", "*.cod", "*.pdb", "*.ilk", "*.asm")
    } else {
        @("*.o")
    }

    foreach ($pattern in $patterns) {
        Get-ChildItem -Path $repoRoot -Filter $pattern -File -ErrorAction SilentlyContinue | Remove-Item -Force
        Get-ChildItem -Path (Join-Path $repoRoot "src") -Filter $pattern -File -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force
    }
}

function Get-BuildTargets {
    param([string]$SrcRoot)

    return @(
        @{ Name = "dcc";         SourceDir = (Join-Path $SrcRoot "dcc") },
        @{ Name = "dccpeep";     SourceDir = (Join-Path $SrcRoot "dccpeep") },
        @{ Name = "dccrtlstrip"; SourceDir = (Join-Path $SrcRoot "dccrtlstrip") },
        @{ Name = "dccmake";     SourceDir = (Join-Path $SrcRoot "dccmake") },
        @{ Name = "m80c";        SourceDir = (Join-Path $SrcRoot "m80c") },
        @{ Name = "l80c";        SourceDir = (Join-Path $SrcRoot "l80c") }
    )
}

# Runs a batch of independent work items (one external-process invocation
# each) across parallel runspaces, throttled to the machine's processor
# count, then replays each item's output afterward in a stable order -- so
# concurrent processes never interleave their output on the console, but
# still all actually run at once. $ScriptBlock receives one item via $_ (as
# usual for ForEach-Object) and must return a [PSCustomObject] with at least
# Name/ExitCode/Output; GroupKey/SortKey are used only for result ordering.
function Invoke-ParallelBatch {
    param(
        [Parameter(Mandatory)] [array]$Items,
        [Parameter(Mandatory)] [scriptblock]$ScriptBlock,
        [string]$GroupKey,
        [string]$SortKey = "Name"
    )

    $throttle = [Environment]::ProcessorCount
    $results = $Items | ForEach-Object -ThrottleLimit $throttle -Parallel $ScriptBlock

    $ordered = if ($GroupKey) {
        $results | Group-Object $GroupKey | ForEach-Object { $_.Group | Sort-Object $SortKey }
    } else {
        $results | Sort-Object $SortKey
    }

    foreach ($result in $ordered) {
        Write-Host "  $($result.Name)"
        if ($result.Output) {
            Write-Host $result.Output.TrimEnd()
        }
        if ($result.ExitCode -ne 0) {
            throw "$($result.Name) failed with exit code $($result.ExitCode)"
        }
    }

    return $ordered
}

# Builds dcc, dccpeep, dccrtlstrip, dccmake, m80c, and l80c together instead
# of one at a time: every source file across every target is compiled in one
# flat parallel wave (so dcc's many files compile alongside dccpeep's, which
# compile alongside the single-file tools, all sharing the machine's cores),
# then all six targets are linked in a second parallel wave. This replaces
# the previous approach of one `cl` invocation per target, run sequentially,
# with all of a target's own sources passed to that single invocation --
# which only ever used one core at a time, six times over.
#
# Splitting compile and link into two separate cl.exe invocations (needed to
# flatten compilation across targets) loses two things cl normally infers
# automatically when compiling and linking in one combined invocation, so
# they're restored explicitly in $linkerFlags below: /LTCG (link-time
# codegen for the /GL-compiled objects) and /DEBUG (linker-generated PDB for
# the /Zi debug info already embedded in those objects). /FS makes /Zi safe
# when multiple parallel cl.exe processes write into one target's shared PDB.
function Build-WindowsMsvc {
    Initialize-Msvc
    Remove-MisplacedArtifacts

    $cflags = @(
        "/nologo",
        "/GS-",
        "/GL",
        "/Oti2",
        "/Ob3",
        "/Qpar",
        "/FAsc",
        "/Zi",
        "/FS",
        "/std:c11"
    )
    $linkerFlags = @("user32.lib", "ntdll.lib", "/OPT:REF", "/LTCG", "/DEBUG")

    $targets = Get-BuildTargets (Join-Path $repoRoot "src")
    foreach ($target in $targets) {
        $target.ObjDir = Join-Path $outputRoot $target.Name
        $target.Out = Join-Path $repoRoot "$($target.Name).exe"
        $target.Sources = @(Get-ChildItem -Path $target.SourceDir -Filter "*.c" | Sort-Object Name)
        New-BuildDirectory $target.ObjDir
    }

    Write-Host "`n=== Compiling dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="
    $units = foreach ($target in $targets) {
        foreach ($source in $target.Sources) {
            [PSCustomObject]@{
                Target = $target.Name
                Name   = "$($target.Name)/$($source.Name)"
                Object = Join-Path $target.ObjDir ([System.IO.Path]::ChangeExtension($source.Name, ".obj"))
                Args   = @($cflags) + @(
                    "/I$($target.SourceDir)",
                    "/c", $source.FullName,
                    "/Fo:$(Join-Path $target.ObjDir ([System.IO.Path]::ChangeExtension($source.Name, '.obj')))",
                    "/Fa$($target.ObjDir)\",
                    "/Fd:$($target.ObjDir)\$($target.Name).pdb"
                )
            }
        }
    }

    $compileResults = Invoke-ParallelBatch -Items $units -GroupKey "Target" -ScriptBlock {
        $unit = $_
        $output = & cl @($unit.Args) 2>&1
        [PSCustomObject]@{ Target = $unit.Target; Name = $unit.Name; Object = $unit.Object; ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    }

    foreach ($target in $targets) {
        $target.Objects = @($compileResults | Where-Object Target -eq $target.Name | ForEach-Object Object)
    }

    Write-Host "`n=== Linking dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="
    Invoke-ParallelBatch -Items $targets -ScriptBlock {
        $target = $_
        $linkerFlags = $using:linkerFlags
        $arguments = @("/nologo") + @($target.Objects) + @("/Fe:$($target.Out)", "/link") + $linkerFlags + @("/PDB:$($target.ObjDir)\$($target.Name)-link.pdb")
        $output = & cl @arguments 2>&1
        [PSCustomObject]@{ Name = $target.Name; ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    } | Out-Null

    return @($targets | ForEach-Object Out)
}

function Get-UnixCompiler {
    if ($CC) {
        return $CC
    }
    if ($env:CC) {
        return $env:CC
    }
    if ($IsMacOS) {
        return "clang"
    }
    return "gcc"
}

# Same idea as Build-WindowsMsvc above: compile every source file across all
# six targets in one flat parallel wave, then link all six targets in a
# second parallel wave, instead of building one target fully before starting
# the next.
function Build-UnixNative {
    Remove-MisplacedArtifacts

    $compiler = Get-UnixCompiler
    $compilerCommand = Get-Command $compiler -ErrorAction SilentlyContinue
    if (-not $compilerCommand) {
        throw (Get-UnixBuildToolsHelp $compiler)
    }

    $compilerVersionOutput = & $compiler --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw (Get-UnixBuildToolsHelp $compiler)
    }
    $compilerVersion = $compilerVersionOutput | Select-Object -First 1
    Write-Host "C compiler: $compilerVersion"

    $baseCflags = if ($env:CFLAGS) {
        @($env:CFLAGS -split "\s+" | Where-Object { $_ })
    } else {
        # Host build tools run on the development machine, not the Z80 target.
        # Use the same portable C11 baseline as the Windows/MSVC build.
        # -w suppresses compiler warnings for a quiet default build. -g
        # matches the Windows path's /Zi: debug symbols by default even in
        # an optimized build.
        @("-std=c11", "-w", "-O2", "-g")
    }
    if ($IsMacOS -and ($baseCflags -notcontains "-fno-common")) {
        $baseCflags += "-fno-common"
    }
    # gcc + glibc on Linux emit warnings that clang (macOS) and MSVC (Windows)
    # do not: a gcc-only misleading-indentation warning on intentional
    # "if (cond) continue; next;" one-liners, plus conservative _FORTIFY_SOURCE
    # heuristics under -O2. Silence those toolchain-specific diagnostics here.
    $compilerIsGcc = ($compilerVersionOutput -join "`n") -match "(?im)^.*\bgcc\b"
    if ($compilerIsGcc -and -not $env:CFLAGS) {
        $baseCflags += @(
            "-Wno-misleading-indentation",
            "-Wno-format-overflow",
            "-Wno-stringop-truncation"
        )
    }

    # These are host build tools, not the Z80 target, so static linking is
    # purely about making the resulting binaries easy to copy/run on a
    # different Linux box without matching the exact glibc version - not
    # something Apple's libSystem supports (there is no libSystem.a), so this
    # only applies on Linux, and only if -NoStatic wasn't passed (e.g.
    # because the static libc dev package isn't installed).
    $linkFlags = @()
    if ($IsLinux -and -not $NoStatic) {
        $linkFlags = @("-static")
    }

    $targets = Get-BuildTargets (Join-Path $repoRoot "src")
    foreach ($target in $targets) {
        $target.ObjDir = Join-Path $outputRoot $target.Name
        $target.Out = Join-Path $repoRoot $target.Name
        $target.Sources = @(Get-ChildItem -Path $target.SourceDir -Filter "*.c" | Sort-Object Name)
        New-BuildDirectory $target.ObjDir
    }

    Write-Host "`n=== Compiling dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="
    $units = foreach ($target in $targets) {
        foreach ($source in $target.Sources) {
            $object = Join-Path $target.ObjDir ([System.IO.Path]::ChangeExtension($source.Name, ".o"))
            [PSCustomObject]@{
                Target = $target.Name
                Name   = "$($target.Name)/$($source.Name)"
                Object = $object
                Args   = @($baseCflags) + @("-I", $target.SourceDir, "-c", $source.FullName, "-o", $object)
            }
        }
    }

    $compileResults = Invoke-ParallelBatch -Items $units -GroupKey "Target" -ScriptBlock {
        $unit = $_
        $compiler = $using:compiler
        $output = & $compiler @($unit.Args) 2>&1
        [PSCustomObject]@{ Target = $unit.Target; Name = $unit.Name; Object = $unit.Object; ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    }

    foreach ($target in $targets) {
        $target.Objects = @($compileResults | Where-Object Target -eq $target.Name | ForEach-Object Object)
    }

    Write-Host "`n=== Linking dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="
    Invoke-ParallelBatch -Items $targets -ScriptBlock {
        $target = $_
        $compiler = $using:compiler
        $baseCflags = $using:baseCflags
        $linkFlags = $using:linkFlags
        $arguments = @($baseCflags) + @($target.Objects) + @($linkFlags) + @("-o", $target.Out)
        $output = & $compiler @arguments 2>&1
        [PSCustomObject]@{ Name = $target.Name; ExitCode = $LASTEXITCODE; Output = ($output | Out-String) }
    } | Out-Null

    return @($targets | ForEach-Object Out)
}

function Build-DccDebugHost {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) {
        $hint = if ($IsMacOS) {
            "Install it with: brew install cmake"
        } elseif ($IsLinux) {
            "Install it with: sudo apt-get install -y cmake  (or your distro's equivalent, e.g. dnf/yum/pacman)"
        } elseif ($IsWindows) {
            "Install it with: winget install Kitware.CMake  (or see https://cmake.org/download/)"
        } else {
            "See https://cmake.org/download/ for install instructions."
        }
        throw "CMake is required to build dcc-debug-host. $hint"
    }

    $sourceDir = Join-Path $repoRoot "src\dcc_debug_host"
    $buildDir = Join-Path $outputRoot "dcc_debug_host"
    New-BuildDirectory $buildDir

    $executableName = if ($IsWindows) { "dcc-debug-host.exe" } else { "dcc-debug-host" }
    $adapterName = if ($IsWindows) {
        "dcc-debug-io-adapter-example.dll"
    } elseif ($IsMacOS) {
        "libdcc-debug-io-adapter-example.dylib"
    } else {
        "libdcc-debug-io-adapter-example.so"
    }
    $debugHostOut = Join-Path $repoRoot $executableName
    $adapterOut = Join-Path $repoRoot $adapterName
    Remove-Item -LiteralPath $debugHostOut, $adapterOut -Force -ErrorAction SilentlyContinue

    Write-Host "`n=== Building dcc-debug-host and example I/O adapter ==="
    $configureArguments = @(
        "-S", $sourceDir,
        "-B", $buildDir,
        "-DBUILD_TESTING=OFF",
        "-DDCC_DEBUG_HOST_BUILD_EXAMPLES=ON",
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=$repoRoot",
        "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=$repoRoot"
    )
    if (-not $IsWindows) {
        $configureArguments += "-DCMAKE_BUILD_TYPE=Release"
    }
    Invoke-Checked $cmakeCommand.Source $configureArguments "debugger host configuration" -QuietOutput
    Invoke-Checked $cmakeCommand.Source @(
        "--build", $buildDir,
        "--config", "Release",
        "--target", "dcc-debug-host", "dcc-debug-io-adapter-example"
    ) "debugger host and example adapter compilation" -QuietOutput

    if (-not (Test-Path $debugHostOut) -or -not (Test-Path $adapterOut)) {
        throw "debugger host artifacts were not built in the repository root"
    }
    return @($debugHostOut, $adapterOut)
}

Write-Host "Build artifacts will go to: $outputPathDisplay"
Write-Host "Commands will be placed in: $repoRoot"

$executables = if ($IsWindows) {
    Build-WindowsMsvc
} else {
    Build-UnixNative
}
$executables = @($executables)
$executables += Build-DccDebugHost

Write-Host "`n=== Build complete ==="
Write-Host "Root outputs:"
foreach ($executable in $executables) {
    Write-Host "  $executable"
}
Write-Host "Intermediate build artifacts: $outputPathDisplay"
