#Requires -Version 7
<#
.SYNOPSIS
Create a native installer from a staged dcc release package.
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$PackageName,

    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true)]
    [string]$Target,

    [string]$OutputDir = "dist",
    [string]$Version = "0.0.0"
)

$ErrorActionPreference = "Stop"

$resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot).ProviderPath
$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    [System.IO.Path]::GetFullPath($OutputDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputDir))
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

function Get-InstallerVersion {
    param([string]$InputVersion)

    $match = [regex]::Match($InputVersion, '(\d+)\.(\d+)\.(\d+)')
    if ($match.Success) { return $match.Value }
    return "0.0.0"
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $Source -Force)) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

function New-UnixLinks {
    param(
        [string]$Root,
        [string]$Prefix,
        [string]$LinkDir
    )

    $binDir = Join-Path $Root $LinkDir.TrimStart("/")
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    foreach ($tool in @("dcc", "dccpeep", "dccrtlstrip", "ntvcm")) {
        New-Item -ItemType SymbolicLink -Path (Join-Path $binDir $tool) -Target "$Prefix/bin/$tool" -Force | Out-Null
    }

    $wrapper = @"
#!/usr/bin/env sh
export DCC_HOME="$Prefix"
export PATH="`$DCC_HOME/bin:`$PATH"
export DCC_INCLUDE="`$DCC_HOME/include`${DCC_INCLUDE:+:`$DCC_INCLUDE}"
export DCC_LIB="`$DCC_HOME/lib:`$DCC_HOME`${DCC_LIB:+:`$DCC_LIB}"
export DCC_RUNTIME="`${DCC_RUNTIME:-`$DCC_HOME/lib/DCCRTL.MAC}"
exec "$Prefix/scripts/ma.sh" "`$@"
"@
    $wrapperPath = Join-Path $binDir "dcc-build"
    Set-Content -LiteralPath $wrapperPath -Value $wrapper -Encoding utf8
    & chmod +x $wrapperPath

    $legacyWrapperPath = Join-Path $binDir "dcc-ma"
    New-Item -ItemType SymbolicLink -Path $legacyWrapperPath -Target "dcc-build" -Force | Out-Null
}

function New-UnixEnvironment {
    param(
        [string]$Root,
        [string]$Prefix
    )

    $envScript = @"
# dcc CP/M Z80 package environment
export DCC_HOME="$Prefix"
export DCC_INCLUDE="`$DCC_HOME/include`${DCC_INCLUDE:+:`$DCC_INCLUDE}"
export DCC_LIB="`$DCC_HOME/lib:`$DCC_HOME`${DCC_LIB:+:`$DCC_LIB}"
export DCC_RUNTIME="`${DCC_RUNTIME:-`$DCC_HOME/lib/DCCRTL.MAC}"
"@

    $profileDir = Join-Path $Root "etc/profile.d"
    New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
    $profilePath = Join-Path $profileDir "dcc-cpm-z80.sh"
    Set-Content -LiteralPath $profilePath -Value $envScript -Encoding utf8

    $prefixRoot = Join-Path $Root $Prefix.TrimStart("/")
    New-Item -ItemType Directory -Path $prefixRoot -Force | Out-Null
    $packageEnvPath = Join-Path $prefixRoot "dcc-env.sh"
    Set-Content -LiteralPath $packageEnvPath -Value $envScript -Encoding utf8
}

