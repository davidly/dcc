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

$helperScripts = @("ma.ps1", "stacksize.sh", "stacksize.bat", "README.md")
foreach ($script in $helperScripts) {
    Copy-RequiredFile -Source (Join-Path $repoRoot "scripts/$script") -Destination (Join-Path $scriptsDir $script)
}

Copy-RequiredFile -Source (Join-Path $repoRoot "README.md") -Destination (Join-Path $packageRoot "README.md")
Copy-RequiredFile -Source (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $packageRoot "LICENSE")

function Write-PackageTextFile {
    param(
        [string]$Path,
        [string]$Content
    )

    Set-Content -LiteralPath $Path -Value $Content -Encoding utf8
}

$installPs1 = @'
#Requires -Version 7
param(
    [string]$InstallDir = "",
    [switch]$AddToUserPath
)

$ErrorActionPreference = "Stop"

if (-not $InstallDir) {
    $InstallDir = if ($IsWindows) {
        Join-Path $env:LOCALAPPDATA "dcc"
    } else {
        Join-Path $HOME ".local/dcc"
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

Write-Host "dcc installed to: $installPath"
Write-Host "Tools: $installPath/bin"
Write-Host "PowerShell helpers: $installPath/scripts"
if ($AddToUserPath) {
    Write-Host "User PATH updated. Restart your terminal for PATH changes to take effect."
}
else {
    Write-Host "Run with -AddToUserPath to add bin/ and scripts/ to your user PATH."
}
'@

$uninstallPs1 = @'
#Requires -Version 7
param(
    [string]$InstallDir = "",
    [switch]$RemoveFromUserPath
)

$ErrorActionPreference = "Stop"

if (-not $InstallDir) {
    $InstallDir = if ($IsWindows) {
        Join-Path $env:LOCALAPPDATA "dcc"
    } else {
        Join-Path $HOME ".local/dcc"
    }
}

$installPath = [System.IO.Path]::GetFullPath($InstallDir)

if (Test-Path -LiteralPath $installPath) {
    Remove-Item -LiteralPath $installPath -Recurse -Force
    Write-Host "Removed: $installPath"
}
else {
    Write-Host "Install directory not found: $installPath"
}

if ($RemoveFromUserPath) {
    $separator = [System.IO.Path]::PathSeparator
    $pathsToRemove = @((Join-Path $installPath "bin"), (Join-Path $installPath "scripts"))
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $remainingEntries = @($currentPath -split [regex]::Escape($separator) |
        Where-Object { $_ -and ($pathsToRemove -notcontains $_) })
    [Environment]::SetEnvironmentVariable("Path", ($remainingEntries -join $separator), "User")
    Write-Host "User PATH entries removed. Restart your terminal for PATH changes to take effect."
}
'@

$installSh = @'
#!/usr/bin/env sh
set -eu

PACKAGE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX=${PREFIX:-"$HOME/.local/dcc"}
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

mkdir -p "$LINK_DIR"
for tool in dcc dccpeep dccrtlstrip ntvcm; do
    if [ -f "$PREFIX/bin/$tool" ]; then
        ln -sf "$PREFIX/bin/$tool" "$LINK_DIR/$tool"
    fi
done

cat > "$LINK_DIR/dcc-ma" <<EOF
#!/usr/bin/env sh
exec pwsh "$PREFIX/scripts/ma.ps1" "\$@"
EOF
chmod +x "$LINK_DIR/dcc-ma"

echo "dcc installed to: $PREFIX"
echo "Command links written to: $LINK_DIR"
echo "PowerShell helper scripts are in: $PREFIX/scripts"
echo "Use dcc-ma as a convenience wrapper for scripts/ma.ps1."
'@

$uninstallSh = @'
#!/usr/bin/env sh
set -eu

PREFIX=${PREFIX:-"$HOME/.local/dcc"}
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
}

$versionLine = if ($Version) { "Version: $Version" } else { "Version: development build" }
$packageReadme = @"
# dcc binary package

$versionLine

This package contains the dcc host tools, the ntvcm CP/M emulator, the CP/M
assembler/linker tools used by the build pipeline, the DCC runtime, and the
public standard-library headers.

## Layout

- bin/ - dcc, dccpeep, dccrtlstrip, and ntvcm for this host platform.
- include/ - dcc public C headers.
- lib/ - DCCRTL.MAC runtime library.
- scripts/ - PowerShell and shell helper scripts, including ma.ps1.
- DCCRTL.MAC, m80.com, l80.com - staged at the package root for compatibility
  with ma.ps1 and the existing build pipeline.

## Install

Windows PowerShell 7+:

```pwsh
pwsh ./install.ps1 -AddToUserPath
```

Linux/macOS:

```sh
./install.sh
```

By default, Unix installs to `\$HOME/.local/dcc` and links commands into
`\$HOME/.local/bin`. Override with `PREFIX=/path/to/dcc LINK_DIR=/path/to/bin
./install.sh`.

## Portable quick start

From this package root, add bin to your PATH, then build a C source file:

```pwsh
`$env:PATH = "`$PWD/bin`$([IO.Path]::PathSeparator)`$env:PATH"
pwsh ./scripts/ma.ps1 hello -SourcePath ./hello.c -Mode fast
```

The resulting CP/M .COM and intermediate build files are written under build/.
"@

Write-PackageTextFile -Path (Join-Path $packageRoot "PACKAGE-README.md") -Content $packageReadme

Write-Host "Package staged at: $packageRoot"