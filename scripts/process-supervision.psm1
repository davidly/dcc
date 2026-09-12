#Requires -Version 7
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "process-supervisor.ps1")

# Environment is inherited unless the caller explicitly overrides/removes keys.
# DCC_PROCESS_SCOPE is private bookkeeping for explicitly linked nested scopes.
function Start-SupervisedProcess(
    [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory,
    [string]$LogPath = "", [hashtable]$Environment = @{},
    [string[]]$RemoveEnvironment = @(), [string]$ParentScope = "",
    [ValidateRange(0.01, 86400)][double]$DrainTimeoutSeconds = 5
) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Get-Process -Id $PID).Path
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($name in $RemoveEnvironment) { [void]$start.Environment.Remove($name) }
    foreach ($name in $Environment.Keys) {
        if ($null -eq $Environment[$name]) {
            [void]$start.Environment.Remove($name)
        } else { $start.Environment[$name] = $Environment[$name] }
    }
    $scopeParent = if ($ParentScope) {
        Join-Path $ParentScope "children"
    } else { Join-Path $WorkingDirectory "scratch" }
    $scope = Join-Path $scopeParent ("process-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $scope -Force | Out-Null
    $jobName = "Local\dcc-process-" + [guid]::NewGuid()
    $job = [IntPtr]::Zero
    $start.Environment["DCC_PROCESS_SCOPE"] = $scope
    $request = Join-Path $scope "request.json"
    [ordered]@{
        FilePath = $FilePath; Arguments = @($Arguments)
        JobName = $jobName; ScopePath = $scope
    } | ConvertTo-Json | Set-Content -LiteralPath $request
    foreach ($argument in @("-NoLogo", "-NoProfile", "-NonInteractive", "-File",
        (Join-Path $PSScriptRoot "process-supervisor.ps1"), "-RequestPath", $request)) {
        $start.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $launched = $false
    $registered = $false
    try {
        if ($IsWindows) { $job = [DccProcessScope]::CreateJob($jobName) }
        if (-not $process.Start()) { throw "Failed to start $FilePath" }
        $launched = $true
        [System.IO.File]::WriteAllText((Join-Path $scope "process-id"), "$($process.Id)")
        $registered = $true
    } finally {
        if (-not $registered) {
            if ($launched) {
                if (-not $process.HasExited) {
                    $process.Kill($true)
                    [void]$process.WaitForExit(1000)
                }
                if (-not $IsWindows) {
                    [DccProcessScope]::TerminateUnix(
                        $process.Id, (Test-Path -LiteralPath (Join-Path $scope "ready")))
                }
            }
            if ($job -ne [IntPtr]::Zero) { [DccProcessScope]::TerminateJob($job) }
            $process.Dispose()
            if ($job -ne [IntPtr]::Zero) { [void][DccProcessScope]::CloseHandle($job) }
            Remove-Item -LiteralPath $scope -Recurse -Force
        }
    }
    [pscustomobject]@{
        Process = $process; LogPath = $LogPath; Clock = $clock
        ScopePath = $scope; Job = $job; Completed = $false
        DrainTimeoutSeconds = $DrainTimeoutSeconds; DrainDeadlineSeconds = $null
        Stdout = $process.StandardOutput.ReadToEndAsync()
        Stderr = $process.StandardError.ReadToEndAsync()
    }
}

# TimeoutSeconds is an overall deadline measured from Start-SupervisedProcess.
# A separate grace begins when the leader exits while descendants retain pipes.
function Test-SupervisedProcessComplete(
    $Command, [double]$TimeoutSeconds = [double]::PositiveInfinity
) {
    if ($Command.Process.HasExited) {
        if ($Command.Stdout.IsCompleted -and $Command.Stderr.IsCompleted) { return $true }
        if ($null -eq $Command.DrainDeadlineSeconds) {
            $Command.DrainDeadlineSeconds =
                $Command.Clock.Elapsed.TotalSeconds + $Command.DrainTimeoutSeconds
        }
    }
    return $Command.Clock.Elapsed.TotalSeconds -ge $TimeoutSeconds -or
        ($null -ne $Command.DrainDeadlineSeconds -and
         $Command.Clock.Elapsed.TotalSeconds -ge $Command.DrainDeadlineSeconds)
}

# TimeoutSeconds bounds this call's exit+drain wait. Cleanup has a separate,
# bounded one-second grace; no task result is accessed before completion.
function Complete-SupervisedProcess($Command, [double]$TimeoutSeconds = 60) {
    try {
        $completion = [System.Diagnostics.Stopwatch]::StartNew()
        while ($completion.Elapsed.TotalSeconds -lt $TimeoutSeconds -and
               -not (Test-SupervisedProcessComplete $Command)) {
            Start-Sleep -Milliseconds 10
        }
        # Zero budget is a nonblocking collection: an already-complete result
        # remains valid, while any unfinished exit/drain is cancelled below.
        $timedOut = -not $Command.Process.HasExited -or
            -not $Command.Stdout.IsCompletedSuccessfully -or
            -not $Command.Stderr.IsCompletedSuccessfully
        if (-not $Command.Process.HasExited) {
            try {
                $Command.Process.Kill($true)
            } catch [System.InvalidOperationException] {
                if (-not $Command.Process.HasExited) { throw }
            }
        }
        if ($Command.Job -ne [IntPtr]::Zero) {
            [DccProcessScope]::TerminateJob($Command.Job)
        } else {
            # Nested supervisors have independent sessions. Registration also
            # lets an outer watchdog stop them after their parent has exited.
            foreach ($path in @(Get-ChildItem -LiteralPath $Command.ScopePath -Recurse -Filter "process-id")) {
                try {
                    $processId = [int][System.IO.File]::ReadAllText($path.FullName)
                } catch [System.IO.FileNotFoundException] {
                    continue # A completed nested command already cleaned its scope.
                } catch [System.IO.DirectoryNotFoundException] {
                    continue
                }
                $ready = Test-Path -LiteralPath (Join-Path $path.DirectoryName "ready")
                [DccProcessScope]::TerminateUnix($processId, $ready)
            }
        }
        $cleanup = [System.Diagnostics.Stopwatch]::StartNew()
        while ($cleanup.ElapsedMilliseconds -lt 1000 -and
               (-not $Command.Process.HasExited -or
                -not $Command.Stdout.IsCompleted -or -not $Command.Stderr.IsCompleted)) {
            Start-Sleep -Milliseconds 10
        }
        $text = ""
        if ($Command.Stdout.IsCompletedSuccessfully) { $text += $Command.Stdout.Result }
        if ($Command.Stderr.IsCompletedSuccessfully) { $text += $Command.Stderr.Result }
        if ($Command.LogPath) { [System.IO.File]::WriteAllText($Command.LogPath, $text) }
        [pscustomobject]@{
            ExitCode = if ($Command.Process.HasExited) { $Command.Process.ExitCode } else { -1 }
            TimedOut = $timedOut; Output = $text
        }
    } finally {
        try {
            if ($Command.Job -ne [IntPtr]::Zero) { [void][DccProcessScope]::CloseHandle($Command.Job) }
            $Command.Process.StandardOutput.Dispose()
            $Command.Process.StandardError.Dispose()
            $Command.Process.Dispose()
            Remove-Item -LiteralPath $Command.ScopePath -Recurse -Force
        } finally {
            $Command.Completed = $true
        }
    }
}

function Stop-SupervisedProcess($Command) {
    if (-not $Command.Completed) { Complete-SupervisedProcess $Command 0 | Out-Null }
}

Export-ModuleMember -Function Start-SupervisedProcess, Test-SupervisedProcessComplete,
    Complete-SupervisedProcess, Stop-SupervisedProcess
