<#
.SYNOPSIS
Cross-platform build driver for dcc C compiler targeting CP/M Z80.
Compiles a single app with optional peephole optimization, strips runtime,
and links to produce a .COM executable.

.DESCRIPTION
This is the PowerShell equivalent of ma.sh. It runs under Windows PowerShell 5.1
and PowerShell 7+, and handles the complete build pipeline:
  1. Compile source with dcc (detect floatio, stack-check, floatio flags)
  2. Optimize with dccpeep (optional)
  3. Assemble app.MAC with M80
  4. Strip DCCRTL runtime using dccrtlstrip
  5. Assemble stripped RTLMIN.MAC
  6. Link app + RTLMIN with L80

The build logic lives in the Invoke-MaBuild function so other scripts (e.g.
runall.ps1) can dot-source this file once and call Invoke-MaBuild in-process,
avoiding a fresh PowerShell process per build. When run directly as a script, the
parameters below are forwarded to Invoke-MaBuild.

.PARAMETER Name
  Test app name (e.g., "triangle", "sieve", "ttt").
  Searches: tests/{name}.c, tests/{name}.C, {name}.c, {name}.C

.PARAMETER Mode
    Build mode: "full", "fast", or "nopeep" (default: full).
    Full mode builds both fast and nopeep. Fast mode runs dccpeep optimization;
    nopeep skips it.

.PARAMETER BuildDir
  Working directory for build artifacts (default: "build").

.PARAMETER Emulator
  Emulator command for running ntvcm (default: "ntvcm").

.PARAMETER SourcePath
    Optional explicit C source file path. When omitted, the driver searches the
    normal dcc test locations by app name.

.EXAMPLE
    powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 triangle
    powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 sieve nopeep
    powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 cobint -Mode fast -BuildDir mybuild

.NOTES
  Environment Variables:
    DCC              dcc compiler (default: "dcc")
    DCCPEEP          dccpeep optimizer (default: "dccpeep")
    DCCRTLSTRIP      runtime stripper (default: "dccrtlstrip")
    NTVCM            emulator (default: "ntvcm")
    M80              assembler (default: "m80")
    L80              linker (default: "l80")
    DCC_HOME         dcc package/install root; used to find include/, lib/, and CP/M tools
    DCC_INCLUDE      additional dcc include directories, separated by the host path separator
    DCC_LIB          additional runtime/tool asset roots, separated by the host path separator
    DCC_RUNTIME      explicit path to DCCRTL.MAC
    DCC_STACK_SIZE   C stack reserve in bytes (default: 512)
    DCC_FORCE_STACK_CHECK  enable -fstack-check for all apps
#>

param(
    [Parameter(Position = 0)]
    [string]$Name,

    [Parameter(Position = 1)]
    [ValidateSet("full", "fast", "peep", "nopeep", "opt", "optimized", "o", "noopt", "unopt", "u", "1", "0", "yes", "no", "true", "false")]
    [string]$Mode = "full",

    [string]$BuildDir = "build",
    [string]$Emulator = "ntvcm",
    [string]$SourcePath = ""
)

# CRLF conversion helper for M80. Reads as text, normalizes all line endings to
# LF, then emits CRLF without a BOM so M80 sees clean bytes.
function ConvertTo-CRLF {
    param([string]$FilePath)
    if (-not (Test-Path -LiteralPath $FilePath)) { return }
    # Resolve to an absolute filesystem path. The [System.IO.File] APIs below
    # resolve relative paths against [Environment]::CurrentDirectory, which is
    # NOT kept in sync with PowerShell's $PWD (e.g. when the caller cd'd into
    # the repo from another directory). Resolve via the PowerShell provider so
    # the path is anchored to $PWD, matching the Test-Path check above.
    $FilePath = (Resolve-Path -LiteralPath $FilePath).ProviderPath
    $text = [System.IO.File]::ReadAllText($FilePath)
    $text = $text -replace "`r`n", "`n"   # collapse existing CRLF to LF
    $text = $text -replace "`r", "`n"      # handle any lone CR
    $text = $text -replace "`n", "`r`n"    # convert all LF to CRLF
    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    [System.IO.File]::WriteAllText($FilePath, $text, $utf8NoBom)
}

