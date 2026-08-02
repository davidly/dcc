#Requires -Version 7
<#
.SYNOPSIS
Build dcc, dccpeep, dccrtlstrip, dccmake, m80c, and l80c on Windows, macOS, and Linux.

.DESCRIPTION
Compiles the host tools with the native compiler for the current platform:
MSVC on Windows, clang on macOS, and gcc on Linux by default. Build artifacts
are placed under build/; final commands are placed in the repository root.
On Linux, these tools are linked -static by default (see -NoStatic);
macOS has no static libSystem to link against, so this never applies there.

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
        [string]$Description
    )

    if ($script:VerboseCommands) {
        Write-Host ($FilePath + " " + ($Arguments -join " "))
    } elseif ($Description) {
        Write-Host "  $Description"
    }
    & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
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

    $clVersion = cl 2>&1 | Select-Object -First 1
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC cl.exe not found in PATH after vcvars setup."
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
        "/std:c11"
    )
    $linkerFlags = @("user32.lib", "ntdll.lib", "/OPT:REF")

    $dccOut = Join-Path $repoRoot "dcc.exe"
    $dccObjDir = Join-Path $outputRoot "dcc"
    New-BuildDirectory $dccObjDir

    Write-Host "`n=== Building dcc compiler ==="
    Push-Location (Join-Path $repoRoot "src\dcc")
    try {
        $sources = Get-ChildItem -Path . -Filter "*.c" | ForEach-Object { $_.Name }
        $arguments = @($cflags) + @(
            "/I.",
            "/Fo:$dccObjDir\",
            "/Fa$dccObjDir\",
            "/Fd:$dccObjDir\dcc.pdb",
            "/Fe:$dccOut"
        ) + $sources + @("/link") + $linkerFlags + @("/PDB:$dccObjDir\dcc-link.pdb")
        Invoke-Checked "cl" $arguments "dcc compilation"
    } finally {
        Pop-Location
    }

    $tools = @(
        @{ Name = "dccpeep"; Sources = @(Get-ChildItem (Join-Path $repoRoot "src\dccpeep") -Filter "*.c" | Sort-Object Name | ForEach-Object FullName) },
        @{ Name = "dccrtlstrip"; Sources = @((Join-Path $repoRoot "src\dccrtlstrip\dccrtlstrip.c")) },
        @{ Name = "dccmake"; Sources = @((Join-Path $repoRoot "src\dccmake\dccmake.c")) },
        @{ Name = "m80c"; Sources = @((Join-Path $repoRoot "src\m80c\m80c.c")) },
        @{ Name = "l80c"; Sources = @((Join-Path $repoRoot "src\l80c\l80c.c")) }
    )

    foreach ($tool in $tools) {
        Write-Host "`n=== Building $($tool.Name) ==="
        $toolObjDir = Join-Path $outputRoot $tool.Name
        $toolOut = Join-Path $repoRoot "$($tool.Name).exe"
        New-BuildDirectory $toolObjDir

        $arguments = @($tool.Sources) + $cflags + @(
            "/Fo:$toolObjDir\",
            "/Fa$toolObjDir\",
            "/Fd:$toolObjDir\$($tool.Name).pdb",
            "/Fe:$toolOut",
            "/link"
        ) + $linkerFlags + @("/PDB:$toolObjDir\$($tool.Name)-link.pdb")
        Invoke-Checked "cl" $arguments "$($tool.Name) compilation"
    }

    return @($dccOut, (Join-Path $repoRoot "dccpeep.exe"), (Join-Path $repoRoot "dccrtlstrip.exe"), (Join-Path $repoRoot "dccmake.exe"), (Join-Path $repoRoot "m80c.exe"), (Join-Path $repoRoot "l80c.exe"))
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

    Write-Host "`n=== Building dcc compiler ==="
    $dccObjDir = Join-Path $outputRoot "dcc"
    $dccOut = Join-Path $repoRoot "dcc"
    New-BuildDirectory $dccObjDir

    $dccSources = Get-ChildItem -Path (Join-Path $repoRoot "src\dcc") -Filter "*.c" | Sort-Object Name
    $dccObjects = @()
    foreach ($source in $dccSources) {
        $object = Join-Path $dccObjDir ([System.IO.Path]::ChangeExtension($source.Name, ".o"))
        $dccObjects += $object
        $arguments = @($baseCflags) + @("-I", (Join-Path $repoRoot "src\dcc"), "-c", $source.FullName, "-o", $object)
        Invoke-Checked $compiler $arguments "compiling $($source.Name)"
    }
    Invoke-Checked $compiler (@($baseCflags) + $dccObjects + $linkFlags + @("-o", $dccOut)) "linking dcc"

    $tools = @(
        @{ Name = "dccpeep"; Sources = @(Get-ChildItem (Join-Path $repoRoot "src/dccpeep") -Filter "*.c" | Sort-Object Name | ForEach-Object FullName) },
        @{ Name = "dccrtlstrip"; Sources = @((Join-Path $repoRoot "src/dccrtlstrip/dccrtlstrip.c")) },
        @{ Name = "dccmake"; Sources = @((Join-Path $repoRoot "src/dccmake/dccmake.c")) },
        @{ Name = "m80c"; Sources = @((Join-Path $repoRoot "src/m80c/m80c.c")) },
        @{ Name = "l80c"; Sources = @((Join-Path $repoRoot "src/l80c/l80c.c")) }
    )

    foreach ($tool in $tools) {
        Write-Host "`n=== Building $($tool.Name) ==="
        $toolObjDir = Join-Path $outputRoot $tool.Name
        $toolOut = Join-Path $repoRoot $tool.Name
        New-BuildDirectory $toolObjDir

        $toolObjects = @()
        foreach ($source in $tool.Sources) {
            $toolObject = Join-Path $toolObjDir ([System.IO.Path]::ChangeExtension((Split-Path $source -Leaf), ".o"))
            $toolObjects += $toolObject
            Invoke-Checked $compiler (@($baseCflags) + @("-I", (Split-Path $source -Parent), "-c", $source, "-o", $toolObject)) "compiling $($tool.Name):$(Split-Path $source -Leaf)"
        }
        Invoke-Checked $compiler (@($baseCflags) + $toolObjects + $linkFlags + @("-o", $toolOut)) "linking $($tool.Name)"
    }

    return @($dccOut, (Join-Path $repoRoot "dccpeep"), (Join-Path $repoRoot "dccrtlstrip"), (Join-Path $repoRoot "dccmake"), (Join-Path $repoRoot "m80c"), (Join-Path $repoRoot "l80c"))
}

Write-Host "Build artifacts will go to: $outputPathDisplay"
Write-Host "Commands will be placed in: $repoRoot"

$executables = if ($IsWindows) {
    Build-WindowsMsvc
} else {
    Build-UnixNative
}

Write-Host "`n=== Build complete ==="
Write-Host "Commands:"
foreach ($executable in $executables) {
    Write-Host "  $executable"
}
Write-Host "Build artifacts: $outputPathDisplay"
