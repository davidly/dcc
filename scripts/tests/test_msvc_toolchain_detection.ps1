#Requires -Version 7
<#
.SYNOPSIS
Regression test for build-dcc.ps1's MSVC toolchain path detection.

.DESCRIPTION
Get-MsvcVarsPath (in scripts/build-dcc.ps1) previously missed Visual Studio
Preview/RC installs through both of its lookup strategies: the vswhere call
lacked -prerelease (vswhere excludes prerelease instances by default even
with -products *), and the hardcoded fallback edition list
(Community/Professional/Enterprise/BuildTools) didn't include "Preview" -
the actual folder segment VS uses for that channel. This was reported on
Windows ARM64 with vcvarsarm64.bat confirmed present on disk but
build-dcc.ps1 unable to find it ("Could not find MSVC build tools").

This test builds a fake "Program Files\Microsoft Visual Studio\2022\Preview\
VC\Auxiliary\Build\vcvars64.bat" tree with no real vswhere.exe alongside it
(forcing the code through the fallback enumeration path specifically - the
one whose edition list was missing "Preview"), points Get-MsvcVarsPath at it
by temporarily overriding ProgramFiles/ProgramFiles(x86), and asserts it's
found. Runs on any platform pwsh does (the logic under test is pure string/
path matching - it doesn't need a real Windows ARM64 host or a real Visual
Studio install), so it runs as part of the ordinary Linux CI job rather than
needing a dedicated Windows ARM64 runner.

.EXAMPLE
  pwsh ./scripts/tests/test_msvc_toolchain_detection.ps1
#>

$ErrorActionPreference = "Stop"
$failures = 0

function Assert-Equal {
    param([string]$Actual, [string]$Expected, [string]$Description)
    if ($Actual -eq $Expected) {
        Write-Host "  PASS: $Description"
    } else {
        Write-Host "  FAIL: $Description" -ForegroundColor Red
        Write-Host "        expected: $Expected"
        Write-Host "        actual:   $Actual"
        $script:failures++
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).ProviderPath
$buildDccPs1 = Join-Path $repoRoot "scripts/build-dcc.ps1"

# Dot-source build-dcc.ps1 to reach its functions (Get-MsvcToolchain,
# Get-MsvcVarsPath/Get-VisualStudioCmakePath) without running a real build - see the sentinel comment
# in build-dcc.ps1 immediately above its main-execution block.
$env:DCC_BUILD_DCC_SKIP_MAIN = "1"
try {
    . $buildDccPs1 -OutputPath (Join-Path ([System.IO.Path]::GetTempPath()) "dcc-msvc-test-build-$PID")
} finally {
    Remove-Item Env:\DCC_BUILD_DCC_SKIP_MAIN -ErrorAction SilentlyContinue
}

$fakeRoot = Join-Path ([System.IO.Path]::GetTempPath()) "dcc-msvc-test-$PID"
$originalProgramFiles = $env:ProgramFiles
$originalProgramFilesX86 = ${env:ProgramFiles(x86)}

try {
    Write-Host "Test: Get-MsvcVarsPath finds a Preview-edition install via the fallback enumeration"

    # A ProgramFiles(x86) with no Installer\vswhere.exe forces Get-MsvcVarsPath
    # past the vswhere branch entirely and into the hardcoded fallback
    # enumeration - the one whose $editions list was missing "Preview".
    $fakeProgramFilesX86 = Join-Path $fakeRoot "Program Files (x86)"
    New-Item -ItemType Directory -Path $fakeProgramFilesX86 -Force | Out-Null

    $toolchain = Get-MsvcToolchain
    $previewBuildDir = Join-Path $fakeRoot "Program Files/Microsoft Visual Studio/2022/Preview/VC/Auxiliary/Build"
    New-Item -ItemType Directory -Path $previewBuildDir -Force | Out-Null
    $fakeVcVars = Join-Path $previewBuildDir $toolchain.VcVars
    Set-Content -Path $fakeVcVars -Value "@echo off`r`n" -NoNewline

    $env:ProgramFiles = Join-Path $fakeRoot "Program Files"
    ${env:ProgramFiles(x86)} = $fakeProgramFilesX86

    $found = Get-MsvcVarsPath
    Assert-Equal -Actual $found -Expected $fakeVcVars `
        -Description "finds the target vcvars script under a Preview-only install"

    Write-Host ""
    Write-Host "Test: Get-VisualStudioCmakePath finds CMake from the same VS install"
    $cmakeDir = Join-Path $fakeRoot "Program Files/Microsoft Visual Studio/2022/Preview/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"
    New-Item -ItemType Directory -Path $cmakeDir -Force | Out-Null
    $fakeCmake = Join-Path $cmakeDir "cmake.exe"
    Set-Content -Path $fakeCmake -Value "fake CMake" -NoNewline
    $foundCmake = Get-VisualStudioCmakePath
    Assert-Equal -Actual $foundCmake -Expected $fakeCmake `
        -Description "finds Visual Studio's bundled CMake without PATH"

    Write-Host ""
    Write-Host "Test: Get-MsvcVarsPath returns null when nothing matches (no false positive)"
    Remove-Item -Path $fakeVcVars -Force
    $notFound = Get-MsvcVarsPath
    Assert-Equal -Actual "$notFound" -Expected "" `
        -Description "returns nothing once the fake install is removed"
    $missingCmake = Get-VisualStudioCmakePath
    Assert-Equal -Actual "$missingCmake" -Expected "" `
        -Description "does not use CMake after its VS install is unavailable"
}
finally {
    if ($null -ne $originalProgramFiles) { $env:ProgramFiles = $originalProgramFiles }
    if ($null -ne $originalProgramFilesX86) { ${env:ProgramFiles(x86)} = $originalProgramFilesX86 }
    Remove-Item -Path $fakeRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "All MSVC toolchain detection tests passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$failures MSVC toolchain detection test(s) failed." -ForegroundColor Red
    exit 1
}