function New-DebPackage {
    $arch = if ($Target -like "*arm64") { "arm64" } else { "amd64" }
    $versionText = Get-InstallerVersion $Version
    $debRoot = Join-Path $outputRoot "$PackageName-debroot"
    if (Test-Path -LiteralPath $debRoot) { Remove-Item -LiteralPath $debRoot -Recurse -Force }

    $installPrefix = "/opt/dcc-cpm-z80"
    $payloadRoot = Join-Path $debRoot "opt/dcc-cpm-z80"
    Copy-DirectoryContents -Source $resolvedPackageRoot -Destination $payloadRoot
    New-UnixLinks -Root $debRoot -Prefix $installPrefix -LinkDir "/usr/bin"
    New-UnixEnvironment -Root $debRoot -Prefix $installPrefix

    $controlDir = Join-Path $debRoot "DEBIAN"
    New-Item -ItemType Directory -Path $controlDir -Force | Out-Null
    $control = @"
Package: dcc-cpm-z80
Version: $versionText
Section: devel
Priority: optional
Architecture: $arch
Maintainer: gloveboxes <noreply@github.com>
Provides: dcc-4-cpm-z80
Conflicts: dcc-4-cpm-z80
Replaces: dcc-4-cpm-z80
Description: dcc C compiler toolchain for CP/M 2.2 on Z80
 Includes dcc, dccpeep, dccrtlstrip, ntvcm, the DCC runtime, CP/M assembler/linker tools, standard-library headers, and helper scripts.
"@
    Set-Content -LiteralPath (Join-Path $controlDir "control") -Value $control -Encoding ascii

    $debPath = Join-Path $outputRoot "$PackageName.deb"
    & dpkg-deb --build --root-owner-group $debRoot $debPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "Created: $debPath"
}

function New-MacPackage {
    $versionText = Get-InstallerVersion $Version
    $pkgRoot = Join-Path $outputRoot "$PackageName-pkgroot"
    if (Test-Path -LiteralPath $pkgRoot) { Remove-Item -LiteralPath $pkgRoot -Recurse -Force }

    $installPrefix = "/usr/local/dcc-cpm-z80"
    $payloadRoot = Join-Path $pkgRoot "usr/local/dcc-cpm-z80"
    Copy-DirectoryContents -Source $resolvedPackageRoot -Destination $payloadRoot
    New-UnixLinks -Root $pkgRoot -Prefix $installPrefix -LinkDir "/usr/local/bin"
    New-UnixEnvironment -Root $pkgRoot -Prefix $installPrefix

    $pkgPath = Join-Path $outputRoot "$PackageName.pkg"
    & pkgbuild --root $pkgRoot --identifier "com.gloveboxes.dcc-cpm-z80" --version $versionText --install-location / $pkgPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host "Created: $pkgPath"
}

function ConvertTo-WixText {
    param([string]$Text)
    return [System.Security.SecurityElement]::Escape($Text)
}

function Get-WixCommandPath {
    $wixCommand = Get-Command wix -ErrorAction SilentlyContinue
    if ($wixCommand) { return $wixCommand.Source }

    $candidateRoots = @($env:USERPROFILE, $HOME) | Where-Object { $_ } | Select-Object -Unique
    foreach ($root in $candidateRoots) {
        foreach ($candidate in @(
            (Join-Path $root ".dotnet/tools/wix.exe"),
            (Join-Path $root ".dotnet/tools/wix")
        )) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }

    throw "WiX Toolset command 'wix' was not found. PATH: $env:PATH"
}

