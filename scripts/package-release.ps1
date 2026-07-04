#Requires -Version 7
<#
.SYNOPSIS
Create a dcc binary release package from already-built tools.

.DESCRIPTION
Stages the host tools, ntvcm emulator, CP/M assembler/linker, DCC runtime,
standard-library headers, and helper scripts into a versioned package directory.
The caller is responsible for building dcc and ntvcm before invoking this script.
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$PackageName,

    [Parameter(Mandatory = $true)]
    [string]$NtvcmPath,

    [string]$OutputDir = "dist",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    [System.IO.Path]::GetFullPath($OutputDir)
} else {
    Join-Path $repoRoot $OutputDir
}
$packageRoot = Join-Path $outputRoot $PackageName

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

$binDir = Join-Path $packageRoot "bin"
$includeDir = Join-Path $packageRoot "include"
$libDir = Join-Path $packageRoot "lib"
foreach ($dir in @($binDir, $includeDir, $libDir)) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required file not found: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$exeExt = if ($IsWindows) { ".exe" } else { "" }
$hostTools = @("dcc", "dccpeep", "dccrtlstrip", "dccmake")
foreach ($tool in $hostTools) {
    Copy-RequiredFile -Source (Join-Path $repoRoot "$tool$exeExt") -Destination (Join-Path $binDir "$tool$exeExt")
}

Copy-RequiredFile -Source $NtvcmPath -Destination (Join-Path $binDir ([System.IO.Path]::GetFileName($NtvcmPath)))

foreach ($toolFile in @("m80.com", "l80.com")) {
    Copy-RequiredFile -Source (Join-Path $repoRoot $toolFile) -Destination (Join-Path $packageRoot $toolFile)
}

Copy-RequiredFile -Source (Join-Path $repoRoot "DCCRTL.MAC") -Destination (Join-Path $packageRoot "DCCRTL.MAC")
Copy-RequiredFile -Source (Join-Path $repoRoot "DCCRTL.MAC") -Destination (Join-Path $libDir "DCCRTL.MAC")

$headers = @(
    "assert.h", "ctype.h", "dirent.h", "errno.h", "fcntl.h", "float.h",
    "limits.h", "locale.h", "math.h", "setjmp.h", "signal.h", "stdarg.h",
    "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "string.h",
    "time.h", "unistd.h"
)
foreach ($header in $headers) {
    Copy-RequiredFile -Source (Join-Path $repoRoot $header) -Destination (Join-Path $includeDir $header)
}

function Write-PackageTextFile {
    param(
        [string]$Path,
        [string]$Content
    )

    Set-Content -LiteralPath $Path -Value $Content -Encoding utf8
}

Copy-RequiredFile -Source (Join-Path $repoRoot "README.md") -Destination (Join-Path $packageRoot "README.md")
Copy-RequiredFile -Source (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $packageRoot "LICENSE")

$installPs1 = @'
param(
    [string]$InstallDir = "",
    [switch]$AddToUserPath
)

$ErrorActionPreference = "Stop"
$isWindowsHost = [System.IO.Path]::DirectorySeparatorChar -eq '\'

if (-not $InstallDir) {
    $InstallDir = if ($isWindowsHost) {
        Join-Path $env:LOCALAPPDATA "dcc-cpm-z80"
    } else {
        Join-Path $HOME ".local/dcc-cpm-z80"
    }
}

$packageRoot = $PSScriptRoot
$installPath = [System.IO.Path]::GetFullPath($InstallDir)

if (Test-Path -LiteralPath $installPath) {
    Remove-Item -LiteralPath $installPath -Recurse -Force
}
New-Item -ItemType Directory -Path $installPath -Force | Out-Null

Get-ChildItem -LiteralPath $packageRoot -Force |
    Where-Object { $_.Name -notin @("install.ps1", "install.sh", "uninstall.ps1", "uninstall.sh") } |
    Copy-Item -Destination $installPath -Recurse -Force

if ($AddToUserPath) {
    $pathsToAdd = @((Join-Path $installPath "bin"))
    $separator = [System.IO.Path]::PathSeparator
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $currentEntries = @($currentPath -split [regex]::Escape($separator) | Where-Object { $_ })
    foreach ($pathToAdd in $pathsToAdd) {
        if ($currentEntries -notcontains $pathToAdd) {
            $currentEntries += $pathToAdd
        }
    }
    [Environment]::SetEnvironmentVariable("Path", ($currentEntries -join $separator), "User")
}

[Environment]::SetEnvironmentVariable("DCC_HOME", $installPath, "User")
[Environment]::SetEnvironmentVariable("DCC_INCLUDE", (Join-Path $installPath "include"), "User")
[Environment]::SetEnvironmentVariable("DCC_LIB", ((Join-Path $installPath "lib"), $installPath -join [System.IO.Path]::PathSeparator), "User")
[Environment]::SetEnvironmentVariable("DCC_RUNTIME", (Join-Path $installPath "lib/DCCRTL.MAC"), "User")
[Environment]::SetEnvironmentVariable("M80", (Join-Path $installPath "m80.com"), "User")
[Environment]::SetEnvironmentVariable("L80", (Join-Path $installPath "l80.com"), "User")

