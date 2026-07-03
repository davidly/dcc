<#
.SYNOPSIS
Publish or republish the dcc binary package release.

.DESCRIPTION
Reads the package version from scripts/package-version.txt by default, deletes
an existing GitHub release and tag for that version if present, recreates the
tag at the current commit, and pushes it. The release.yml workflow rebuilds and
publishes package assets from the pushed tag.

.EXAMPLE
  pwsh ./scripts/publish-package.ps1

.EXAMPLE
  pwsh ./scripts/publish-package.ps1 -Version v2.0.1 -Watch
#>

param(
    [string]$Version = "",
    [string]$VersionFile = (Join-Path $PSScriptRoot "package-version.txt"),
    [string]$Remote = "origin",
    [string]$Workflow = "release.yml",
    [switch]$AllowDirty,
    [switch]$NoPushBranch,
    [switch]$Watch
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [string[]]$Arguments = @()
    )

    Write-Host "+ $Command $($Arguments -join ' ')"
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

function Test-CommandExists {
    param([string]$Command)
    return $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

foreach ($command in @("git", "gh")) {
    if (-not (Test-CommandExists $command)) {
        throw "Required command not found on PATH: $command"
    }
}

$repoProbe = & git -C (Join-Path $PSScriptRoot "..") rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0 -or -not $repoProbe) {
    throw "Could not find the repository root."
}
$repoRoot = $repoProbe.Trim()
Set-Location $repoRoot

if (-not $Version) {
    if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
        throw "Version file not found: $VersionFile"
    }
    $Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
}
$Version = $Version.Trim()

if ($Version -notmatch '^v\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$') {
    throw "Package version must look like v2.0.0; got '$Version'."
}

$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $branch) {
    throw "Could not determine the current git branch."
}
if ($branch -eq "HEAD" -and -not $NoPushBranch) {
    throw "Repository is in detached HEAD state. Use -NoPushBranch or check out a branch."
}

$dirty = & git status --porcelain
if ($dirty -and -not $AllowDirty) {
    Write-Host "Uncommitted changes:" -ForegroundColor Yellow
    $dirty | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    throw "Commit or stash changes before publishing, or pass -AllowDirty."
}

Write-Host "Publishing package version: $Version"
Write-Host "Repository root: $repoRoot"
Write-Host "Current branch: $branch"

Invoke-Checked "git" @("fetch", $Remote, "--tags")

if (-not $NoPushBranch) {
    Invoke-Checked "git" @("push", $Remote, $branch)
}

& gh release view $Version *> $null
if ($LASTEXITCODE -eq 0) {
    Write-Host "Existing GitHub release found for $Version; deleting release and remote tag."
    Invoke-Checked "gh" @("release", "delete", $Version, "--cleanup-tag", "--yes")
}
else {
    Write-Host "No GitHub release found for $Version."
}

$remoteTag = & git ls-remote --tags $Remote "refs/tags/$Version"
if ($LASTEXITCODE -ne 0) {
    throw "Could not query remote tag refs/tags/$Version."
}
if ($remoteTag) {
    Write-Host "Remote tag $Version exists without a release; deleting it."
    Invoke-Checked "git" @("push", $Remote, ":refs/tags/$Version")
}

$localTag = & git tag --list $Version
if ($LASTEXITCODE -ne 0) {
    throw "Could not query local tag $Version."
}
if ($localTag) {
    Invoke-Checked "git" @("tag", "-d", $Version)
}

Invoke-Checked "git" @("tag", $Version)
Invoke-Checked "git" @("push", $Remote, "refs/tags/$Version")

Write-Host "Release workflow should start from the pushed tag. Recent runs:"
Invoke-Checked "gh" @("run", "list", "--workflow", $Workflow, "--limit", "5")

if ($Watch) {
    Invoke-Checked "gh" @("run", "watch")
}
else {
    Write-Host "Watch with: gh run watch"
}