# The toolchain

To build CP/M apps, you need the native DCC tools (`dcc`, `dccmake`,
`dccpeep`, `dccrtlstrip`, `m80c`, and `l80c`), the bundled headers, and
`DCCRTL.MAC`. Normal builds run entirely on the host. Use
[`ntvcm`](appendix/03-utilities.md#toolchain-commands) to run the resulting
programs, or transfer them to another CP/M emulator or real hardware.
The original `m80.com` and `l80.com` are needed only for the optional emulated
assembler/linker path. Source debugging uses the separate
[`dcc-debug-host`](00-debug-host.md).

You build these tools once. After that, use them from any CP/M app project.

Setup flow:

- Install the host prerequisites for Windows, macOS, or Linux.
- Clone the DCC C Compiler (`dcc`) and `ntvcm` repositories.
- Build the DCC C Compiler host tools with `pwsh ./scripts/build-dcc.ps1`
  (or `sh m-posix.sh` on Linux platforms without a PowerShell package, e.g.
  RISC-V64 boards or Raspberry Pi OS).
- Build the ntvcm emulator.
- Add the DCC C Compiler and ntvcm directories to your `PATH`.
- Verify the setup with a sample CP/M program.

## Install prerequisites

Install the native compiler tools for your host platform before cloning and
building DCC C Compiler or ntvcm.

The full PowerShell build requires **PowerShell 7 or later** (`pwsh`), CMake,
and a C++17 compiler for `dcc-debug-host` and its example adapter. The Visual
Studio C++ workload below supplies CMake on Windows. Install it separately with
`brew install cmake` on macOS or `sudo apt install cmake` on Ubuntu.

Install the latest **stable** PowerShell release unless you have a reason to
test a preview. Keep the package manager or installer that installed it as the
owner of upgrades; do not combine a system package, a user-local archive, and
a .NET global-tool installation on the same `PATH`.

=== "Windows"

     1. Install Visual Studio Build Tools with the C++ workload. Install with
         `winget`:

        ```powershell
        winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait --norestart"
        ```

        You can also use the Visual Studio Installer and select **Desktop
        development with C++**. The Windows build uses the Microsoft C/C++
        compiler tools and CMake from that installation.

    2. On Windows 11 or a Windows client with WinGet, install the stable
       PowerShell package. This is Microsoft's recommended client install
       path:

        ```powershell
        winget install --id Microsoft.PowerShell --source winget
        pwsh --version
        ```

        WinGet currently selects the MSIX package. That is suitable for this
        build, but it is per-user and does not support PowerShell remoting or
        all-users profiles. For Windows Server, managed deployments, remoting,
        or an all-users installation, use the MSI path in the
        [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/install-powershell-on-windows)
        instead. Use the architecture-matched package on Windows ARM64.

=== "macOS"

    1. Install the Xcode Command Line Tools:

        ```bash
        xcode-select --install
        ```

        This provides the clang and C++ compiler tools used by the macOS build
        scripts.

    2. Install the architecture-matched, Microsoft-signed PKG from the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/install-powershell-on-macos).
       This is Microsoft's preferred path for most macOS users. Choose the
       `osx-arm64` package for Apple Silicon and `osx-x64` for Intel Macs.
       Then open a new terminal and verify it:

        ```bash
        pwsh --version
        ```

        The PKG installs `pwsh` under `/usr/local/bin`. A Homebrew cask can be
        appropriate when Homebrew centrally manages your developer tools, but
        do not install both variants. Use the archive path only when you need
        side-by-side versions or a custom install location; it requires manual
        dependency and update management.

