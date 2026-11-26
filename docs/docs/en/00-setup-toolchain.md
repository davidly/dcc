# The toolchain

Build DCC once to compile and link CP/M programs on your host. This guide also
builds [ntvcm](appendix/03-utilities.md#toolchain-commands) so you can run those
programs locally. Use the same terminal from cloning through verification.

## Install prerequisites

Reuse your existing tools if available. The DCC build needs Python 3.7 or
newer and a native C compiler. The debugger host and example adapter also
need a C++17 compiler and CMake 3.16 or newer. The build scripts use Python's
standard library.

=== "Windows"

    Install [Git for Windows](https://git-scm.com/install/windows){: target="_blank" }
    if Git is not already installed.

    If you need Python, install the
    [Python install manager](https://www.python.org/downloads/windows/){: target="_blank" }.
    Open a new PowerShell window and run `py install default`. If the older
    Python launcher owns the command, run `pymanager install default` instead.
    Use `python --version` to check the interpreter.

    Install [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio){: target="_blank" }
    through WinGet in PowerShell:

    ```powershell
    winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    ```

    The command installs the **Desktop development with C++** workload and
    its recommended **C++ CMake tools for Windows** component. Restart
    PowerShell when installation finishes. Check that the C++ workload and
    CMake are present by running:

    ```powershell
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Test-Path (Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat')
    Test-Path (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    ```

    Both commands should print `True`.

=== "macOS"

    Run `xcode-select --install` if you need the Xcode Command Line Tools,
    which provide Git and Clang. If you need Python, use the
    [macOS installer](https://www.python.org/downloads/macos/){: target="_blank" } and check
    `python3 --version`. Install [CMake](https://cmake.org/download/){: target="_blank" } if needed.
    If you installed `CMake.app` in Applications, add its command-line tools to
    the current terminal.

    ```sh
    export PATH="/Applications/CMake.app/Contents/bin:$PATH"
    ```

=== "Ubuntu (x64 and ARM64)"

    ```bash
    sudo apt update && sudo apt install git build-essential python3 cmake
    ```

    Optional, for differential fuzz testing, install Clang and its sanitizer
    runtime:

    ```bash
    sudo apt-get install --no-install-recommends clang libclang-rt-21-dev
    ```

=== "Windows ARM64"

    Install [Git for Windows](https://git-scm.com/install/windows){: target="_blank" }
    if Git is not already installed.

    If you need Python, install the
    [Python install manager](https://www.python.org/downloads/windows/){: target="_blank" }.
    Open a new PowerShell window and run `py install default`. If the older
    Python launcher owns the command, run `pymanager install default` instead.
    Use `python --version` to check the interpreter.

    Install [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio){: target="_blank" }
    through WinGet in PowerShell:

    ```powershell
    winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.Tools.ARM64"
    ```

    This installs the **Desktop development with C++** workload, its
    recommended **C++ CMake tools for Windows** component, and the MSVC
    ARM64 tools. Restart PowerShell when installation finishes. To select
    ARM64EC as well, choose it in the Visual Studio Installer.
    Confirm the C++ workload and CMake are present:

    ```powershell
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
    Test-Path (Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsarm64.bat')
    Test-Path (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    ```

    Both commands should print `True`.

## Clone the repositories

Run these commands from the directory where you keep your projects.

```sh
git clone https://github.com/davidly/dcc.git
git clone https://github.com/davidly/ntvcm.git
```

<span id="build-dcc-c-compiler"></span>
<span id="build-ntvcm"></span>

## Build DCC and ntvcm

From the directory containing both checkouts, run the build block for your
platform. On Windows, the scripts initialize MSVC automatically.

Each build writes its executables to its own checkout. The DCC build also
includes the [debugger host and example adapter](00-debug-host.md). Add
`--skip-debug-host` to the Python command if you only need the DCC command-line
tools.

=== "Windows"

    ```powershell
    Set-Location dcc -ErrorAction Stop; python scripts/build-dcc.py; if ($LASTEXITCODE -eq 0) { Set-Location ../ntvcm -ErrorAction Stop; .\m.bat }; Set-Location ..
    ```

=== "macOS"

    ```bash
    cd dcc && python3 scripts/build-dcc.py &&
    cd ../ntvcm && bash mmac.sh
    ```

=== "Ubuntu (x64 and ARM64)"

    ```bash
    cd dcc && python3 scripts/build-dcc.py &&
    cd ../ntvcm && bash m.sh
    ```

=== "Windows ARM64"

    ```powershell
    Set-Location dcc -ErrorAction Stop; python scripts/build-dcc.py; if ($LASTEXITCODE -eq 0) { Set-Location ../ntvcm -ErrorAction Stop; .\m.bat }; Set-Location ..
    ```

<span id="set-up-your-environment"></span>

## Verify the setup

Run your platform's block from the directory containing both checkouts. It adds both tools to
`PATH` for this terminal and future sessions, then prints both versions.

=== "Windows"

    ```powershell
    $toolDirs = @((Resolve-Path dcc).Path, (Resolve-Path ntvcm).Path); foreach ($scope in 'User', 'Process') { $existing = [Environment]::GetEnvironmentVariable('Path', $scope) -split ';'; $otherDirs = @($existing | Where-Object { $_ -and $_ -notin $toolDirs }); [Environment]::SetEnvironmentVariable('Path', (($toolDirs + $otherDirs) -join ';'), $scope) }; ntvcm -V; dcc --version
    ```

=== "macOS"

    ```zsh
    tool_dirs="$(cd dcc && pwd):$(cd ntvcm && pwd)"
    printf -v path_line 'export PATH=%q:"$PATH"' "$tool_dirs"
    grep -Fqx -- "$path_line" ~/.zshrc 2>/dev/null ||
        printf '\n%s\n' "$path_line" >> ~/.zshrc
    export PATH="$tool_dirs:$PATH"
    ntvcm -V
    dcc --version
    ```

=== "Ubuntu (x64 and ARM64)"

    ```bash
    tool_dirs="$(cd dcc && pwd):$(cd ntvcm && pwd)"
    printf -v path_line 'export PATH=%q:"$PATH"' "$tool_dirs"
    grep -Fqx -- "$path_line" ~/.bashrc 2>/dev/null ||
        printf '\n%s\n' "$path_line" >> ~/.bashrc
    export PATH="$tool_dirs:$PATH"
    ntvcm -V
    dcc --version
    ```

=== "Windows ARM64"

    ```powershell
    $toolDirs = @((Resolve-Path dcc).Path, (Resolve-Path ntvcm).Path); foreach ($scope in 'User', 'Process') { $existing = [Environment]::GetEnvironmentVariable('Path', $scope) -split ';'; $otherDirs = @($existing | Where-Object { $_ -and $_ -notin $toolDirs }); [Environment]::SetEnvironmentVariable('Path', (($toolDirs + $otherDirs) -join ';'), $scope) }; ntvcm -V; dcc --version
    ```

The macOS commands use zsh and the Ubuntu commands use Bash. If you use another
shell, save the two directory paths in that shell's startup file. For builds
outside the DCC checkout, configure `dcc-runtime` and `dcc-include-directory`
as shown in
[Building and linking](02-build-and-link.md#build-with-dccmake).