Write-Host "dcc installed to: $installPath"
Write-Host "Tools: $installPath/bin"
Write-Host "User environment variables set: DCC_HOME, DCC_INCLUDE, DCC_LIB, DCC_RUNTIME, M80, L80"
if ($AddToUserPath) {
    Write-Host "User PATH updated. Restart your terminal for PATH changes to take effect."
}
else {
    Write-Host "Run with -AddToUserPath to add bin/ to your user PATH."
}
'@

$uninstallPs1 = @'
param(
    [string]$InstallDir = "",
    [switch]$RemoveFromUserPath
)

$ErrorActionPreference = "Stop"
$isWindowsHost = [System.IO.Path]::DirectorySeparatorChar -eq '\'

if (-not $InstallDir) {
    $InstallDir = if ($isWindowsHost) {
        Join-Path $env:LOCALAPPDATA "dcc-cpm-z80"
    } else {
        Join-Path $HOME ".local/dcc-cpm-z80"
    }
}

$installCandidates = @()
if ($InstallDir) {
    $installCandidates += [System.IO.Path]::GetFullPath($InstallDir)
}
else {
    $defaultInstallDir = if ($isWindowsHost) {
        Join-Path $env:LOCALAPPDATA "dcc-cpm-z80"
    } else {
        Join-Path $HOME ".local/dcc-cpm-z80"
    }
    $installCandidates += [System.IO.Path]::GetFullPath($defaultInstallDir)

    $legacyInstallDir = if ($isWindowsHost) {
        Join-Path $env:LOCALAPPDATA "dcc"
    } else {
        Join-Path $HOME ".local/dcc"
    }
    $legacyInstallPath = [System.IO.Path]::GetFullPath($legacyInstallDir)
    if ($installCandidates -notcontains $legacyInstallPath) {
        $installCandidates += $legacyInstallPath
    }
}

$removedAny = $false
foreach ($installPath in $installCandidates) {
    if (Test-Path -LiteralPath $installPath) {
        Remove-Item -LiteralPath $installPath -Recurse -Force
        Write-Host "Removed: $installPath"
        $removedAny = $true
    }
}
if (-not $removedAny) {
    Write-Host "No install directory found. Checked: $($installCandidates -join ', ')"
}

if ($RemoveFromUserPath) {
    $separator = [System.IO.Path]::PathSeparator
    $pathsToRemove = @()
    foreach ($installPath in $installCandidates) {
        $pathsToRemove += (Join-Path $installPath "bin")
        $pathsToRemove += (Join-Path $installPath "scripts")
    }
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $remainingEntries = @($currentPath -split [regex]::Escape($separator) |
        Where-Object { $_ -and ($pathsToRemove -notcontains $_) })
    [Environment]::SetEnvironmentVariable("Path", ($remainingEntries -join $separator), "User")
    Write-Host "User PATH entries removed. Restart your terminal for PATH changes to take effect."
}

$expectedEnvValues = @{
    DCC_HOME = @()
    DCC_INCLUDE = @()
    DCC_LIB = @()
    DCC_RUNTIME = @()
    M80 = @()
    L80 = @()
}
foreach ($installPath in $installCandidates) {
    $expectedEnvValues.DCC_HOME += $installPath
    $expectedEnvValues.DCC_INCLUDE += (Join-Path $installPath "include")
    $expectedEnvValues.DCC_LIB += ((Join-Path $installPath "lib"), $installPath -join [System.IO.Path]::PathSeparator)
    $expectedEnvValues.DCC_RUNTIME += (Join-Path $installPath "lib/DCCRTL.MAC")
    $expectedEnvValues.M80 += (Join-Path $installPath "m80.com")
    $expectedEnvValues.L80 += (Join-Path $installPath "l80.com")
}
foreach ($entry in $expectedEnvValues.GetEnumerator()) {
    $currentValue = [Environment]::GetEnvironmentVariable($entry.Key, "User")
    if ($entry.Value -contains $currentValue) {
        [Environment]::SetEnvironmentVariable($entry.Key, $null, "User")
        Write-Host "Removed user environment variable: $($entry.Key)"
    }
}
'@

$installSh = @'
#!/usr/bin/env sh
set -eu

PACKAGE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX=${PREFIX:-"$HOME/.local/dcc-cpm-z80"}
LINK_DIR=${LINK_DIR:-"$HOME/.local/bin"}

rm -rf "$PREFIX"
mkdir -p "$PREFIX"
(
    cd "$PACKAGE_DIR"
    tar --exclude='./install.ps1' --exclude='./install.sh' --exclude='./uninstall.ps1' --exclude='./uninstall.sh' -cf - .
) | (
    cd "$PREFIX"
    tar -xpf -
)

