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
- Build the DCC C Compiler host tools with `sh m.sh` on Unix or `m.bat`
  on Windows. Use `--skip-debug-host` to build just the C tools on minimal
  systems; the builder supports Python 3.7 and newer.
- Build the ntvcm emulator.
- Add the DCC C Compiler and ntvcm directories to your `PATH`.
- Verify the setup with a sample CP/M program.

## Install prerequisites

Building and testing DCC C Compiler requires **Python 3.7 or newer** and a
native C compiler. The debugger host and example adapter also require CMake
and a C++17 compiler. The same Python drivers run on Windows, macOS, Linux,
and Raspberry Pi; PowerShell is not needed for normal builds or test suites.

=== "Windows"

    Install Python 3.7+ and put `python` on your `PATH`. Install Visual Studio
    or Visual Studio Build Tools with **Desktop development with C++** and its
    CMake component. For Windows ARM64, include the ARM64/ARM64EC build tools.
    The Python builder locates MSVC and initializes its environment itself.

=== "macOS"

    Install Xcode Command Line Tools with `xcode-select --install`, plus
    Python 3.7+ and CMake. Verify `clang`, `python3`, and `cmake` are on `PATH`.

=== "Ubuntu / Raspberry Pi OS"

    Install the native build tools, Python, and CMake:

    ```sh
    sudo apt update
    sudo apt install build-essential python3 cmake
    ```

    The Python drivers support the older Python versions available on
    Raspberry Pi 4 systems; no PowerShell installation is required.

Verify Python with `python --version` on Windows or `python3 --version` on
Unix. Commands below use `python`; use `python3` where that is the installed
command name. Full local proof/coverage runs additionally require sh,
`ntvcm`, Clang, and matching `llvm-cov` / `llvm-profdata` tools.

Some release-packaging and specialized utility scripts still use PowerShell
7. Install it only when running those remaining `.ps1` utilities; see
[`scripts/README.md`](https://github.com/davidly/dcc/blob/main/scripts/README.md).

## Clone the repositories

    # Clone DCC C Compiler
    git clone https://github.com/davidly/dcc.git

    # Clone the ntvcm z80 emulator
    git clone https://github.com/davidly/ntvcm.git

## Build DCC C Compiler

The cross-platform Python build driver is in the `scripts` directory. It
builds `dcc`, `dccpeep`, `dccrtlstrip`, `dccmake`, `m80c`, `l80c`,
`dcc-debug-host`, and its example I/O adapter, using MSVC on Windows, clang on
macOS, and gcc on Linux by default. Run it from the cloned `dcc` directory:

    cd dcc
    python scripts/build-dcc.py


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
Use `scripts/runall.py` to build and verify the test suite.

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
python3 ./scripts/runall.py --mode full --extended
```

The suite builds and runs the repository's tests with and without the peephole
optimizer. Once it passes, move on to [Building and linking](02-build-and-link.md)
for the day-to-day workflow.
