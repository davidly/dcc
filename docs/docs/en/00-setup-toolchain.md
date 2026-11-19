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
native C compiler. **3.7 is the minimum supported version, not the version to
install specifically.** An existing Python 3.7+ installation is sufficient;
for a new installation, choose the current stable Python 3 supported by your
operating system. The build and test drivers use Python's standard library,
so they do not require a separate `pip install` step.

The debugger host and example adapter also require CMake and a C++17 compiler.
The same Python drivers run on Windows, macOS, Linux, and Raspberry Pi.

=== "Windows"

    1. If `python --version` already reports Python 3.7 or later, keep that
       installation. Otherwise, download the **Python install manager** from
       [Python's Windows downloads](https://www.python.org/downloads/windows/)
       and open the downloaded installer. The Microsoft Store version is also
       supported by Python's [Windows installation guide](https://docs.python.org/3/using/windows.html).
    2. Open a new Command Prompt and install the default runtime with the
       manager, then verify it:

        ```bat
        py install default
        python --version
        python -c "import sys; print(sys.executable)"
        ```

       `py install` is a command of the install manager, not the older `py`
       launcher. If an existing `py` command does not recognize `install`, use
       the manager's `pymanager install default` command instead. If `python`
       opens the Store or is not found, check **Manage app execution aliases**
       for the manager's **Python (default)** alias, then reopen the terminal.
       The Python guide above has the corresponding troubleshooting steps.
    3. Install Visual Studio or Visual Studio Build Tools with **Desktop
       development with C++** and its CMake component. For Windows ARM64,
       include the ARM64/ARM64EC build tools. The Python builder locates MSVC
       and initializes its environment itself.

    Run DCC scripts with `python`, for example `python scripts/build-dcc.py`.
    The `m.bat` wrapper also expects `python` to be available on `PATH`.

=== "macOS"

    1. Download the current stable **macOS installer** from
       [Python's macOS downloads](https://www.python.org/downloads/macos/).
       Open the `.pkg` and follow the installer with its default options.
       The universal2 installer supports both Apple Silicon and Intel Macs
       within that release's supported macOS versions. Python's
       [macOS installation guide](https://docs.python.org/3/using/mac.html)
       describes the installer and supported systems.
    2. Open a new Terminal window and verify the installed interpreter:

        ```sh
        python3 --version
        python3 -c "import sys; print(sys.executable)"
        ```

       Confirm the version is at least 3.7. An existing suitable installation
       can be used; installing another copy is not required.
    3. Install the native compiler tools:

        ```sh
        xcode-select --install
        ```

       Install CMake separately if building the debugger host, and verify
       `clang` and `cmake` are on `PATH`.

    Run DCC scripts with `python3`, for example `python3 scripts/build-dcc.py`.

=== "Ubuntu / Raspberry Pi OS"

    Install the distribution's Python 3, native build tools, and CMake:

    ```sh
    sudo apt update
    sudo apt install build-essential python3 cmake
    python3 --version
    python3 -c "import sys; print(sys.executable)"
    ```

    Confirm the reported version is at least 3.7. Existing Python 3.7+
    installations on older Raspberry Pi 4 systems are supported; a newer
    Python is not required just to run DCC's build and test scripts.
    Run scripts with `python3`, for example `python3 scripts/build-dcc.py`.

Documentation examples that use `python` mean the selected Python 3.7+
interpreter. On macOS, Ubuntu, and Raspberry Pi OS, use `python3` instead.
The version and executable-path checks above help confirm which interpreter
will run the scripts. PowerShell is not required on any platform.

Full local proof/coverage runs additionally require sh,
`ntvcm`, Clang, and matching `llvm-cov` / `llvm-profdata` tools.

Release packaging and specialized test utilities also use Python. Native
installer creation additionally requires the platform's packaging tools; see
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
