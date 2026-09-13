#Requires -Version 7
param(
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$IncludeDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$MachineMutation,
    [Parameter(Mandatory)][string]$ExpectedReject,
    [Parameter(Mandatory)][string]$ExpectedFailure
)

$ErrorActionPreference = "Stop"
$environment = @{
    DCC_MIR_MACHINE_MUTATE = $MachineMutation
    DCC_MIR_MACHINE_MUTATE_FUNCTION = "main"
}
foreach ($name in $environment.Keys) {
    [Environment]::SetEnvironmentVariable(
        $name, $environment[$name], "Process")
}
try {
    $output = & $Compiler "-I" $IncludeDirectory "-fstack-check" "-c" `
        $Source "-o" $OutputPath 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
} finally {
    foreach ($name in $environment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }
}
$output | Write-Host -NoNewline
if ($exitCode -ne 0 -or -not (Test-Path -LiteralPath $OutputPath)) {
    Write-Host "Matcher restoration compiler probe failed"
    exit 2
}
$escapedReject = [regex]::Escape($ExpectedReject)
$rejected = $output -cmatch (
    "(?m)^; MIR machine function=main " +
    "template=allocation-lifetime-runner reject=$escapedReject\r?$")
$generic = $output -cmatch (
    "(?m)^; MIR selection function=main " +
    "selector=spilled-scalar-cfg result=mir(?: .*)?\r?$")
$accepted = $output -cmatch (
    "(?m)^; MIR machine function=main " +
    "template=allocation-lifetime-runner accept=emitted\r?$")
$scheduled = $output -cmatch (
    "(?m)^; MIR selection function=main " +
    "selector=scheduled-machine-cfg result=mir(?: .*)?\r?$")
if ($rejected -and $generic -and -not $accepted -and -not $scheduled) {
    Write-Host "MIR matcher restoration failures=0"
    exit 0
}
if ($accepted -and $scheduled -and -not $rejected -and -not $generic) {
    Write-Host $ExpectedFailure
    Write-Host "MIR matcher restoration failures=1"
    exit 1
}
Write-Host "Matcher restoration probe produced unrelated selection evidence"
exit 2
