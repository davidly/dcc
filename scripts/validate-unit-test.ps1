#Requires -Version 7
<#
.SYNOPSIS
Validate test baselines by compiling test apps with the host C compiler.

.DESCRIPTION
Builds each tests/*.c app with the platform-native host compiler and compares
stdout against tests/baselines/<app>.txt. This is a read-only baseline check: it
never rewrites baseline files. Host builds use C99 mode where the compiler
supports it and force signed plain char where GCC/clang support it, matching
the C99 conveniences and target char semantics accepted by dcc.

Compiler selection follows scripts/build-dcc.ps1: MSVC on Windows, clang on
macOS, and gcc on Linux by default. On Linux, GCC is used with -m32 when the
compiler can build and link 32-bit executables. On Unix-like hosts, pass -CC or
set CC to override the compiler.

The runner honors tests/_test_overrides.json for per-app args, stdin, ignore,
and host-only skip settings. Tests that explicitly depend on CP/M/Z80-only
services (BDOS, direct port I/O, getch/kbhit console polling, #asm blocks, or
CP/M vector reads) are skipped because host compilers cannot execute those
semantics. A per-app "host-cflags" string in that same file fully replaces the
default `-std=gnu99 -w -O2` for one test's host build only - e.g. a test whose
own point is C89 implicit-int declarator grammar needs `-std=gnu89` to compile
under a strict-by-default host compiler; a test whose correctness check is
sensitive to a specific host compiler's optimizer (not to dcc, which is
unaffected either way) may need `-O0` instead of `-O2`.

.PARAMETER RunTimeout
    Max seconds to let a host test executable run (default: 10).

.PARAMETER BuildDir
  Working directory for host build artifacts (default: build/host-validate).

.PARAMETER BaselineDir
  Directory of per-app baseline files for verification (default: tests/baselines).

.PARAMETER CC
  Override the C compiler used on macOS/Linux. Ignored on Windows, where MSVC
  cl.exe is used.

.PARAMETER App
  Validate only one test app name, without .c.

.PARAMETER Serial
  Build and run apps sequentially. By default the suite runs in parallel
  (one runspace per app, each with its own build subdirectory); use
  -Serial as a fallback (e.g. for debugging or on constrained machines).

.PARAMETER ThrottleLimit
  Max concurrent apps in parallel mode (default: CPU core count).

.PARAMETER Help
  Show this help text and exit without building or running tests.

.EXAMPLE
  pwsh ./scripts/validate-unit-test.ps1
  pwsh ./scripts/validate-unit-test.ps1 -App tprintf
  pwsh ./scripts/validate-unit-test.ps1 -CC clang

.NOTES
  Exit codes:
    0 = all runnable host validations passed
    1 = one or more runnable host validations failed
#>

param(
    [string]$BuildDir = "build/host-validate",
    [string]$BaselineDir = "tests/baselines",
    [string]$CC,
    [string]$App,
    [int]$RunTimeout = 10,
    [switch]$Serial,
    [int]$ThrottleLimit = [Environment]::ProcessorCount,
    [switch]$Help
)

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    return
}

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
Push-Location $repoRoot
try {
    $buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
        [System.IO.Path]::GetFullPath($BuildDir)
    }
    else {
        Join-Path $repoRoot $BuildDir
    }
    if (-not (Test-Path $buildRoot -PathType Container)) {
        New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
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
    pwsh ./scripts/validate-unit-test.ps1 -CC clang
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
    pwsh ./scripts/validate-unit-test.ps1 -CC clang
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
                if (Test-Path $candidate) { return $candidate }
            }
        }

        $versions = @("18", "2022", "2019", "17")
        $editions = @("Community", "Professional", "Enterprise", "BuildTools")
        $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
        foreach ($root in $roots) {
            foreach ($version in $versions) {
                foreach ($edition in $editions) {
                    $candidate = Join-Path $root "Microsoft Visual Studio\$version\$edition\VC\Auxiliary\Build\$($toolchain.VcVars)"
                    if (Test-Path $candidate) { return $candidate }
                }
            }
        }
        return $null
    }

    function Initialize-Msvc {
        $toolchain = Get-MsvcToolchain
        $vcvars = Get-MsvcVarsPath
        if (-not $vcvars) { throw (Get-WindowsBuildToolsHelp) }

        Write-Host "Found $($toolchain.Description): $vcvars"
        $envBlock = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
        foreach ($line in $envBlock) {
            if ($line -match "^([^=]+)=(.*)$") {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
            }
        }

        $clVersion = cl 2>&1 | Select-Object -First 1
        if ($LASTEXITCODE -ne 0) { throw "MSVC cl.exe not found in PATH after vcvars setup." }
        Write-Host "MSVC cl.exe: $clVersion"
    }

    function Get-UnixCompiler {
        if ($CC) { return $CC }
        if ($env:CC) { return $env:CC }
        if ($IsMacOS) { return "clang" }
        return "gcc"
    }

    function Test-GccM32Support {
        param([string]$Compiler)

        if (-not $IsLinux) { return $false }

        $probeDir = Join-Path $buildRoot ".m32-probe"
        New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
        $probeSource = Join-Path $probeDir "m32_probe.c"
        $probeExe = Join-Path $probeDir "m32_probe"

        Set-Content -Path $probeSource -Value "int main(void) { return sizeof(void *) == 4 ? 0 : 1; }" -NoNewline
        try {
            $null = & $Compiler -m32 $probeSource -o $probeExe 2>&1
            return ($LASTEXITCODE -eq 0 -and (Test-Path $probeExe -PathType Leaf))
        }
        finally {
            Remove-Item -LiteralPath $probeSource, $probeExe -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $probeDir -Force -Recurse -ErrorAction SilentlyContinue
        }
    }

    function Get-HostCompiler {
        if ($IsWindows) {
            Initialize-Msvc
            return [pscustomobject]@{
                Kind = "msvc"
                Command = "cl"
                Version = (cl 2>&1 | Select-Object -First 1)
            }
        }

        $compiler = Get-UnixCompiler
        if (-not (Get-Command $compiler -ErrorAction SilentlyContinue)) {
            throw (Get-UnixBuildToolsHelp $compiler)
        }
        $compilerVersionOutput = & $compiler --version 2>&1
        if ($LASTEXITCODE -ne 0) { throw (Get-UnixBuildToolsHelp $compiler) }
        $compilerVersion = $compilerVersionOutput | Select-Object -First 1
        $extraCFlags = @()
        if ($IsLinux -and $compilerVersion -match 'gcc|GNU' -and (Test-GccM32Support $compiler)) {
            $extraCFlags += "-m32"
            $compilerVersion = "$compilerVersion (-m32)"
        }
        return [pscustomobject]@{
            Kind = "unix"
            Command = $compiler
            Version = $compilerVersion
            CFlags = $extraCFlags
        }
    }

    function Get-FixtureFiles {
        if (-not (Test-Path "tests" -PathType Container)) { return @() }
        return @(Get-ChildItem -Path "tests" -File |
            Where-Object { $_.Name -notlike '.*' -and $_.Extension -notin '.c', '.json', '.md' } |
            ForEach-Object {
                [pscustomobject]@{
                    Name = $_.Name
                    Source = $_.FullName
                }
            })
    }

    function Copy-FixtureForHostRun {
        param([object]$Fixture, [string]$DestDir)
        if (-not $Fixture -or -not $Fixture.Name -or -not (Test-Path $Fixture.Source -PathType Leaf)) { return }

        $names = @($Fixture.Name, $Fixture.Name.ToUpper(), $Fixture.Name.ToLower()) | Select-Object -Unique
        foreach ($name in $names) {
            Copy-Item -LiteralPath $Fixture.Source -Destination (Join-Path $DestDir $name) -Force -ErrorAction SilentlyContinue
        }
    }

    function Test-UsesCpmOnlyFeature {
        param([string]$SourceContent)

        $patterns = @(
            @{ Pattern = '(?m)^\s*#\s*asm\b|(?m)^\s*#\s*endasm\b'; Reason = '#asm/#endasm Z80 inline assembly' },
            @{ Pattern = '(?<![A-Za-z0-9_])bdos\s*\('; Reason = 'BDOS call' },
            @{ Pattern = '(?<![A-Za-z0-9_])__BDOS\b'; Reason = 'BDOS call' },
            @{ Pattern = '(?<![A-Za-z0-9_])bios\s*\('; Reason = 'BIOS call' },
            @{ Pattern = '(?<![A-Za-z0-9_])inp\s*\('; Reason = 'Z80 port input' },
            @{ Pattern = '(?<![A-Za-z0-9_])outp\s*\('; Reason = 'Z80 port output' },
            @{ Pattern = '(?<![A-Za-z0-9_])getch\s*\('; Reason = 'CP/M non-echo console input' },
            @{ Pattern = '(?<![A-Za-z0-9_])kbhit\s*\('; Reason = 'CP/M console polling' },
            @{ Pattern = '(?<![A-Za-z0-9_])tmpnam\s*\('; Reason = 'CP/M 8.3 limit' },
            @{ Pattern = '(?<![A-Za-z0-9_])execv\s*\('; Reason = 'CP/M execv' },
            @{ Pattern = '(?<![A-Za-z0-9_])strftime\s*\('; Reason = 'CP/M strftime' },
            @{ Pattern = '(?<![A-Za-z0-9_])assert\s*\('; Reason = 'CP/M assert' },
            @{ Pattern = '(?<![A-Za-z0-9_])abort\s*\('; Reason = 'CP/M abort' },
            @{ Pattern = '\*\s*\([^\)]*\*\)\s*6\b'; Reason = 'CP/M BDOS vector memory access' }
        )

        foreach ($entry in $patterns) {
            if ($SourceContent -match $entry.Pattern) { return $entry.Reason }
        }
        return $null
    }

    function Test-MatchesBaseline {
        param(
            [string]$Actual,
            [string]$Baseline,
            [System.Collections.IDictionary]$Placeholders
        )

        if ($Baseline -notmatch '\{\{[A-Z]+\}\}') {
            return ($Actual -ceq $Baseline)
        }

        $tokenRegex = [regex]'\{\{([A-Z]+)\}\}'
        $sb = [System.Text.StringBuilder]::new()
        $last = 0
        foreach ($m in $tokenRegex.Matches($Baseline)) {
            $literal = $Baseline.Substring($last, $m.Index - $last)
            [void]$sb.Append([regex]::Escape($literal))
            $token = '{{' + $m.Groups[1].Value + '}}'
            if ($Placeholders.Contains($token)) {
                [void]$sb.Append($Placeholders[$token])
            }
            else {
                [void]$sb.Append([regex]::Escape($m.Value))
            }
            $last = $m.Index + $m.Length
        }
        [void]$sb.Append([regex]::Escape($Baseline.Substring($last)))
        return ($Actual -cmatch ('^' + $sb.ToString() + '$'))
    }

    function Invoke-HostCompile {
        param(
            [object]$Compiler,
            [string]$SourceFile,
            [string]$ExePath,
            [string]$WorkDir,
            [string]$RepoRoot,
            [string]$HostCflags
        )

        # Force-included on every host build: supplies host-libc equivalents
        # for dcc RTL extensions (e.g. stricmp) that tests use directly by
        # their dcc name. See the header itself for what belongs here vs. in
        # tests/_test_overrides.json's host/ignore flags. Absolute path:
        # MSVC's /FI does not reliably fall back to searching the process's
        # current directory for a relative force-include the way gcc/clang's
        # -include (and a plain #include "...") do.
        $preludePath = Join-Path $RepoRoot "tests/host-validate-prelude.h"

        if ($Compiler.Kind -eq "msvc") {
            $objPath = Join-Path $WorkDir ([System.IO.Path]::GetFileNameWithoutExtension($ExePath) + ".obj")
            $arguments = @("/nologo", "/w", "/O2", "/Zc:__STDC__", "/std:c11", "/FI", $preludePath, "/Fe:$ExePath", "/Fo:$objPath", $SourceFile)
            $output = & $Compiler.Command @arguments 2>&1
            return [pscustomobject]@{ Success = ($LASTEXITCODE -eq 0 -and (Test-Path $ExePath -PathType Leaf)); Output = ($output -join "`n") }
        }

        # A per-app tests/_test_overrides.json "host-cflags" string takes
        # priority over $env:CFLAGS: it's a correctness requirement for that
        # one test (e.g. -std=gnu89 for a K&R implicit-int regression test,
        # or -O0 where a specific host compiler's optimizer doesn't preserve
        # a property the test checks), not a general build preference.
        $baseCflags = if ($HostCflags) { @($HostCflags -split "\s+" | Where-Object { $_ }) }
                      elseif ($env:CFLAGS) { @($env:CFLAGS -split "\s+" | Where-Object { $_ }) }
                      else { @("-std=gnu99", "-w", "-O2") }
        if ($baseCflags -notcontains "-fsigned-char") { $baseCflags += "-fsigned-char" }
        if ($IsMacOS -and ($baseCflags -notcontains "-fno-common")) { $baseCflags += "-fno-common" }
        $arguments = @($baseCflags) + @($Compiler.CFlags) + @("-include", $preludePath, $SourceFile, "-o", $ExePath, "-lm")
        $output = & $Compiler.Command @arguments 2>&1
        return [pscustomobject]@{ Success = ($LASTEXITCODE -eq 0 -and (Test-Path $ExePath -PathType Leaf)); Output = ($output -join "`n") }
    }

    function Invoke-HostApp {
        param(
            [string]$ExePath,
            [string]$WorkDir,
            [string]$RunArgs,
            [string]$RunStdin,
            [int]$TimeoutSeconds
        )

        $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = $ExePath
        $startInfo.WorkingDirectory = $WorkDir
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardInput = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true

        $appArgs = if ($RunArgs) { @($RunArgs.Split([char]' ', [System.StringSplitOptions]::RemoveEmptyEntries)) } else { @() }
        foreach ($arg in $appArgs) { [void]$startInfo.ArgumentList.Add($arg) }

        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $startInfo
        [void]$process.Start()
        if ($RunStdin) { $process.StandardInput.Write($RunStdin) }
        $process.StandardInput.Close()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $completed = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $completed) {
            try { $process.Kill($true) } catch { }
            return [pscustomobject]@{ ExitCode = $null; Output = ""; TimedOut = $true }
        }

        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()

        $output = if ($stderr) { $stdout + $stderr } else { $stdout }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $output; TimedOut = $false }
    }

    function Invoke-AppValidation {
        param(
            [string]$AppName,
            [object]$Compiler,
            [object[]]$Fixtures,
            [System.Collections.IDictionary]$Placeholders,
            [string]$BuildRoot,
            [string]$BaselineDir,
            [int]$RunTimeout,
            [System.Collections.IDictionary]$Overrides,
            [string]$RepoRoot
        )

        $lines = [System.Collections.Generic.List[string]]::new()
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $sourceFile = Join-Path "tests" "$AppName.c"
        $sourceContent = Get-Content -Path $sourceFile -Raw
        $skipReason = Test-UsesCpmOnlyFeature $sourceContent
        if ($skipReason) {
            $sw.Stop()
            $lines.Add("    SKIP: $skipReason")
            return [pscustomobject]@{ App = $AppName; Status = "Skipped"; Passed = $true; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
        }

        $appBuildDir = Join-Path $BuildRoot $AppName
        if (-not (Test-Path $appBuildDir -PathType Container)) {
            New-Item -ItemType Directory -Path $appBuildDir -Force | Out-Null
        }
        foreach ($fixture in $Fixtures) { Copy-FixtureForHostRun -Fixture $fixture -DestDir $appBuildDir }

        $exeName = if ($IsWindows) { "$AppName.exe" } else { $AppName }
        $exePath = Join-Path $appBuildDir $exeName
        Remove-Item -LiteralPath $exePath -Force -ErrorAction SilentlyContinue

        $hostCflags = Get-AppHostCflags -Name $AppName -Overrides $Overrides
        $compile = Invoke-HostCompile -Compiler $Compiler -SourceFile $sourceFile -ExePath $exePath -WorkDir $appBuildDir -RepoRoot $RepoRoot -HostCflags $hostCflags
        if (-not $compile.Success) {
            $sw.Stop()
            $lines.Add("    COMPILE FAILED")
            if ($compile.Output) { $lines.Add("    " + (($compile.Output -split "`n" | Select-Object -First 5) -join " | ")) }
            return [pscustomobject]@{ App = $AppName; Status = "Failed"; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
        }
        $lines.Add("    Host build complete")

        $run = Invoke-HostApp -ExePath $exePath -WorkDir $appBuildDir -RunArgs (Get-AppArgs -Name $AppName -Overrides $Overrides) -RunStdin (Get-AppStdin -Name $AppName -Overrides $Overrides) -TimeoutSeconds $RunTimeout
        if ($run.TimedOut) {
            $sw.Stop()
            $lines.Add("    ERROR: host app timed out after $RunTimeout seconds")
            return [pscustomobject]@{ App = $AppName; Status = "Failed"; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
        }
        if ($run.ExitCode -ne 0) { $lines.Add("    WARNING: host app exited with code $($run.ExitCode)") }

        $blPath = Join-Path $BaselineDir "$AppName.txt"
        if (-not (Test-Path $blPath -PathType Leaf)) {
            $sw.Stop()
            $lines.Add("    ERROR: no baseline at $BaselineDir/$AppName.txt")
            return [pscustomobject]@{ App = $AppName; Status = "Failed"; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
        }

        $expected = (((Get-Content -Path $blPath -Raw) -replace "`r`n", "`n")).TrimEnd("`n")
        $actual = (($run.Output -replace "`r`n", "`n") -replace "`r", "`n").TrimEnd("`n")
        if (-not (Test-MatchesBaseline -Actual $actual -Baseline $expected -Placeholders $Placeholders)) {
            $sw.Stop()
            $lines.Add("    OUTPUT MISMATCH (vs $BaselineDir/$AppName.txt)")
            $lines.Add("    Got: " + (($actual -split "`n" | Select-Object -First 3) -join ' | '))
            return [pscustomobject]@{ App = $AppName; Status = "Failed"; Passed = $false; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
        }

        $sw.Stop()
        $lines.Add("    Output matches baseline")
        return [pscustomobject]@{ App = $AppName; Status = "Passed"; Passed = $true; Elapsed = $sw.Elapsed; Lines = $lines.ToArray() }
    }

    function Show-AppResult {
        param($Result)

        $elapsed = $Result.Elapsed
        $elapsedStr = if ($elapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $elapsed } else { "{0:0.00}s" -f $elapsed.TotalSeconds }
        $status = switch ($Result.Status) { "Passed" { "PASS" } "Skipped" { "SKIP" } default { "FAIL" } }
        $color = switch ($Result.Status) { "Passed" { "Green" } "Skipped" { "DarkGray" } default { "Red" } }
        Write-Host ("  {0}  {1,-12} {2,8}" -f $status, $Result.App, $elapsedStr) -ForegroundColor $color
        foreach ($detail in $Result.Lines) {
            if ($Result.Status -ne "Passed" -or $detail -match 'matches baseline') {
                $detailColor = if ($detail -match 'FAILED|MISMATCH|WARNING|ERROR') { "Red" } elseif ($detail -match 'SKIP') { "DarkGray" } else { "Green" }
                Write-Host $detail -ForegroundColor $detailColor
            }
        }
    }

    $appOverridesPath = "tests/_test_overrides.json"
    if (-not (Test-Path $appOverridesPath -PathType Leaf)) {
        Write-Warning "App overrides not found: $appOverridesPath"
        $appOverrides = @{}
    }
    else {
        $config = Get-Content $appOverridesPath | ConvertFrom-Json
        $appOverrides = @{}
        foreach ($item in $config.apps) {
            $appOverrides[$item.name] = @{}
            if ($item.args) { $appOverrides[$item.name]['args'] = $item.args }
            if ($item.stdin) { $appOverrides[$item.name]['stdin'] = $item.stdin }
            if ($item.ignore) { $appOverrides[$item.name]['ignore'] = $item.ignore }
            if ($item.host) { $appOverrides[$item.name]['host'] = $item.host }
            if ($item.'requires-32bit-linux-host-compiler') { $appOverrides[$item.name]['requires32'] = $true }
            if ($item.'requires-non-msvc-host-compiler') { $appOverrides[$item.name]['requiresNonMsvc'] = $true }
            if ($item.'host-cflags') { $appOverrides[$item.name]['hostCflags'] = $item.'host-cflags' }
        }
    }

    function Get-AppArgs {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        if ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['args']) { return $Overrides[$Name]['args'] }
        return ""
    }

    function Get-AppStdin {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        if ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['stdin']) { return $Overrides[$Name]['stdin'] }
        return ""
    }

    function Get-IgnoreApp {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        return ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['ignore'])
    }

    function Get-IgnoreHostApp {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        return ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['host'])
    }

    function Get-Requires32BitApp {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        return ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['requires32'])
    }

    function Get-RequiresNonMsvcApp {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        return ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['requiresNonMsvc'])
    }

    function Get-AppHostCflags {
        param([string]$Name, [System.Collections.IDictionary]$Overrides)
        if ($Overrides.ContainsKey($Name) -and $Overrides[$Name]['hostCflags']) { return $Overrides[$Name]['hostCflags'] }
        return ""
    }

    $Placeholders = [ordered]@{
        '{{DATE}}' = '[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}'
        '{{TIME}}' = '\d{2}:\d{2}:\d{2}'
        '{{SEP}}'  = '[/\\]'
        '{{UINT}}' = '\d+'
    }

    $testFiles = if ($App) {
        $candidate = Join-Path "tests" "$App.c"
        if (-not (Test-Path $candidate -PathType Leaf)) { throw "Source file not found for app: $App" }
        @($App)
    }
    else {
        @(Get-ChildItem -Path (Join-Path "tests" "*.c") -File | ForEach-Object { $_.BaseName } | Sort-Object)
    }
    if ($testFiles.Count -eq 0) { throw "No test files found in tests" }

    if (Test-Path $BaselineDir -PathType Container) {
        $baselineCount = @(Get-ChildItem -Path "$BaselineDir/*.txt" -File -ErrorAction SilentlyContinue).Count
        Write-Host "Using per-app baselines from $BaselineDir ($baselineCount files)" -ForegroundColor Cyan
    }
    else {
        Write-Host "No baseline directory at $BaselineDir; every runnable app will fail verification" -ForegroundColor Yellow
    }

    $compiler = Get-HostCompiler
    Write-Host "Host compiler: $($compiler.Version)" -ForegroundColor Cyan
    Write-Host "Build artifacts: $buildRoot" -ForegroundColor Cyan
    $m32Active = @($compiler.CFlags) -contains "-m32"

    $fixtureList = @(Get-FixtureFiles)
    $results = @()
    $skippedByConfig = 0
    $skippedByHostConfig = 0

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "STARTING HOST BASELINE VALIDATION" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    # Filter first (ignore/host-skip are cheap, synchronous decisions) so both
    # the serial and parallel paths below only ever touch apps that actually
    # need a compile+run.
    $appsToRun = [System.Collections.Generic.List[string]]::new()
    foreach ($appName in $testFiles) {
        if (Get-IgnoreApp -Name $appName -Overrides $appOverrides) {
            $skippedByConfig++
            continue
        }
        if (Get-IgnoreHostApp -Name $appName -Overrides $appOverrides) {
            # Tests flagged requires-32bit-linux-host-compiler are skipped on a
            # normal 64-bit host, but a 32-bit Linux compiler (-m32 makes long
            # 4 bytes) reproduces dcc's long width, so run them in that case.
            if ($m32Active -and (Get-Requires32BitApp -Name $appName -Overrides $appOverrides)) {
                # fall through and validate under -m32
            }
            else {
                $skippedByHostConfig++
                continue
            }
        }
        if ($compiler.Kind -eq "msvc" -and (Get-RequiresNonMsvcApp -Name $appName -Overrides $appOverrides)) {
            # MSVC-specific C99 gaps (no VLAs, no array-parameter qualifiers,
            # static _Bool initializers not normalized to 0/1) that gcc/clang
            # handle fine - skip only on MSVC, still validate elsewhere.
            $skippedByHostConfig++
            continue
        }
        $appsToRun.Add($appName)
    }

    $suiteStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    if ($Serial) {
        foreach ($appName in $appsToRun) {
            $result = Invoke-AppValidation -AppName $appName -Compiler $compiler -Fixtures $fixtureList `
                -Placeholders $Placeholders -BuildRoot $buildRoot -BaselineDir $BaselineDir `
                -RunTimeout $RunTimeout -Overrides $appOverrides -RepoRoot $repoRoot
            $results += $result
            Show-AppResult $result
        }
    }
    else {
        Write-Host "(parallel, throttle = $ThrottleLimit)" -ForegroundColor Cyan

        # Each worker runs in its own runspace; Invoke-AppValidation gives every
        # app its own build subdirectory (build/host-validate/<app>), so
        # concurrent compiles/runs never clobber each other's output files.
        $repoRootForWorkers = $repoRoot
        $iavDef  = ${function:Invoke-AppValidation}.ToString()
        $ihcDef  = ${function:Invoke-HostCompile}.ToString()
        $ihaDef  = ${function:Invoke-HostApp}.ToString()
        $tmbDef  = ${function:Test-MatchesBaseline}.ToString()
        $tucofDef = ${function:Test-UsesCpmOnlyFeature}.ToString()
        $cffhrDef = ${function:Copy-FixtureForHostRun}.ToString()
        $gaaDef  = ${function:Get-AppArgs}.ToString()
        $gasDef  = ${function:Get-AppStdin}.ToString()
        $gahcDef = ${function:Get-AppHostCflags}.ToString()

        $appsToRun | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
            Set-Location $using:repoRootForWorkers
            ${function:Invoke-AppValidation}    = $using:iavDef
            ${function:Invoke-HostCompile}      = $using:ihcDef
            ${function:Invoke-HostApp}          = $using:ihaDef
            ${function:Test-MatchesBaseline}    = $using:tmbDef
            ${function:Test-UsesCpmOnlyFeature} = $using:tucofDef
            ${function:Copy-FixtureForHostRun}  = $using:cffhrDef
            ${function:Get-AppArgs}             = $using:gaaDef
            ${function:Get-AppStdin}            = $using:gasDef
            ${function:Get-AppHostCflags}       = $using:gahcDef

            Invoke-AppValidation -AppName $_ -Compiler $using:compiler -Fixtures $using:fixtureList `
                -Placeholders $using:Placeholders -BuildRoot $using:buildRoot -BaselineDir $using:BaselineDir `
                -RunTimeout $using:RunTimeout -Overrides $using:appOverrides -RepoRoot $using:repoRootForWorkers
        } | ForEach-Object {
            $results += $_
            Show-AppResult $_
        }
    }
    $suiteStopwatch.Stop()

    $passed = @($results | Where-Object { $_.Status -eq "Passed" }).Count
    $failedResults = @($results | Where-Object { $_.Status -eq "Failed" })
    $skippedCpm = @($results | Where-Object { $_.Status -eq "Skipped" }).Count
    $failed = $failedResults.Count
    $suiteElapsed = $suiteStopwatch.Elapsed
    $suiteElapsedStr = if ($suiteElapsed.TotalSeconds -ge 60) { "{0:m\m\ s\.f\s}" -f $suiteElapsed } else { "{0:0.00}s" -f $suiteElapsed.TotalSeconds }

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "HOST VALIDATION SUMMARY" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Compiler:         $($compiler.Version)"
    Write-Host "  Total apps:       $($testFiles.Count)"
    Write-Host "  Passed:           $passed" -ForegroundColor Green
    Write-Host "  Failed:           $failed" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
    Write-Host "  Skipped config:   $skippedByConfig"
    Write-Host "  Skipped host:     $skippedByHostConfig"
    Write-Host "  Skipped CP/M:     $skippedCpm"
    Write-Host "  Total time:       $suiteElapsedStr"

    if ($failed -gt 0) {
        Write-Host ""
        Write-Host "Apps with host/baseline issues:" -ForegroundColor Red
        foreach ($result in $failedResults) { Write-Host "  - $($result.App)" -ForegroundColor Red }
        Write-Host ""
        Write-Host ">>> FAILURE: $failed host validation issue(s) found <<<" -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host ">>> SUCCESS: All runnable host validations passed <<<" -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
