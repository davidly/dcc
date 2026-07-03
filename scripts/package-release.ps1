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
$scriptsDir = Join-Path $packageRoot "scripts"
foreach ($dir in @($binDir, $includeDir, $libDir, $scriptsDir)) {
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
$hostTools = @("dcc", "dccpeep", "dccrtlstrip")
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

$helperScripts = @("ma.ps1", "ma.sh", "stacksize.sh", "stacksize.bat")
foreach ($script in $helperScripts) {
    Copy-RequiredFile -Source (Join-Path $repoRoot "scripts/$script") -Destination (Join-Path $scriptsDir $script)
}

function Write-PackageTextFile {
    param(
        [string]$Path,
        [string]$Content
    )

    Set-Content -LiteralPath $Path -Value $Content -Encoding utf8
}

$windowsBuildCommand = @'
@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "DCC_HOME=%%~fI"
set "PATH=%DCC_HOME%\bin;%PATH%"
set "DCC_INCLUDE=%DCC_HOME%\include;%DCC_INCLUDE%"
set "DCC_LIB=%DCC_HOME%\lib;%DCC_HOME%;%DCC_LIB%"
if not defined DCC_RUNTIME set "DCC_RUNTIME=%DCC_HOME%\lib\DCCRTL.MAC"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%DCC_HOME%\scripts\ma.ps1" %*
exit /b %ERRORLEVEL%
'@

$unixBuildCommand = @'
#!/usr/bin/env sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DCC_HOME=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
export DCC_HOME
export PATH="$DCC_HOME/bin:$PATH"
export DCC_INCLUDE="$DCC_HOME/include${DCC_INCLUDE:+:$DCC_INCLUDE}"
export DCC_LIB="$DCC_HOME/lib:$DCC_HOME${DCC_LIB:+:$DCC_LIB}"
export DCC_RUNTIME="${DCC_RUNTIME:-$DCC_HOME/lib/DCCRTL.MAC}"
exec "$DCC_HOME/scripts/ma.sh" "$@"
'@

if ($IsWindows) {
    Write-PackageTextFile -Path (Join-Path $binDir "dcc-ma.cmd") -Content $windowsBuildCommand
}
else {
    Write-PackageTextFile -Path (Join-Path $binDir "dcc-ma") -Content $unixBuildCommand
    & chmod +x (Join-Path $binDir "dcc-ma")
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
    $pathsToAdd = @((Join-Path $installPath "bin"), (Join-Path $installPath "scripts"))
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

Write-Host "dcc installed to: $installPath"
Write-Host "Tools: $installPath/bin"
Write-Host "PowerShell helpers: $installPath/scripts"
Write-Host "User environment variables set: DCC_HOME, DCC_INCLUDE, DCC_LIB, DCC_RUNTIME"
if ($AddToUserPath) {
    Write-Host "User PATH updated. Restart your terminal for PATH changes to take effect."
}
else {
    Write-Host "Run with -AddToUserPath to add bin/ and scripts/ to your user PATH."
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
}
foreach ($installPath in $installCandidates) {
    $expectedEnvValues.DCC_HOME += $installPath
    $expectedEnvValues.DCC_INCLUDE += (Join-Path $installPath "include")
    $expectedEnvValues.DCC_LIB += ((Join-Path $installPath "lib"), $installPath -join [System.IO.Path]::PathSeparator)
    $expectedEnvValues.DCC_RUNTIME += (Join-Path $installPath "lib/DCCRTL.MAC")
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
EOF

mkdir -p "$LINK_DIR"
for tool in dcc dccpeep dccrtlstrip ntvcm; do
    if [ -f "$PREFIX/bin/$tool" ]; then
        ln -sf "$PREFIX/bin/$tool" "$LINK_DIR/$tool"
    fi
done

cat > "$LINK_DIR/dcc-ma" <<EOF
#!/usr/bin/env sh
export DCC_HOME="$PREFIX"
export PATH="\$DCC_HOME/bin:\$PATH"
export DCC_INCLUDE="\$DCC_HOME/include\${DCC_INCLUDE:+:\$DCC_INCLUDE}"
export DCC_LIB="\$DCC_HOME/lib:\$DCC_HOME\${DCC_LIB:+:\$DCC_LIB}"
export DCC_RUNTIME="\${DCC_RUNTIME:-\$DCC_HOME/lib/DCCRTL.MAC}"
exec "$PREFIX/scripts/ma.sh" "\$@"
EOF
chmod +x "$LINK_DIR/dcc-ma"

echo "dcc installed to: $PREFIX"
echo "Command links written to: $LINK_DIR"
echo "Helper scripts are in: $PREFIX/scripts"
echo "Package environment file written to: $PREFIX/dcc-env.sh"
echo "Source it with: . $PREFIX/dcc-env.sh"
echo "Use dcc-ma to build CP/M apps with dcc."
'@

$uninstallSh = @'
#!/usr/bin/env sh
set -eu

PREFIX=${PREFIX:-"$HOME/.local/dcc-cpm-z80"}
LINK_DIR=${LINK_DIR:-"$HOME/.local/bin"}

rm -rf "$PREFIX"
for tool in dcc dccpeep dccrtlstrip ntvcm dcc-ma; do
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
    & chmod +x (Join-Path $scriptsDir "ma.sh")
}

$versionLine = if ($Version) { "Version: $Version" } else { "Version: development build" }
$packageReadme = @"
# dcc binary package

$versionLine

This package contains the dcc host tools, the ntvcm CP/M emulator, the CP/M
assembler/linker tools used by the build pipeline, the DCC runtime, and the
public standard-library headers.

## Layout

- bin/ - dcc, dccpeep, dccrtlstrip, ntvcm, and dcc-ma for this host platform.
- include/ - dcc public C headers.
- lib/ - DCCRTL.MAC runtime library.
- scripts/ - helper scripts, including ma.sh and ma.ps1.
- DCCRTL.MAC, m80.com, l80.com - staged at the package root for compatibility
    with ma.sh, ma.ps1, and the existing build pipeline.

## Install

Windows PowerShell:

```pwsh
powershell.exe -ExecutionPolicy Bypass -File .\install.ps1 -AddToUserPath
```

Linux/macOS:

```sh
./install.sh
```

By default, Unix installs to `\$HOME/.local/dcc-cpm-z80` and links commands into
`\$HOME/.local/bin`. Override with `PREFIX=/path/to/dcc LINK_DIR=/path/to/bin
./install.sh`.

The Unix installer writes `dcc-env.sh` under the install prefix and creates a
`dcc-ma` command in `LINK_DIR`. Source `dcc-env.sh` from
custom shells or scripts when you need the package environment outside the
`dcc-ma` wrapper:

```sh
. \$HOME/.local/dcc-cpm-z80/dcc-env.sh
```

## Portable quick start

From this package root, add bin to your PATH, then build a C source file:

Linux/macOS:

```sh
export PATH="\$PWD/bin:\$PATH"
export DCC_HOME="\$PWD"
dcc-ma hello --source-path ./hello.c --mode fast
```

Windows PowerShell:

```pwsh
`$env:PATH = "`$PWD/bin`$([IO.Path]::PathSeparator)`$env:PATH"
`$env:DCC_HOME = "`$PWD"
dcc-ma hello -SourcePath .\hello.c -Mode fast
```

The resulting CP/M .COM and intermediate build files are written under build/.
"@

Write-PackageTextFile -Path (Join-Path $packageRoot "PACKAGE-README.md") -Content $packageReadme

Write-Host "Package staged at: $packageRoot"