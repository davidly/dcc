#Requires -Version 7
param(
    [Parameter(Mandatory)][string]$RepoRoot,
    [Parameter(Mandatory)][string]$Workspace,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$Name,
    [ValidateRange(1, 1024)][int]$BuildJobs = 2
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "mir-compiler-mutations.psm1") -Force
Invoke-MirMutationWorker $RepoRoot $Workspace $OutputDirectory $Name $BuildJobs
