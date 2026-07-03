# The toolchain

To build and run CP/M apps with dcc you need two things: the dcc compiler tools
(`dcc`, `dccpeep`, `dccrtlstrip`, plus `DCCRTL.MAC`, `m80.com`, and `l80.com`)
and the [ntvcm](https://github.com/davidly/ntvcm) CP/M 2.2 emulator to run the
results. Both are self-contained projects that build on Windows, Linux, and
macOS.

You build these tools once. After that, use them from any CP/M app project.

Setup flow:

- Install the host prerequisites for Windows, macOS, or Linux.
- Clone the `dcc` and `ntvcm` repositories.
- Build the dcc host tools with `pwsh ./scripts/build-dcc.ps1`.
- Build the ntvcm emulator.
- Add the dcc and ntvcm directories to your `PATH`.
- Verify the setup with a sample CP/M program.

## Install prerequisites

Install the native compiler tools for your host platform before cloning and
building dcc or ntvcm.

=== "Windows"

     1. Install Visual Studio Build Tools with the C++ workload. Install with
         `winget`:

        ```powershell
        winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait --norestart"
        ```

        You can also use the Visual Studio Installer and select **Desktop
        development with C++**. The Windows build uses the Microsoft C/C++
        compiler tools from that installation.

    2. Install PowerShell 7 (`pwsh`) from the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows),
       then verify it is available:

        ```powershell
        pwsh --version
        ```

=== "macOS"

    1. Install the Xcode Command Line Tools:

        ```bash
        xcode-select --install
        ```

        This provides the clang and C++ compiler tools used by the macOS build
        scripts.

    2. Install PowerShell 7 (`pwsh`) with Homebrew or the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-macos),
       then verify it is available:

        ```bash
        brew install --cask powershell
        pwsh --version
        ```

=== "Ubuntu"

    1. Install gcc, g++, make, and the usual build tools:

        ```bash
        sudo apt install build-essential
        ```

    2. Install PowerShell 7 (`pwsh`) using the package instructions for your
       Ubuntu release in the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-linux),
       then verify it is available:

        ```bash
        pwsh --version
        ```