function New-MsiPackage {
    $versionText = Get-InstallerVersion $Version
    $wixCommandPath = Get-WixCommandPath
    Write-Host "WiX command: $wixCommandPath"
    & $wixCommandPath --version
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $componentIds = [System.Collections.Generic.List[string]]::new()
    $nextId = 1

    function New-WixId {
        param([string]$Prefix)
        $script:nextId += 1
        return "$Prefix$script:nextId"
    }

    function Add-WixDirectory {
        param(
            [string]$Path,
            [int]$Indent
        )

        $lines = [System.Collections.Generic.List[string]]::new()
        $pad = " " * $Indent

        foreach ($directory in @(Get-ChildItem -LiteralPath $Path -Directory | Sort-Object Name)) {
            $dirId = New-WixId "dir"
            $dirName = ConvertTo-WixText $directory.Name
            $lines.Add("$pad<Directory Id=`"$dirId`" Name=`"$dirName`">")
            foreach ($line in @(Add-WixDirectory -Path $directory.FullName -Indent ($Indent + 2))) { $lines.Add($line) }
            $lines.Add("$pad</Directory>")
        }

        foreach ($file in @(Get-ChildItem -LiteralPath $Path -File | Sort-Object Name)) {
            $componentId = New-WixId "cmp"
            $fileId = New-WixId "fil"
            $source = ConvertTo-WixText $file.FullName
            $name = ConvertTo-WixText $file.Name
            $componentIds.Add($componentId)
            $lines.Add("$pad<Component Id=`"$componentId`" Guid=`"*`">")
            $lines.Add("$pad  <File Id=`"$fileId`" Source=`"$source`" Name=`"$name`" KeyPath=`"yes`" />")
            $lines.Add("$pad</Component>")
        }

        return $lines.ToArray()
    }

    $script:nextId = $nextId
    $wxsPath = Join-Path $outputRoot "$PackageName.wxs"
    $msiPath = Join-Path $outputRoot "$PackageName.msi"
    $arch = if ($Target -like "*arm64") { "arm64" } else { "x64" }
    $upgradeCode = "{6B38A8A4-61F7-4D3F-8F7A-3B0220E86F40}"

    $wxs = [System.Collections.Generic.List[string]]::new()
    $wxs.Add('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
    $wxs.Add("  <Package Name=`"dcc CP/M Z80`" Manufacturer=`"gloveboxes`" Version=`"$versionText`" UpgradeCode=`"$upgradeCode`" Scope=`"perMachine`">")
    $wxs.Add('    <MajorUpgrade DowngradeErrorMessage="A newer version of dcc CP/M Z80 is already installed." />')
    $wxs.Add('    <MediaTemplate EmbedCab="yes" />')
    $wxs.Add('    <StandardDirectory Id="ProgramFiles64Folder">')
    $wxs.Add('      <Directory Id="INSTALLFOLDER" Name="dcc-cpm-z80">')
    foreach ($line in @(Add-WixDirectory -Path $resolvedPackageRoot -Indent 8)) { $wxs.Add($line) }
    $componentIds.Add("cmpPathEnvironment")
    $wxs.Add('        <Component Id="cmpPathEnvironment" Guid="*">')
    $wxs.Add('          <Environment Id="envBinPath" Name="Path" Value="[INSTALLFOLDER]bin" Action="set" Part="last" System="no" Permanent="no" />')
    $wxs.Add('          <Environment Id="envScriptsPath" Name="Path" Value="[INSTALLFOLDER]scripts" Action="set" Part="last" System="no" Permanent="no" />')
    $wxs.Add('          <Environment Id="envDccHome" Name="DCC_HOME" Value="[INSTALLFOLDER]" Action="set" System="no" Permanent="no" />')
    $wxs.Add('          <Environment Id="envDccInclude" Name="DCC_INCLUDE" Value="[INSTALLFOLDER]include" Action="set" System="no" Permanent="no" />')
    $wxs.Add('          <Environment Id="envDccLib" Name="DCC_LIB" Value="[INSTALLFOLDER]lib;[INSTALLFOLDER]" Action="set" System="no" Permanent="no" />')
    $wxs.Add('          <Environment Id="envDccRuntime" Name="DCC_RUNTIME" Value="[INSTALLFOLDER]lib\DCCRTL.MAC" Action="set" System="no" Permanent="no" />')
    $wxs.Add('          <RegistryValue Root="HKLM" Key="Software\gloveboxes\dcc-cpm-z80" Name="PathEnvironment" Type="integer" Value="1" KeyPath="yes" />')
    $wxs.Add('        </Component>')
    $wxs.Add('      </Directory>')
    $wxs.Add('    </StandardDirectory>')
    $wxs.Add('    <Feature Id="MainFeature" Title="dcc CP/M Z80" Level="1">')
    foreach ($componentId in $componentIds) { $wxs.Add("      <ComponentRef Id=`"$componentId`" />") }
    $wxs.Add('    </Feature>')
    $wxs.Add('  </Package>')
    $wxs.Add('</Wix>')
    Set-Content -LiteralPath $wxsPath -Value $wxs -Encoding utf8

    & $wixCommandPath build $wxsPath -arch $arch -out $msiPath
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Generated WiX source ($wxsPath):"
        Get-Content -LiteralPath $wxsPath | ForEach-Object { Write-Host $_ }
        exit $LASTEXITCODE
    }
    Write-Host "Created: $msiPath"
}

if ($IsWindows) {
    New-MsiPackage
} elseif ($IsMacOS) {
    New-MacPackage
} elseif ($IsLinux) {
    New-DebPackage
} else {
    throw "Unsupported installer host OS."
}