# Build a single app through the full dcc -> dccpeep -> M80 -> dccrtlstrip ->
# M80 -> L80 pipeline. Returns $true on success (a .COM was produced), $false
# otherwise. Designed to be called in-process (dot-source this file first) so
# the test runner does not spawn a fresh pwsh per build.
function Invoke-MaBuild {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [string]$Mode = "fast",
        [string]$BuildDir = "build",
        [string]$Emulator = "ntvcm",
        [string]$SourcePath = "",
        [int]$StackSize = 0,
        [switch]$Quiet
    )

    function Write-Step {
        param([string]$Message, [string]$Color = "Gray")
        if (-not $Quiet) { Write-Host $Message -ForegroundColor $Color }
    }

    function Write-BuildError {
        param([string]$Message)
        Write-Error -Message $Message -ErrorAction Continue
    }

    # Normalize mode
    $modeLower = $Mode.ToLower()
    if ($modeLower -eq "full") {
        $fastOk = Invoke-MaBuild -Name $Name -Mode fast -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -Quiet:$Quiet
        $nopeepOk = Invoke-MaBuild -Name $Name -Mode nopeep -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath -StackSize $StackSize -Quiet:$Quiet
        return ($fastOk -and $nopeepOk)
    }
    $usePeep = @("fast", "peep", "opt", "optimized", "o", "1", "yes", "true") -contains $modeLower

    # Resolve app name
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Name)
    $lowerBase = $base.ToLower()
    $upperBase = $base.ToUpper()

    $sourceFile = ""
    if ($SourcePath) {
        if (Test-Path -LiteralPath $SourcePath -PathType Leaf) {
            $sourceFile = (Resolve-Path -LiteralPath $SourcePath).ProviderPath
        }
    }
    else {
        foreach ($candidate in @(
            (Join-Path "tests" "$base.c"),
            (Join-Path "tests" "$base.C"),
            (Join-Path "tests" "$lowerBase.c"),
            (Join-Path "tests" "$upperBase.C"),
            "$base.c",
            "$base.C",
            "$lowerBase.c",
            "$upperBase.C"
        )) {
            if (Test-Path $candidate -PathType Leaf) {
                $sourceFile = $candidate
                break
            }
        }
    }

    if (-not $sourceFile) {
        if ($SourcePath) {
            Write-BuildError "Source file not found: $SourcePath"
        }
        else {
            Write-BuildError "Source file not found for: $Name"
        }
        return $false
    }

    # Tool paths
    $DCC = $env:DCC -replace '^\s+|\s+$', ''
    if (-not $DCC) { $DCC = "dcc" }
    $DCCPEEP = $env:DCCPEEP -replace '^\s+|\s+$', ''
    if (-not $DCCPEEP) { $DCCPEEP = "dccpeep" }
    $DCCRTLSTRIP = $env:DCCRTLSTRIP -replace '^\s+|\s+$', ''
    if (-not $DCCRTLSTRIP) { $DCCRTLSTRIP = "dccrtlstrip" }
    $NTVCM = $env:NTVCM -replace '^\s+|\s+$', ''
    if (-not $NTVCM) { $NTVCM = $Emulator }
    $M80 = $env:M80 -replace '^\s+|\s+$', ''
    if (-not $M80) { $M80 = "m80" }
    $L80 = $env:L80 -replace '^\s+|\s+$', ''
    if (-not $L80) { $L80 = "l80" }

    function Add-UniquePath {
        param(
            [System.Collections.Generic.List[string]]$List,
            [string]$Path
        )

        $trimmedPath = $Path -replace '^\s+|\s+$', ''
        if (-not $trimmedPath) { return }

        $resolvedPath = $trimmedPath
        if (Test-Path -LiteralPath $trimmedPath) {
            $resolvedPath = (Resolve-Path -LiteralPath $trimmedPath).ProviderPath
        }

        if (-not $List.Contains($resolvedPath)) {
            $List.Add($resolvedPath) | Out-Null
        }
    }

    function Add-PathList {
        param(
            [System.Collections.Generic.List[string]]$List,
            [string]$Paths
        )

        if (-not $Paths) { return }
        foreach ($pathEntry in @($Paths -split [regex]::Escape([System.IO.Path]::PathSeparator))) {
            Add-UniquePath -List $List -Path $pathEntry
        }
    }

    # Ensure build directory exists
    if (-not (Test-Path $BuildDir -PathType Container)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    $assetRoots = New-Object 'System.Collections.Generic.List[string]'
    Add-UniquePath -List $assetRoots -Path (Get-Location).Path

    $dccHome = $env:DCC_HOME -replace '^\s+|\s+$', ''
    if ($dccHome) {
        Add-UniquePath -List $assetRoots -Path $dccHome
        Add-UniquePath -List $assetRoots -Path (Join-Path $dccHome "lib")
    }

    Add-PathList -List $assetRoots -Paths $env:DCC_LIB

    $scriptAssetRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
    Add-UniquePath -List $assetRoots -Path $scriptAssetRoot
    Add-UniquePath -List $assetRoots -Path (Join-Path $scriptAssetRoot "lib")

    $includeDirs = New-Object 'System.Collections.Generic.List[string]'
    Add-PathList -List $includeDirs -Paths $env:DCC_INCLUDE
    if ($dccHome) { Add-UniquePath -List $includeDirs -Path (Join-Path $dccHome "include") }
    foreach ($assetRoot in $assetRoots) {
        $candidateIncludeDir = Join-Path $assetRoot "include"
        if (Test-Path -LiteralPath $candidateIncludeDir -PathType Container) {
            Add-UniquePath -List $includeDirs -Path $candidateIncludeDir
        }
        elseif (Test-Path -LiteralPath (Join-Path $assetRoot "stdio.h") -PathType Leaf) {
            Add-UniquePath -List $includeDirs -Path $assetRoot
        }
    }

    # Stage tool COM files
    foreach ($toolFile in @("m80.com", "l80.com")) {
        foreach ($assetRoot in $assetRoots) {
            $toolPath = Join-Path $assetRoot $toolFile
            if (Test-Path $toolPath) {
                Copy-Item -Path $toolPath -Destination (Join-Path $BuildDir $toolFile) -Force -ErrorAction SilentlyContinue
                break
            }
        }
    }

    # Build artifact paths
    $appMac = Join-Path $BuildDir "$upperBase.MAC"
    $appRel = Join-Path $BuildDir "$upperBase.REL"
    $appCom = Join-Path $BuildDir "$upperBase.COM"
    $peepTmp = Join-Path $BuildDir "_PEEPOUT.MAC"
    $rtlSrc = Join-Path $BuildDir "DCCRTL.MAC"
    $rtlMin = Join-Path $BuildDir "RTLMIN.MAC"
    $rtlMinRel = Join-Path $BuildDir "RTLMIN.REL"
    $rootRtlSrc = $null

    $explicitRuntime = $env:DCC_RUNTIME -replace '^\s+|\s+$', ''
    if ($explicitRuntime) {
        if (Test-Path -LiteralPath $explicitRuntime -PathType Leaf) {
            $rootRtlSrc = (Resolve-Path -LiteralPath $explicitRuntime).ProviderPath
        }
        else {
            Write-BuildError "Runtime not found from DCC_RUNTIME: $explicitRuntime"
            return $false
        }
    }

    if (-not $rootRtlSrc) {
        foreach ($assetRoot in $assetRoots) {
            $candidateRtlSrc = Join-Path $assetRoot "DCCRTL.MAC"
            if (Test-Path $candidateRtlSrc) {
                $rootRtlSrc = $candidateRtlSrc
                break
            }
        }
    }

    if (-not $rootRtlSrc) {
        Write-BuildError "Runtime not found: DCCRTL.MAC"
        return $false
    }

    # Detect floatio / longio requirements
    $dccFloatio = 0
    $dccLongio = 0
    $sourceContent = Get-Content -Path $sourceFile -Raw
    if ($sourceContent -match '%[-+ #0-9.*]*[fF]') {
        $dccFloatio = 1
    }
    if ($sourceContent -match '%[-+ #0-9.*]*l[duxXs]') {
        $dccLongio = 1
    }

    # Detect/enable stack check
    $dccStackChk = ""
    if ($env:DCC_FORCE_STACK_CHECK -eq "1" -or $sourceContent -match 'DCC_STACK_CHECK') {
        $dccStackChk = "-fstack-check"
    }

    # Clean old artifacts. This deletes every file this build is about to
    # (re)create BEFORE running any tool, so a failed compile/assemble/link
    # cannot leave a stale artifact from a previous build that would be run or
    # verified as if the build had succeeded. The lowercase convenience copy is
    # included so it cannot linger on case-sensitive filesystems.
    Remove-Item -Path @($appMac, $appRel, $appCom, (Join-Path $BuildDir "$lowerBase.com"), (Join-Path $BuildDir "$upperBase.PRN"), $peepTmp, $rtlSrc, $rtlMin, $rtlMinRel, (Join-Path $BuildDir "RTLMIN.PRN")) -Force -ErrorAction SilentlyContinue

    # Determine stack size: explicit parameter wins, then env var, then default.
    $dccStackSize = if ($StackSize -gt 0) { "$StackSize" }
                    elseif ($env:DCC_STACK_SIZE) { $env:DCC_STACK_SIZE }
                    else { "512" }

    # Compile to .MAC
    $dccArgs = @($dccStackChk, "-stack", $dccStackSize)
    if ($dccFloatio -eq 1) { $dccArgs += "-ffloatio" }
    if ($dccLongio  -eq 1) { $dccArgs += "-flongio"  }
    foreach ($includeDir in $includeDirs) { $dccArgs += @("-I", $includeDir) }
    $dccArgs += @($sourceFile, "-o", $appMac)

    Write-Step "  Compiling with: $DCC $($dccArgs -join ' ')"
    $dccOut = & $DCC @($dccArgs | Where-Object { $_ }) 2>&1
    $dccExit = $LASTEXITCODE
    if (-not $Quiet) { $dccOut | Write-Host }

    if ($dccExit -ne 0) {
        Write-BuildError "Compilation failed for $Name (dcc exit code $dccExit)"
        return $false
    }

    if (-not (Test-Path $appMac)) {
        Write-BuildError "Compilation failed, no .MAC produced for $Name"
        return $false
    }

    # Run peephole optimizer if requested
    if ($usePeep) {
        Write-Step "  Optimizing with dccpeep..."
        $peepOut = & $DCCPEEP "$appMac" "$peepTmp" 2>&1
        $peepExit = $LASTEXITCODE
        if (-not $Quiet) { $peepOut | Write-Host }
        if ($peepExit -ne 0 -or -not (Test-Path $peepTmp)) {
            Write-BuildError "Peephole optimization failed for $Name (dccpeep exit code $peepExit)"
            return $false
        }
        Move-Item -Path $peepTmp -Destination $appMac -Force
    }

    # Convert to CRLF for M80
    ConvertTo-CRLF $appMac

    # Assemble app
    Write-Step "  Assembling $upperBase.MAC..."
    Push-Location $BuildDir
    $m80Out = & $NTVCM "$M80" "=$upperBase.MAC" "/X" "/O" "/Z" "/L" 2>&1
    Pop-Location
    if (-not $Quiet) { $m80Out | Write-Host }

    # The .REL was deleted above, so its absence now means M80 failed to
    # assemble the app (ntvcm always exits 0, so artifact presence is the
    # reliable signal that this stage produced output).
    if (-not (Test-Path $appRel)) {
        Write-BuildError "Assembly failed: no .REL produced for $Name"
        return $false
    }

    # Strip runtime
    Write-Step "  Stripping runtime..."
    Copy-Item -Path $rootRtlSrc -Destination $rtlSrc -Force
    ConvertTo-CRLF $rtlSrc

    $stripArgs = @()
    if ($dccFloatio -eq 1) { $stripArgs += @("-k", "_pffio") }
    if ($dccLongio  -eq 1) { $stripArgs += @("-k", "_pflng") }
    $stripArgs += @("-r", $rtlSrc, "-o", $rtlMin, $appMac)

    $stripOut = & $DCCRTLSTRIP @stripArgs 2>&1
    if (-not $Quiet) { $stripOut | Write-Host }

    ConvertTo-CRLF $rtlMin

    # Assemble runtime and link
    Write-Step "  Assembling RTLMIN.MAC and linking..."
    Push-Location $BuildDir
    $rtlOut = & $NTVCM "$M80" "=RTLMIN.MAC" "/X" "/O" "/Z" 2>&1
    $linkOut = & $NTVCM "$L80" "/P:100,RTLMIN,$upperBase,$upperBase/N/E" 2>&1
    Pop-Location
    if (-not $Quiet) { $rtlOut | Write-Host; $linkOut | Write-Host }

    # RTLMIN.REL was deleted above; its absence means the runtime failed to
    # assemble, which would leave the link reading nothing (or stale) input.
    if (-not (Test-Path $rtlMinRel)) {
        Write-BuildError "Runtime assembly failed: RTLMIN.REL not produced for $Name"
        return $false
    }

    # Create lowercase convenience copy
    $lowerCom = Join-Path $BuildDir "$lowerBase.com"
    if ((Test-Path $appCom) -and $lowerBase -ne $upperBase) {
        if (-not (Test-Path $lowerCom) -or ((Get-Item $appCom).FullName -ne (Get-Item $lowerCom).FullName)) {
            Copy-Item -Path $appCom -Destination $lowerCom -Force -ErrorAction SilentlyContinue
        }
    }

    # Treat M80/L80 warnings as errors (e.g. %Mult. Def. Global from 6-char name collisions)
    $allBuildOut = @($m80Out) + @($rtlOut) + @($linkOut)
    $buildWarnings = $allBuildOut | Where-Object { $_ -match '%Mult\. Def\.|%Phase error|%Undefined' }
    if ($buildWarnings) {
        $buildWarnings | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
        Write-BuildError "Build warnings treated as errors for $Name"
        return $false
    }

    if (Test-Path $appCom) {
        Write-Step "  Build successful: $appCom" "Green"
        return $true
    }
    else {
        Write-BuildError "Build failed: .COM file not produced for $Name"
        return $false
    }
}

# When executed directly (not dot-sourced), run the build with the script
# parameters. Dot-sourcing leaves InvocationName as "." and skips this block,
# exposing Invoke-MaBuild and ConvertTo-CRLF to the caller.
if ($MyInvocation.InvocationName -ne '.') {
    if (-not $Name) {
        Write-Error "Name is required. Usage: ma.ps1 <name> [full|fast|nopeep]"
        exit 1
    }
    $ok = Invoke-MaBuild -Name $Name -Mode $Mode -BuildDir $BuildDir -Emulator $Emulator -SourcePath $SourcePath
    if (-not $ok) { exit 1 }
}