cat > "$PREFIX/dcc-env.sh" <<EOF
# dcc CP/M Z80 package environment
export DCC_HOME="$PREFIX"
export DCC_INCLUDE="\$DCC_HOME/include\${DCC_INCLUDE:+:\$DCC_INCLUDE}"
export DCC_LIB="\$DCC_HOME/lib:\$DCC_HOME\${DCC_LIB:+:\$DCC_LIB}"
export DCC_RUNTIME="\${DCC_RUNTIME:-\$DCC_HOME/lib/DCCRTL.MAC}"
export M80="\${M80:-\$DCC_HOME/m80.com}"
export L80="\${L80:-\$DCC_HOME/l80.com}"
EOF

mkdir -p "$LINK_DIR"
for tool in dcc dccpeep dccrtlstrip dccmake ntvcm; do
    if [ -f "$PREFIX/bin/$tool" ]; then
        ln -sf "$PREFIX/bin/$tool" "$LINK_DIR/$tool"
    fi
done

echo "dcc installed to: $PREFIX"
echo "Command links written to: $LINK_DIR"
echo "Package environment file written to: $PREFIX/dcc-env.sh"
echo "Source it with: . $PREFIX/dcc-env.sh"
echo "Use dccmake to build CP/M apps with dcc."
'@

$uninstallSh = @'
#!/usr/bin/env sh
set -eu

PREFIX=${PREFIX:-"$HOME/.local/dcc-cpm-z80"}
LINK_DIR=${LINK_DIR:-"$HOME/.local/bin"}

rm -rf "$PREFIX"
for tool in dcc dccpeep dccrtlstrip dccmake ntvcm; do
    if [ -L "$LINK_DIR/$tool" ] || [ -f "$LINK_DIR/$tool" ]; then
        rm -f "$LINK_DIR/$tool"
    fi
done

echo "dcc removed from: $PREFIX"
echo "Command links removed from: $LINK_DIR"
'@

Write-PackageTextFile -Path (Join-Path $packageRoot "install.ps1") -Content $installPs1
Write-PackageTextFile -Path (Join-Path $packageRoot "uninstall.ps1") -Content $uninstallPs1
Write-PackageTextFile -Path (Join-Path $packageRoot "install.sh") -Content $installSh
Write-PackageTextFile -Path (Join-Path $packageRoot "uninstall.sh") -Content $uninstallSh
if (-not $IsWindows) {
    & chmod +x (Join-Path $packageRoot "install.sh") (Join-Path $packageRoot "uninstall.sh")
}

$versionLine = if ($Version) { "Version: $Version" } else { "Version: development build" }
$packageReadmeTemplate = @'
# dcc binary package

{{VERSION_LINE}}

This package contains the dcc host tools, the ntvcm CP/M emulator, the CP/M
assembler/linker tools used by the build pipeline, the DCC runtime, and the
public standard-library headers.

## Layout

- bin/ - dcc, dccpeep, dccrtlstrip, dccmake, and ntvcm for this host platform.
- include/ - dcc public C headers.
- lib/ - DCCRTL.MAC runtime library.
- DCCRTL.MAC, m80.com, l80.com - staged at the package root for compatibility
    with dccmake and the existing build pipeline.

## Install

Windows PowerShell:

```pwsh
powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -AddToUserPath
```

Linux/macOS:

```sh
./install.sh
```

By default, Unix installs to `$HOME/.local/dcc-cpm-z80` and links commands into
`$HOME/.local/bin`. Override with `PREFIX=/path/to/dcc LINK_DIR=/path/to/bin
./install.sh`.

The Unix installer writes `dcc-env.sh` under the install prefix. Source it from
custom shells or scripts before running `dccmake`:

```sh
. $HOME/.local/dcc-cpm-z80/dcc-env.sh
```

## Portable quick start

From this package root, add bin to your PATH, then build a C source file:

Linux/macOS:

```sh
export PATH="$PWD/bin:$PATH"
export DCC_HOME="$PWD"
export DCC_RUNTIME="$PWD/lib/DCCRTL.MAC"
export M80="$PWD/m80.com"
export L80="$PWD/l80.com"
dccmake hello.c dcc-include="$PWD/include"
```

Windows PowerShell:

```pwsh
$env:PATH = "$PWD/bin$([IO.Path]::PathSeparator)$env:PATH"
$env:DCC_HOME = "$PWD"
$env:DCC_RUNTIME = "$PWD/lib/DCCRTL.MAC"
$env:M80 = "$PWD/m80.com"
$env:L80 = "$PWD/l80.com"
dccmake hello.c dcc-include="$PWD/include"
```

The resulting CP/M .COM and intermediate build files are written under build/.
'@
$packageReadme = $packageReadmeTemplate.Replace("{{VERSION_LINE}}", $versionLine)

Write-PackageTextFile -Path (Join-Path $packageRoot "PACKAGE-README.md") -Content $packageReadme

Write-Host "Package staged at: $packageRoot"