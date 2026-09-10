#Requires -Version 7
param(
    [string]$OutputDirectory = "build/mir-compiler-mutations",
    [ValidateRange(1, 1024)][int]$Jobs = 1,
    [ValidateRange(1, 1024)][int]$BuildJobs = 2
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "mir-compiler-mutations.psm1") -Force
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath
$output = [System.IO.Path]::GetFullPath($OutputDirectory, $repoRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null
# Do not let two invocations overwrite each other's evidence.
$lock = [System.IO.File]::Open(
    (Join-Path $output ".lock"), [System.IO.FileMode]::OpenOrCreate,
    [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
$workspace = Join-Path $output ("work-" + [guid]::NewGuid())
$cases = @(Get-MirCompilerMutations)
$results = @($cases | ForEach-Object {
    [pscustomobject][ordered]@{
        mutation = $_.Name; outcome = "invalid"; phase = "not-run"
        exitCode = $null; detail = "Baseline must pass before mutants run"
    }
})
$active = [System.Collections.Generic.List[object]]::new()
$next = 0
$baselinePassed = $false
$pwsh = (Get-Process -Id $PID).Path
$worker = Join-Path $PSScriptRoot "run-mir-compiler-mutation-worker.ps1"
try {
    New-Item -ItemType Directory -Path $workspace | Out-Null
    foreach ($case in $cases) {
        $caseOutput = Join-Path $output $case.Name
        if (Test-Path -LiteralPath $caseOutput) {
            Remove-Item -LiteralPath $caseOutput -Recurse -Force
        }
    }
    while ($next -lt $cases.Count -or $active.Count -gt 0) {
        while ($next -lt $cases.Count -and $active.Count -lt $Jobs -and
               ($next -eq 0 -or $baselinePassed)) {
            $case = $cases[$next]
            $caseOutput = Join-Path $output $case.Name
            New-Item -ItemType Directory -Path $caseOutput -Force | Out-Null
            $resultPath = Join-Path $caseOutput "result.json"
            # Overwrite old results before launch, including on a failed launch.
            $results[$next].detail = "Worker did not complete"
            $results[$next] | ConvertTo-Json |
                Set-Content -LiteralPath $resultPath
            $command = Start-MirMutationProcess $pwsh @(
                "-NoLogo", "-NoProfile", "-NonInteractive", "-File", $worker,
                "-RepoRoot", $repoRoot,
                "-Workspace", (Join-Path $workspace $case.Name),
                "-OutputDirectory", $caseOutput,
                "-Name", $case.Name, "-BuildJobs", "$BuildJobs"
            ) $caseOutput (Join-Path $caseOutput "worker.log")
            $active.Add([pscustomobject]@{
                Index = $next; Command = $command; ResultPath = $resultPath
            })
            ++$next
        }
        if ($active.Count -eq 0) { break }
        $completed = @($active | Where-Object { $_.Command.Process.HasExited })
        if ($completed.Count -eq 0) {
            Start-Sleep -Milliseconds 100
            continue
        }
        foreach ($item in $completed) {
            [void]$active.Remove($item)
            $execution = Complete-MirMutationProcess $item.Command
            $result = $null
            if (Test-Path -LiteralPath $item.ResultPath -PathType Leaf) {
                try {
                    $result = Get-Content -LiteralPath $item.ResultPath -Raw | ConvertFrom-Json
                } catch [System.ArgumentException] {
                    # A truncated result is a worker error, not a reason to lose
                    # the remaining workers' outcomes.
                    $result = $null
                }
            }
            if ($null -eq $result -or $execution.ExitCode -ne 0 -or $execution.TimedOut -or
                $result.mutation -ne $cases[$item.Index].Name -or
                $result.outcome -notin @("passed", "killed", "survived", "invalid") -or
                ($item.Index -gt 0 -and $result.outcome -eq "passed")) {
                $results[$item.Index].phase = "worker"
                $results[$item.Index].exitCode = $execution.ExitCode
                $results[$item.Index].detail = "Worker failed; see worker.log"
            } else {
                $results[$item.Index] = $result
            }
            if ($item.Index -eq 0) {
                $baselinePassed = $results[0].outcome -eq "passed"
                if ($baselinePassed) {
                    foreach ($pending in $results | Select-Object -Skip 1) {
                        $pending.detail = "Worker was not scheduled"
                    }
                }
            }
        }
    }
} finally {
    foreach ($item in $active) {
        Stop-MirMutationProcess $item.Command
    }
    try {
        ConvertTo-Json -InputObject $results -Depth 4 |
            Set-Content -LiteralPath (Join-Path $output "results.json")
        if (Test-Path -LiteralPath $workspace) {
            Remove-Item -LiteralPath $workspace -Recurse -Force
        }
    } finally {
        $lock.Dispose()
    }
}
if (-not $baselinePassed -or
    @($results | Where-Object { $_.outcome -in @("survived", "invalid") }).Count -gt 0) {
    throw "Compiler mutation checks failed: $output/results.json"
}
Write-Host "Compiler mutation controls: baseline passed, $($cases.Count - 1) mutants killed"
exit 0
