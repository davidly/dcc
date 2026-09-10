#Requires -Version 7
param([string]$RequestPath = "")

$ErrorActionPreference = "Stop"
if (-not ("DccProcessScope" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class DccProcessScope {
    [DllImport("libc", SetLastError = true)] static extern int setsid();
    [DllImport("libc", SetLastError = true)] static extern int kill(int pid, int signal);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr OpenJobObject(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int kind, ref Limits limits, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool TerminateJobObject(IntPtr job, uint code);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);

    [StructLayout(LayoutKind.Sequential)]
    struct BasicLimits {
        public long ProcessTime, JobTime;
        public uint Flags;
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
        public uint ActiveProcesses;
        public UIntPtr Affinity;
        public uint Priority, Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Counters {
        public ulong ReadOperations, WriteOperations, OtherOperations;
        public ulong ReadBytes, WriteBytes, OtherBytes;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Limits {
        public BasicLimits Basic;
        public Counters Io;
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
    }
    public static IntPtr CreateJob(string name) {
        IntPtr job = CreateJobObject(IntPtr.Zero, name);
        if (job == IntPtr.Zero) throw new Win32Exception();
        var limits = new Limits();
        limits.Basic.Flags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf<Limits>())) {
            int error = Marshal.GetLastWin32Error();
            CloseHandle(job);
            throw new Win32Exception(error);
        }
        return job;
    }
    public static void Enter(string jobName) {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) {
            IntPtr job = OpenJobObject(1, false, jobName); // JOB_OBJECT_ASSIGN_PROCESS
            if (job == IntPtr.Zero) throw new Win32Exception();
            try {
                if (!AssignProcessToJobObject(job, new IntPtr(-1)))
                    throw new Win32Exception();
            } finally { CloseHandle(job); }
        } else if (setsid() < 0) {
            throw new Win32Exception();
        }
    }
    public static void TerminateJob(IntPtr job) {
        if (!TerminateJobObject(job, 1)) throw new Win32Exception();
    }
    public static void TerminateUnix(int pid, bool ready) {
        // The group remains addressable after its leader exits. Before the
        // supervisor creates it, only the recorded bootstrap PID can exist.
        if (kill(ready ? -pid : pid, 9) < 0) {
            int error = Marshal.GetLastWin32Error();
            if (error != 3) throw new Win32Exception(error); // ESRCH: already gone
        }
    }
}
'@
}

if (-not $RequestPath) { return }
$request = Get-Content -LiteralPath $RequestPath -Raw | ConvertFrom-Json
[DccProcessScope]::Enter($request.JobName)
# Registration precedes target launch, including nested command supervisors.
[System.IO.File]::WriteAllText((Join-Path $request.ScopePath "ready"), "")
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $request.FilePath
$start.UseShellExecute = $false
foreach ($argument in $request.Arguments) { $start.ArgumentList.Add($argument) }
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    if (-not $process.Start()) { throw "Failed to start $($request.FilePath)" }
    # The caller owns the deadline and the job/process group. Inherit its pipes
    # so descendants retaining them remain visible to the caller's drain gate.
    $process.WaitForExit()
    $exitCode = $process.ExitCode
} finally {
    $process.Dispose()
}
exit $exitCode