=== "Ubuntu"

    1. Install gcc, g++, make, and the usual build tools:

        ```bash
        sudo apt install build-essential
        ```

    2. On a supported Ubuntu LTS release, install PowerShell from Microsoft's
       package repository. This is the preferred Ubuntu method and lets APT
       manage upgrades:

        ```sh
        sudo apt-get update
        sudo apt-get install -y wget apt-transport-https software-properties-common
        . /etc/os-release
        wget -q "https://packages.microsoft.com/config/ubuntu/${VERSION_ID}/packages-microsoft-prod.deb"
        sudo dpkg -i packages-microsoft-prod.deb
        rm packages-microsoft-prod.deb
        sudo apt-get update
        sudo apt-get install -y powershell
        pwsh --version
        ```

        The Microsoft repository also carries some .NET packages. If this host
        uses Ubuntu's .NET packages, review Microsoft's
        [package-source guidance](https://learn.microsoft.com/powershell/scripting/install/install-ubuntu#install-powershell-7-from-the-package-repository)
        before adding it. Microsoft supports Ubuntu LTS releases; use the
        [manual archive method](https://learn.microsoft.com/powershell/scripting/install/install-ubuntu#manually-download-and-install-powershell-7)
        for interim or unsupported releases.

=== "Ubuntu ARM64"

    1. Install gcc, g++, make, and the usual build tools:

        ```bash
        sudo apt install build-essential curl
        ```

    2. Use the Ubuntu repository route above if it publishes a package for your
       release and architecture. Otherwise, use the official `linux-arm64`
       archive. This user-local approach avoids changing system package
       sources. Set `version` to a stable release listed on the
       [PowerShell releases page](https://github.com/PowerShell/PowerShell/releases),
       and verify the archive against that release's published SHA-256 file
       before extracting it:

        ```sh
        version=7.6.6
        asset="powershell-${version}-linux-arm64.tar.gz"
        install_dir="$HOME/.local/share/powershell/$version"
        release_url="https://github.com/PowerShell/PowerShell/releases/download/v${version}"

        mkdir -p "$install_dir" "$HOME/.local/bin"
        curl -fLO "$release_url/$asset"
        curl -fLO "$release_url/hashes.sha256"
        grep -F " $asset" hashes.sha256 | sha256sum -c - || exit 1
        tar -xzf "$asset" -C "$install_dir"
        chmod +x "$install_dir/pwsh"
        ln -sfn "$install_dir/pwsh" "$HOME/.local/bin/pwsh"
        rm "$asset" hashes.sha256
        ```

        Make sure `~/.local/bin` is on your `PATH`, then verify both the
        version and architecture:

        ```sh
        export PATH="$HOME/.local/bin:$PATH"
        pwsh --version
        file "$(readlink -f "$HOME/.local/bin/pwsh")"
        ```

        The `file` output should report `ARM aarch64`. Add `~/.local/bin` to
        your shell startup file if it is not already present. Re-run the
        checksum step for each upgrade, extract into a new versioned directory,
        then move only the `pwsh` symlink.


=== "Windows ARM64"

    1. Install Visual Studio Build Tools with the C++ workload and native ARM64
       compiler tools. Install with `winget`:

        ```powershell
        winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --includeRecommended --quiet --wait --norestart"
        ```

        You can also use the Visual Studio Installer and select **Desktop
        development with C++**, then add the **MSVC ARM64/ARM64EC build tools**
        component. The DCC C Compiler Windows build scripts use the native ARM64 MSVC
        environment (`vcvarsarm64.bat`) when they run on Windows ARM64. CMake
        is included with the recommended components.

    2. Verify that the ARM64 MSVC tools were installed:

        ```powershell
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
        Test-Path "$installPath\VC\Auxiliary\Build\vcvarsarm64.bat"
        ```

        The final command should print `True`.

    3. Install stable PowerShell 7 with WinGet, which selects the native
       architecture automatically:

        ```powershell
        winget install --id Microsoft.PowerShell --source winget
        pwsh --version
        ```

        For server, managed, remoting, or all-users installations, choose the
        ARM64 MSI package from the
        [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/install-powershell-on-windows)
        instead of the per-user MSIX package.

## Verify PowerShell

Run this after any platform-specific installation. It confirms that the shell
on `PATH` is PowerShell 7+, shows the executable being used, and starts without
loading a profile that might mask a setup problem:

```powershell
pwsh -NoLogo -NoProfile -Command '$PSVersionTable.PSVersion; $PSVersionTable.PSEdition; $PSVersionTable.OS; (Get-Command pwsh).Source'
```

## Clone the repositories

    # Clone DCC C Compiler
    git clone https://github.com/davidly/dcc.git

    # Clone the ntvcm z80 emulator
    git clone https://github.com/davidly/ntvcm.git

## Build DCC C Compiler

The cross-platform PowerShell build script is in the `scripts` directory. It
builds `dcc`, `dccpeep`, `dccrtlstrip`, `dccmake`, `m80c`, `l80c`,
`dcc-debug-host`, and its example I/O adapter, using MSVC on Windows, clang on
macOS, and gcc on Linux by default. Run it from the cloned `dcc` directory:

    cd dcc
    pwsh ./scripts/build-dcc.ps1


## Build ntvcm

ntvcm is a C++ project. Build it from its own directory. The commands below
start from the parent directory containing both checkouts; from `dcc`, first
return to that parent with `cd ..`.

Use the `m.bat` / `mmac.sh` / `m.sh` scripts below, not `m.bat` / `mmac.sh`
/ `m.sh`. The `r` versions build release configurations of the emulator with
asserts compiled out, so the resulting `ntvcm` runs faster.

=== "Windows"

    Open a Developer PowerShell or Developer Command Prompt so the Microsoft
    C/C++ compiler (`cl`) is on your `PATH`, then run the Windows release build
    script:

    ```powershell
    cd ntvcm
    .\m.bat
    ```

    Produces `ntvcm.exe`.

=== "macOS"

    Open a terminal where the Xcode Command Line Tools are available, then run
    the macOS release build script:

    ```bash
    cd ntvcm
    chmod +x mmac.sh
    ./mmac.sh
    ```

    Produces the `ntvcm` executable.

=== "Ubuntu"

    Open a terminal where `g++` is available, then run the Linux release build
    script:

    ```bash
    cd ntvcm
    chmod +x m.sh
    ./m.sh
    ```

    Produces the `ntvcm` executable.

=== "Ubuntu ARM64"

    Open a terminal where `g++` is available, then run the Linux release build
    script:

    ```bash
    cd ntvcm
    chmod +x m.sh
    ./m.sh
    ```

    Produces the `ntvcm` executable.

=== "Windows ARM64"

    Open a Developer PowerShell or Developer Command Prompt for ARM64 so the
    Microsoft C/C++ compiler (`cl`) is on your `PATH`, then run the Windows
    release build script:

    ```powershell
    cd ntvcm
    .\m.bat
    ```

    Produces `ntvcm.exe`.

## Set up your environment

Use `dccmake` to build application projects with the source-built tools.
Use `scripts/runall.ps1` to build and verify the test suite.

The tools use an environment variable if you set one; otherwise they look on
your `PATH`. The relevant tools are:

- [`dcc`](appendix/03-utilities.md#toolchain-commands) — compiler
- [`dccmake`](appendix/03-utilities.md#build-pipeline-helper-dccmake) — build pipeline helper
- [`dccpeep`](appendix/03-utilities.md#toolchain-commands) — peephole optimizer
- [`dccrtlstrip`](appendix/03-utilities.md#toolchain-commands) — application/runtime stripper
- [`ntvcm`](appendix/03-utilities.md#toolchain-commands) — CP/M emulator
- [`m80c`](appendix/03-utilities.md#native-assembler-m80c) / [`l80c`](appendix/03-utilities.md#native-linker-l80c) — native assembler and linker

Recommended setup, especially when building apps in a project *outside* the DCC C Compiler
repo, is to add the directories containing the built `dcc` and `ntvcm`
binaries to your `PATH`. The DCC C Compiler directory also provides `dccpeep`,
`dccmake`, `dccrtlstrip`, `m80c`, `l80c`, the standard headers, and
`DCCRTL.MAC`. For a `dccmake` build outside that directory, configure
`dcc-runtime` and `dcc-include-directory` as shown in
[Building and linking](02-build-and-link.md#build-with-dccmake); putting a
directory on `PATH` alone does not configure header lookup.

=== "Windows"

    1. Add the DCC C Compiler and ntvcm directories to `PATH` for the current PowerShell
       session:

        ```powershell
        $env:PATH += ";C:\path\to\dcc;C:\path\to\ntvcm"
        ```

    2. To make that permanent for your Windows user account, update the user
       `PATH` and then open a new terminal:

        ```powershell
        $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
        [Environment]::SetEnvironmentVariable("Path", "$userPath;C:\path\to\dcc;C:\path\to\ntvcm", "User")
        ```

    3. Replace `C:\path\to\dcc` and `C:\path\to\ntvcm` with the actual
       directories. With this on your `PATH`, the scripts find `dcc`,
       `dccpeep`, `dccrtlstrip`, and `ntvcm` automatically.

       To pin specific binaries instead (for example, when juggling multiple
    DCC C Compiler builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```powershell
        $env:PATH += ";C:\path\to\ntvcm"
        $env:DCC = "C:\path\to\dcc\dcc.exe"
        $env:DCCPEEP = "C:\path\to\dcc\dccpeep.exe"
        $env:DCCRTLSTRIP = "C:\path\to\dcc\dccrtlstrip.exe"
        ```

=== "macOS"

    1. Add the DCC C Compiler and ntvcm directories to `PATH` for the current shell
       session:

        ```bash
        export PATH="$PATH:/path/to/dcc:/path/to/ntvcm"
        ```

    2. To make that permanent for the default zsh shell, append the same setting
       to `~/.zshrc`, then open a new terminal or reload the file:

        ```bash
        printf '\nexport PATH="$PATH:/path/to/dcc:/path/to/ntvcm"\n' >> ~/.zshrc
        source ~/.zshrc
        ```

    3. Replace `/path/to/dcc` and `/path/to/ntvcm` with the actual directories
       (e.g. `~/GitHub/dcc` and `~/GitHub/ntvcm`). With this on your `PATH`,
       the scripts find `dcc`, `dccpeep`, `dccrtlstrip`, and `ntvcm`
       automatically.

       To pin specific binaries instead (for example, when juggling multiple
    DCC C Compiler builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Ubuntu"

    1. Add the DCC C Compiler and ntvcm directories to `PATH` for the current shell
       session:

        ```bash
        export PATH="$PATH:/path/to/dcc:/path/to/ntvcm"
        ```

    2. To make that permanent for bash, append the same setting to `~/.bashrc`,
       then open a new terminal or reload the file:

        ```bash
        printf '\nexport PATH="$PATH:/path/to/dcc:/path/to/ntvcm"\n' >> ~/.bashrc
        source ~/.bashrc
        ```

    3. Replace `/path/to/dcc` and `/path/to/ntvcm` with the actual directories
       (e.g. `~/GitHub/dcc` and `~/GitHub/ntvcm`). With this on your `PATH`,
       the scripts find `dcc`, `dccpeep`, `dccrtlstrip`, and `ntvcm`
       automatically.

       To pin specific binaries instead (for example, when juggling multiple
    DCC C Compiler builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Ubuntu ARM64"

    1. Add the DCC C Compiler and ntvcm directories to `PATH` for the current shell
       session:

        ```bash
        export PATH="$PATH:/path/to/dcc:/path/to/ntvcm"
        ```

    2. To make that permanent for bash, append the same setting to `~/.bashrc`,
       then open a new terminal or reload the file:

        ```bash
        printf '\nexport PATH="$PATH:/path/to/dcc:/path/to/ntvcm"\n' >> ~/.bashrc
        source ~/.bashrc
        ```

    3. Replace `/path/to/dcc` and `/path/to/ntvcm` with the actual directories
       (e.g. `~/GitHub/dcc` and `~/GitHub/ntvcm`). With this on your `PATH`,
       the scripts find `dcc`, `dccpeep`, `dccrtlstrip`, and `ntvcm`
       automatically.

       To pin specific binaries instead (for example, when juggling multiple
    DCC C Compiler builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Windows ARM64"

    1. Add the DCC C Compiler and ntvcm directories to `PATH` for the current PowerShell
       session:

        ```powershell
        $env:PATH += ";C:\path\to\dcc;C:\path\to\ntvcm"
        ```

    2. To make that permanent for your Windows user account, update the user
       `PATH` and then open a new terminal:

        ```powershell
        $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
        [Environment]::SetEnvironmentVariable("Path", "$userPath;C:\path\to\dcc;C:\path\to\ntvcm", "User")
        ```

    3. Replace `C:\path\to\dcc` and `C:\path\to\ntvcm` with the actual
       directories. With this on your `PATH`, the scripts find `dcc`,
       `dccpeep`, `dccrtlstrip`, and `ntvcm` automatically.

       To pin specific binaries instead (for example, when juggling multiple
    DCC C Compiler builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```powershell
        $env:PATH += ";C:\path\to\ntvcm"
        $env:DCC = "C:\path\to\dcc\dcc.exe"
        $env:DCCPEEP = "C:\path\to\dcc\dccpeep.exe"
        $env:DCCRTLSTRIP = "C:\path\to\dcc\dccrtlstrip.exe"
        ```

## Verify the setup

With the tools on your `PATH`, run the full extended unit-test suite. From your
operating-system terminal or the VS Code terminal, first change to your local
DCC checkout directory:

```powershell
cd C:\path\to\dcc
pwsh ./scripts/runall.ps1 -Mode full -Extended
```

The suite builds and runs the repository's tests with and without the peephole
optimizer. Once it passes, move on to [Building and linking](02-build-and-link.md)
for the day-to-day workflow.