=== "Ubuntu ARM64"

    1. Install gcc, g++, make, and the usual build tools:

        ```bash
        sudo apt install build-essential
        ```

    2. Install an ARM64 build of PowerShell 7 (`pwsh`). If the package flow for
       your Ubuntu release is available, use the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-linux).
       If not, use the official `linux-arm64` tarball from the PowerShell
       release page. To find the latest available version, check the
       [PowerShell releases page](https://github.com/PowerShell/PowerShell/releases)
       or query the GitHub release metadata:

        ```bash
        curl -fsSL https://api.github.com/repos/PowerShell/PowerShell/releases/latest \
            | sed -n 's/.*"tag_name": "v\([^"]*\)".*/\1/p'
        ```

        For a user-local install, set `version` to that release:

        ```bash
        version=7.6.3
        install_dir="$HOME/.local/share/powershell/$version"
        archive="/tmp/powershell-${version}-linux-arm64.tar.gz"

        mkdir -p "$install_dir" "$HOME/.local/bin"
        curl -fL \
            "https://github.com/PowerShell/PowerShell/releases/download/v${version}/powershell-${version}-linux-arm64.tar.gz" \
            -o "$archive"
        tar -xzf "$archive" -C "$install_dir"
        chmod +x "$install_dir/pwsh"
        ln -sfn "$install_dir/pwsh" "$HOME/.local/bin/pwsh"
        ```

        Make sure `~/.local/bin` is on your `PATH`, then verify both the
        version and architecture:

        ```bash
        export PATH="$HOME/.local/bin:$PATH"
        pwsh --version
        file "$(readlink -f "$HOME/.local/bin/pwsh")"
        ```

        The `file` output should report `ARM aarch64`.

=== "Windows ARM64"

    1. Install Visual Studio Build Tools with the C++ workload and native ARM64
       compiler tools. Install with `winget`:

        ```powershell
        winget install --id Microsoft.VisualStudio.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --includeRecommended --quiet --wait --norestart"
        ```

        You can also use the Visual Studio Installer and select **Desktop
        development with C++**, then add the **MSVC ARM64/ARM64EC build tools**
        component. The dcc Windows build scripts use the native ARM64 MSVC
        environment (`vcvarsarm64.bat`) when they run on Windows ARM64.

    2. Verify that the ARM64 MSVC tools were installed:

        ```powershell
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
        Test-Path "$installPath\VC\Auxiliary\Build\vcvarsarm64.bat"
        ```

        The final command should print `True`.

    3. Install PowerShell 7 (`pwsh`) from the
       [Microsoft PowerShell install guide](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows),
       then verify it is available:

        ```powershell
        pwsh --version
        ```

## Clone the repositories

    # Clone the dcc compiler
    git clone https://github.com/davidly/dcc.git

    # Clone the ntvcm z80 emulator
    git clone https://github.com/davidly/ntvcm.git

## Build dcc

The cross-platform PowerShell build script is in the `scripts` directory. It
builds `dcc`, `dccpeep`, and `dccrtlstrip`, using MSVC on Windows, clang on
macOS, and gcc on Linux by default:

    pwsh ./scripts/build-dcc.ps1

## Build ntvcm

ntvcm is a C++ project. Build it from its own directory.

=== "Windows"

    Open a Developer PowerShell or Developer Command Prompt so the Microsoft
    C/C++ compiler (`cl`) is on your `PATH`, then run the Windows build script:

    ```powershell
    cd ntvcm
    .\m.bat
    ```

    Produces `ntvcm.exe`.

=== "macOS"

    Open a terminal where the Xcode Command Line Tools are available, then run
    the macOS build script:

    ```bash
    cd ntvcm
    chmod +x mmac.sh
    ./mmac.sh
    ```

    Produces the `ntvcm` executable.

=== "Ubuntu"

    Open a terminal where `g++` is available, then run the Linux build script:

    ```bash
    cd ntvcm
    chmod +x m.sh
    ./m.sh
    ```

    Produces the `ntvcm` executable.

=== "Ubuntu ARM64"

    Open a terminal where `g++` is available, then run the Linux build script:

    ```bash
    cd ntvcm
    chmod +x m.sh
    ./m.sh
    ```

    Produces the `ntvcm` executable.

=== "Windows ARM64"

    Open a Developer PowerShell or Developer Command Prompt for ARM64 so the
    Microsoft C/C++ compiler (`cl`) is on your `PATH`, then run the Windows
    build script:

    ```powershell
    cd ntvcm
    .\m.bat
    ```

    Produces `ntvcm.exe`.

## Set up your environment

Build scripts live in the `scripts` directory:

- `scripts/ma.sh` — builds one app on Linux/macOS without PowerShell
- `scripts/ma.ps1` — builds one app with PowerShell 7+
- `scripts/runall.ps1` — builds and verifies the test suite

Run the single-app shell helper directly on Linux/macOS, or run the PowerShell
scripts from your operating-system terminal or the VS Code terminal by changing
to the dcc checkout, starting `pwsh`, and running `./scripts/ma.ps1` or
`./scripts/runall.ps1`.

They resolve each tool the same way: they use an environment variable if you set
one, otherwise they look for the tool on your `PATH`. The relevant tools are:

- `dcc` — compiler
- `dccpeep` — peephole optimizer
- `dccrtlstrip` — runtime stripper
- `ntvcm` — CP/M emulator
- `m80` / `l80` — assembler and linker

Recommended setup, especially when building apps in a project *outside* the dcc
repo, is to add the directories containing the built `dcc` and `ntvcm`
binaries to your `PATH`. The dcc directory also provides `dccpeep`,
`dccrtlstrip`, `m80.com`, `l80.com`, and `DCCRTL.MAC`, so no per-tool variables
are needed.

=== "Windows"

    1. Add the dcc and ntvcm directories to `PATH` for the current PowerShell
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
       dcc builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```powershell
        $env:PATH += ";C:\path\to\ntvcm"
        $env:DCC = "C:\path\to\dcc\dcc.exe"
        $env:DCCPEEP = "C:\path\to\dcc\dccpeep.exe"
        $env:DCCRTLSTRIP = "C:\path\to\dcc\dccrtlstrip.exe"
        ```

=== "macOS"

    1. Add the dcc and ntvcm directories to `PATH` for the current shell
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
       dcc builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Ubuntu"

    1. Add the dcc and ntvcm directories to `PATH` for the current shell
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
       dcc builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Ubuntu ARM64"

    1. Add the dcc and ntvcm directories to `PATH` for the current shell
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
       dcc builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```bash
        export PATH="$PATH:/path/to/ntvcm"
        export DCC=/path/to/dcc/dcc
        export DCCPEEP=/path/to/dcc/dccpeep
        export DCCRTLSTRIP=/path/to/dcc/dccrtlstrip
        ```

=== "Windows ARM64"

    1. Add the dcc and ntvcm directories to `PATH` for the current PowerShell
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
       dcc builds), set the environment variables to explicit paths and only
       put ntvcm on `PATH`:

        ```powershell
        $env:PATH += ";C:\path\to\ntvcm"
        $env:DCC = "C:\path\to\dcc\dcc.exe"
        $env:DCCPEEP = "C:\path\to\dcc\dccpeep.exe"
        $env:DCCRTLSTRIP = "C:\path\to\dcc\dccrtlstrip.exe"
        ```

## Verify the setup

With the tools on your `PATH`, build and run one of the dcc repo's sample tests
to confirm everything is wired up. From your operating-system terminal or the VS
Code terminal, change to the dcc checkout, start PowerShell, build
`tests/tstr.c`, then run the generated `.COM` file under ntvcm:

    cd /path/to/dcc
    pwsh
    ./scripts/ma.ps1 tstr -Mode fast       # compiles tests/tstr.c -> TSTR.COM
    ntvcm TSTR.COM                         # runs it under the emulator

The dcc repo's `tests/` programs are suitable samples for scratch projects, but
day-to-day work does not need to happen inside the dcc repo. The tools build
CP/M apps from wherever your sources live. Once that works, move on to
[Building and linking](02-build-and-link.md) for the day-to-day workflow.
