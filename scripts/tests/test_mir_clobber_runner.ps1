#Requires -Version 7
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).ProviderPath
. (Join-Path $repoRoot "scripts/mir-clobber-runner.ps1")
$root = Join-Path $repoRoot ("build/clobber-runner-unit-" + [guid]::NewGuid())
$checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    ++$script:checks
}

function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $caught = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $caught = $true
    }
    Assert-True $caught "Expected failure matching '$Pattern'"
}

function Write-Campaign($Data) {
    ConvertTo-Json -InputObject $Data -Depth 10 |
        Set-Content -LiteralPath $campaign -Encoding utf8
}

try {
    New-Item -ItemType Directory -Path "$root/scripts/mir-clobber-cases" -Force | Out-Null
    Set-Content -LiteralPath "$root/input.c" -Value "int main(void) { return 0; }"
    Set-Content -LiteralPath "$root/input.dat" -Value "fixture"
    Copy-Item -LiteralPath (Join-Path $repoRoot "scripts/run-mir-clobber-tests.ps1") `
        -Destination "$root/scripts"
    Copy-Item -LiteralPath (Join-Path $repoRoot "scripts/mir-clobber-runner.ps1") `
        -Destination "$root/scripts"
    $campaign = "$root/scripts/mir-clobber-cases/campaign.json"
    $definition = @{
        Name = "amfuture"; Group = "future"; Sources = @("input.c")
        FixturePaths = @("input.dat"); Defines = @("VALUE=4")
        Expected = @("passed"); Exit = 7; ExactTemplate = "shape"
        ExactFunction = "main"; RequireRejected = $true; RequireExact = $false
        MachineMutation = "1:src1:999"; MachineMutationFunction = "main"
        Args = @("one", "two words"); StackBytes = 1024
        StackModes = @($false); DebugModes = @("true", "lines")
        RequiredGenericFunction = "main"; RequiredSelectorFunction = "main"
        RequiredSelector = "spilled-scalar-cfg"; RequiredCandidate = "spilled-all"
        AssemblyPatterns = @("required"); ForbiddenAssemblyPatterns = @("forbidden")
        OddUpperRuntime = $false
    }
    Write-Campaign @($definition)
    $loaded = @(Import-MirClobberCases (Split-Path $campaign) $root @("existing"))
    Assert-True ($loaded.Count -eq 1) "Loader lost case"
    foreach ($name in $definition.Keys) {
        $expected = $definition[$name]
        if ($name -in @("Sources", "FixturePaths")) {
            $expected = @($definition[$name] | ForEach-Object { Join-Path $root $_ })
        }
        Assert-True (
            (ConvertTo-Json -InputObject $loaded[0].$name -Compress) -ceq
            (ConvertTo-Json -InputObject $expected -Compress)
        ) "Loader altered $name"
    }
    Assert-Throws {
        Import-MirClobberCases (Split-Path $campaign) $root @("AMFUTURE")
    } "Duplicate"
    foreach ($invalid in @(
        @{ Key = "Unknown"; Value = "ignored assertion" },
        @{ Key = "RequireRejected"; Value = "false" },
        @{ Key = "Sources"; Value = @("../outside.c") },
        @{ Key = "Sources"; Value = @("$root/input.c") },
        @{ Key = "Sources"; Value = "input.c" },
        @{ Key = "StackModes"; Value = @($false, $false) },
        @{ Key = "StackModes"; Value = @("false") },
        @{ Key = "DebugModes"; Value = @("lines", "lines") },
        @{ Key = "DebugModes"; Value = @("") },
        @{ Key = "StackBytes"; Value = 0 },
        @{ Key = "Exit"; Value = 1.5 },
        @{ Key = "Group"; Value = "existing" },
        @{ Key = "ExactTemplate"; Value = "" },
        @{ Key = "RequiredSelector"; Value = "" },
        @{ Key = "MachineMutationFunction"; Value = "" }
    )) {
        $bad = $definition.Clone()
        $bad[$invalid.Key] = $invalid.Value
        Write-Campaign @($bad)
        Assert-Throws {
            Import-MirClobberCases (Split-Path $campaign) $root @("existing")
        } "."
    }
    Write-Campaign $definition
    Assert-Throws {
        Import-MirClobberCases (Split-Path $campaign) $root @()
    } "JSON array"
    Write-Campaign @($definition, $definition)
    Assert-Throws {
        Import-MirClobberCases (Split-Path $campaign) $root @()
    } "Duplicate"
    Write-Campaign @($definition)

    Assert-MirClobberManifest @("a", "b") @("b", "a")
    Assert-MirClobberManifest @() @()
    Assert-Throws { Assert-MirClobberManifest @("a", "b") @("a", "a") } "duplicate"
    Assert-Throws { Assert-MirClobberManifest @("a", "a") @("a") } "duplicate"
    Assert-Throws { Assert-MirClobberManifest @("a", "b") @("a") } "missing"
    Assert-Throws { Assert-MirClobberManifest @("a", "b") @("a", "c") } "unexpected"
    Assert-Throws { Get-MirClobberShard @("a") 2 2 } "Invalid"
    $plan = [System.Collections.Generic.Dictionary[string, object]]::new()
    $parameters = @{Name = "example"; StackBytes = 1024}
    Add-MirClobberExecution $plan "example|nostack-peep" "Assert-RunCase" $parameters @()
    $parameters.StackBytes = 1
    Assert-True ($plan["example|nostack-peep"].Parameters.StackBytes -eq 1024) `
        "Inventory parameters were not captured"
    Assert-Throws {
        Add-MirClobberExecution $plan "example|nostack-peep" "Assert-RunCase" @{} @()
    } "duplicate"

    $runner = "$root/scripts/run-mir-clobber-tests.ps1"
    $inventory = "$root/inventory.json"
    & $runner -Cases future -ListExecutions $inventory -Emulator deliberately-missing
    $keys = @(Get-Content -LiteralPath $inventory -Raw | ConvertFrom-Json)
    $expected = @(
        "amfuture|nostack-nopeep", "amfuture|nostack-nopeep-debug-lines",
        "amfuture|nostack-nopeep-debug-true", "amfuture|nostack-peep",
        "amfuture|nostack-peep-debug-lines", "amfuture|nostack-peep-debug-true"
    )
    Assert-MirClobberManifest $expected $keys
    $futurePlan = & {
        . $runner -Cases future -ListExecutions $inventory
        return ,$executionPlan
    }
    foreach ($leaf in $futurePlan.Values) {
        foreach ($name in $definition.Keys) {
            if ($name -in @("Group", "StackModes", "DebugModes")) { continue }
            $parameter = switch ($name) {
                "Exit" { "ExpectedExit" }
                "Args" { "RunArguments" }
                default { $name }
            }
            Assert-True (
                (ConvertTo-Json -InputObject $leaf.Parameters[$parameter] -Compress) -ceq
                (ConvertTo-Json -InputObject $loaded[0].$name -Compress)
            ) "Execution $($leaf.Key) dropped campaign parameter $name"
        }
    }
    Assert-True (-not (Test-Path -LiteralPath "$root/build")) `
        "Inventory-only mode created execution artifacts"
    $union = @()
    for ($i = 0; $i -lt 8; ++$i) {
        & $runner -Cases future -ListExecutions $inventory -ShardIndex $i -ShardCount 8
        $shard = ConvertFrom-Json -InputObject (Get-Content $inventory -Raw) -NoEnumerate
        Assert-True ($shard -is [array]) "Shard manifest must remain an array when empty/singleton"
        Assert-MirClobberManifest @(Get-MirClobberShard $expected $i 8) $shard
        $union += $shard
    }
    Assert-MirClobberManifest $expected $union
    & $runner -Cases allocmut -ListExecutions $inventory
    $keys = @(Get-Content $inventory -Raw | ConvertFrom-Json)
    Assert-True ($keys.Count -eq 476 -and -not ($keys -match '^amfuture\|')) `
        "Historical prefix alias captured a new ungrouped campaign"
    Assert-Throws { & $runner -Cases unknown -ListExecutions $inventory } "Unknown"
    Assert-Throws { & $runner -Jobs 2 -ShardCount 2 -ListExecutions $inventory } "cannot"

    # Freeze the existing dispatch inventory independently of future campaign files.
    Remove-Item -LiteralPath $campaign
    & $runner -ListExecutions $inventory -Emulator deliberately-missing
    $keys = @(Get-Content $inventory -Raw | ConvertFrom-Json)
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes(($keys -join "`n") + "`n"))).ToLowerInvariant()
    Assert-True ($keys.Count -eq 5052 -and
        $digest -eq "8fdb6d9a5466b8abcec6c5615ff89dc1f1547f883b9688fbca26a0f0001ee861") `
        "Frozen built-in leaf inventory changed"
    $legacyPlan = & {
        . $runner -Cases ptrcond -ListExecutions $inventory
        return ,$executionPlan
    }
    Assert-True (
        $legacyPlan["ptrcond|stack-peep"].Parameters.RequireExact -and
        -not $legacyPlan["ptrcond|stack-peep-debug-true"].Parameters.ContainsKey("RequireExact")
    ) "Built-in full-debug contract changed"

    $worker = "$root/worker.ps1"
    @'
param($ShardIndex, $ShardCount, $ExecutionManifest, $Helper, $Behavior, $Scratch)
$ErrorActionPreference = "Stop"
. $Helper
if ($env:MIR_CLOBBER_PROCESS_TEST -ne "child") { throw "child environment not isolated" }
if ($Behavior -eq "error") { throw "deliberate child error" }
$keys = @(Get-MirClobberShard @("a", "b", "c", "d") $ShardIndex $ShardCount)
if ($ShardIndex -eq 0) {
    switch ($Behavior) {
        "duplicate" { $keys += $keys[0] }
        "missing" { $keys = @() }
        "unexpected" { $keys[0] = "unexpected" }
        "no-manifest" { exit 0 }
        "malformed" { Set-Content -LiteralPath $ExecutionManifest -Value '{"not":"an array"}'; exit 0 }
    }
}
if ($Behavior -eq "swapped") {
    $keys = @(Get-MirClobberShard @("a", "b", "c", "d") (1 - $ShardIndex) $ShardCount)
}
Write-Host "worker-pid=$PID"
Write-Host "worker-profile=$env:LLVM_PROFILE_FILE"
Write-MirClobberManifest $ExecutionManifest $keys $Scratch
'@ | Set-Content -LiteralPath $worker
    $childParameters = @{
        Helper = (Join-Path $repoRoot "scripts/mir-clobber-runner.ps1")
        Behavior = "success"; Scratch = $root
    }
    $old = [Environment]::GetEnvironmentVariable("MIR_CLOBBER_PROCESS_TEST", "Process")
    $oldProfile = [Environment]::GetEnvironmentVariable("LLVM_PROFILE_FILE", "Process")
    try {
        $env:MIR_CLOBBER_PROCESS_TEST = "parent"
        $env:LLVM_PROFILE_FILE = "$root/raw/dcc-%8m.profraw"
        $actual = @(Invoke-MirClobberShards $worker $childParameters 2 `
            @("a", "b", "c", "d") $root $repoRoot @{ MIR_CLOBBER_PROCESS_TEST = "child" })
        Assert-MirClobberManifest @("a", "b", "c", "d") $actual
        Assert-True ($env:MIR_CLOBBER_PROCESS_TEST -eq "parent") "Child changed parent environment"
        $pids = @(0..1 | ForEach-Object {
            [regex]::Match((Get-Content "$root/shard-$_.log" -Raw), 'worker-pid=(\d+)').Groups[1].Value
        })
        Assert-True ($pids[0] -and $pids[1] -and $pids[0] -ne $pids[1] -and
            "$PID" -notin $pids) "Shards did not use independent child processes"
        $profiles = @(0..1 | ForEach-Object {
            [regex]::Match((Get-Content "$root/shard-$_.log" -Raw),
                'worker-profile=([^\r\n]+)').Groups[1].Value
        })
        Assert-True ($profiles[0] -ne $profiles[1]) "Shards shared profile filenames"
        foreach ($profile in $profiles) {
            Assert-True (
                [System.IO.Path]::GetFullPath([System.IO.Path]::GetDirectoryName($profile)) -eq
                    [System.IO.Path]::GetFullPath("$root/raw") -and
                [System.IO.Path]::GetFileName($profile) -match '^dcc-%8m-clobber-\d+-[01]\.profraw$'
            ) "Shard profile escaped the inherited raw directory or changed its LLVM pattern"
        }
        Assert-True ($env:LLVM_PROFILE_FILE -eq "$root/raw/dcc-%8m.profraw") `
            "Shards changed parent profile configuration"
        foreach ($behavior in @(
            "duplicate", "missing", "unexpected", "error", "no-manifest", "malformed", "swapped"
        )) {
            Remove-Item "$root/shard-*.json"
            $childParameters.Behavior = $behavior
            Assert-Throws {
                Invoke-MirClobberShards $worker $childParameters 2 `
                    @("a", "b", "c", "d") $root $repoRoot @{ MIR_CLOBBER_PROCESS_TEST = "child" }
            } "."
        }
    } finally {
        [Environment]::SetEnvironmentVariable("MIR_CLOBBER_PROCESS_TEST", $old, "Process")
        [Environment]::SetEnvironmentVariable("LLVM_PROFILE_FILE", $oldProfile, "Process")
    }
    Write-Host "MIR clobber runner harness passed $checks checks"
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
