# dcc helper scripts

Developer utility scripts for the `dcc` (CP/M-80 / Z80) toolchain.

## `publish-package.ps1`

Publishes or republishes the binary package release. By default it reads the
version from `scripts/package-version.txt` (`v2.0.0`), deletes any existing
GitHub release/tag for that version, recreates the tag at the current commit,
and pushes the tag so `.github/workflows/release.yml` rebuilds the package
assets.

```pwsh
pwsh ./scripts/publish-package.ps1
pwsh ./scripts/publish-package.ps1 -Version v2.0.1 -Watch
```

The script requires `git` and the GitHub CLI (`gh`) on `PATH`, and refuses to
publish from a dirty worktree unless `-AllowDirty` is passed.

## Host compilers to install

The PowerShell scripts are intended to work on Windows, macOS, and Linux. For
the dcc host tools themselves, use the normal native compiler for each platform:

| Platform | Recommended compiler | Install notes |
| -------- | -------------------- | ------------- |
| Windows | MSVC x64 | Install Visual Studio 2022 or Visual Studio Build Tools with **Desktop development with C++**. |
| Windows ARM64 | MSVC ARM64 | Install Visual Studio Build Tools with **Desktop development with C++** plus the **MSVC ARM64/ARM64EC build tools** component. |
| macOS | Apple clang | Install Xcode Command Line Tools with `xcode-select --install`. |
| Linux | GCC | Install your distribution's C/C++ build tools. |

`build-dcc.ps1` and `validate-unit-test.ps1` follow those defaults: MSVC on
Windows, native ARM64 MSVC on Windows ARM64, clang on macOS, and gcc on Linux.
On Unix-like hosts, pass `-CC` or set `CC` when you intentionally want a
different compiler.

### Linux 32-bit GCC support for baseline validation

32-bit/multilib support is only useful for `validate-unit-test.ps1`; it is not
needed to build `dcc`, `dccpeep`, or `dccrtlstrip`. The host tools should be
built with the normal native compiler for the platform.

The validator can get closer to dcc's target model on Linux if GCC can build and
link 32-bit executables. When running on Linux with GCC, it probes `gcc -m32`; if
the probe succeeds, it adds `-m32` to host test builds and prints `(-m32)` in the
compiler summary. If the probe fails, validation continues with normal
host-width GCC.

Common package installs:

```sh
# Debian / Ubuntu
sudo apt update
sudo apt install build-essential gcc-multilib g++-multilib

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install glibc-devel.i686 libgcc.i686

# openSUSE
sudo zypper install -t pattern devel_C_C++
sudo zypper install gcc-32bit glibc-devel-32bit

# Arch Linux
sudo pacman -S base-devel gcc-multilib
```

The 32-bit host compiler mainly helps validation tests that depend on 32-bit
`long` width. It does not fully emulate dcc: dcc still has 16-bit `int`, 16-bit
pointers, CP/M file records, BDOS/BIOS calls, Z80 port I/O, and dcc-specific
stack-check runtime behavior. Tests that depend on those semantics should remain
marked with `"host": true` in `tests/_test_overrides.json` or skipped by the
validator's explicit CP/M/Z80 source checks.

## `stacksize.sh` / `stacksize.bat`

Finds the minimum **C stack reserve** an app needs under dcc's lightweight
stack-overflow guard (`-fstack-check`).

`stacksize.sh` is the macOS/Linux version; `stacksize.bat` is the equivalent for
Windows (`cmd.exe`). They take the same arguments, honour the same environment
variables, and produce the same report — the Windows version drives `ma.bat`
instead of `ma.sh`.

### Purpose

On the Z80/CP/M target the C stack and the `malloc` heap share memory with no
hardware protection. If a program recurses too deeply (or is built with too
small a `-stack` reserve) the stack can grow down into the heap and silently
corrupt it. The `-fstack-check` guard turns that silent corruption into a clean
`?stack overflow` message and exit.

`stacksize.sh` uses that guard to **measure** how much stack an app actually
needs: it builds the app with the guard forced on, runs it, and sweeps the
`-stack` reserve upward until the program runs without tripping the guard. The
first size that runs clean is the minimum requirement; the script also prints a
rounded-up recommendation with a little headroom.

### Usage

```sh
scripts/stacksize.sh <app> [-- emulator-args...]
```

- `<app>` — test/app name as passed to `ma.sh` (e.g. `triangle`, `cobint`).
- `-- args...` — arguments to pass to the emulated program (e.g. a data file
  such as `e.cob`). Everything after `--` is forwarded to the emulator.

Run it from the repo root (or anywhere — it locates the repo root relative to
the script). The in-repo `./dcc`, `./dccpeep`, `./dccrtlstrip` binaries and the
`ntvcm` emulator must be available.

### Examples

```sh
scripts/stacksize.sh triangle          # simple app
scripts/stacksize.sh cobint -- e.cob   # app that needs a data-file argument
```

Sample output:

```
Finding minimum stack for 'triangle' (guard on): start=256 step=64 max=8192 mode=peep

  stack=256    : overflow
  stack=320    : overflow
  ...
  stack=640    : OK

Minimum passing stack reserve : 640 bytes
Recommended (with headroom)   : 768 bytes

Build it with:  DCC_STACK_SIZE=768 ./ma.sh triangle peep
Or compile direct:  ./dcc -fstack-check -stack 768 tests/triangle.c -o TRIANGLE.mac
```

### Options (environment variables)

| Variable | Default | Meaning |
| -------- | ------- | ------- |
| `START`  | `256`   | First stack size to try (bytes). |
| `STEP`   | `64`    | Increment between tries (bytes). |
| `MAX`    | `8192`  | Give up after this size. Prevents an infinite loop on apps that overflow on purpose (e.g. `spsmash`). |
| `MODE`   | `peep`  | Build mode passed to `ma.sh` (`peep` or `nopeep`). |
| `EMU`    | `ntvcm` | Emulator command used to run the `.COM`. |

```sh
START=512 STEP=128 scripts/stacksize.sh triangle
MAX=16384 scripts/stacksize.sh somedeepapp
```

### Exit status

- `0` — a passing stack size was found (printed as the minimum + recommendation).
- `1` — no passing size up to `MAX`. The app may overflow on purpose (deliberate
  unbounded recursion, like `spsmash`), or it genuinely needs more than `MAX`
  bytes — raise `MAX=...` and retry.

### How it ties into the build

The sweep varies the reserve through the `DCC_STACK_SIZE` hook honored by
`ma.sh`, and forces the guard on with `DCC_FORCE_STACK_CHECK=1`. Once you know
the size, bake it in by building with `DCC_STACK_SIZE=<n> ./ma.sh <app>` or, for
the regression suite, add it to the per-app `stack_size_for` table in
`runall.sh` (and the matching block in `runall.bat`).

## `ma.ps1`

Cross-platform build driver (Windows PowerShell 5.1 and PowerShell 7+ equivalent of `ma.sh`). Compiles a
single test app with optional peephole optimization, strips runtime symbols,
and links to produce a `.COM` executable. The complete pipeline:

1. Compile source with `dcc` using default compiler options unless build flags are requested through environment variables
2. Optimize with `dccpeep` (optional, fast mode only)
3. Assemble app.MAC with M80
4. Strip DCCRTL runtime using dccrtlstrip
5. Assemble stripped RTLMIN.MAC
6. Link app + RTLMIN with L80

### Usage

```pwsh
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 <name> [full|fast|nopeep]
```

- `<name>` — test app name (e.g., `triangle`, `sieve`, `ttt`)
- `[mode]` — build mode: `full` (both builds, default), `fast` (optimized), or
  `nopeep` (unoptimized)

### Examples

```pwsh
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 triangle
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 sieve nopeep
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 cobint -Mode fast -BuildDir mybuild
```

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-Name` | (required) | Test app name (without `.c` extension) |
| `-Mode` | `full` | Build mode: `full`, `fast`, or `nopeep` |
| `-BuildDir` | `build` | Working directory for build artifacts |
| `-Emulator` | `ntvcm` | Emulator command for running CP/M tools |

### Environment Variables

- `DCC_STACK_SIZE` — C stack reserve in bytes; when unset, `dcc` uses its default
- `DCC_FORCE_STACK_CHECK` — Enable `-fstack-check` for all builds
- `DCC_FLOATIO` — Set to `1` to pass `-ffloatio` and keep float `printf` runtime support
- `DCC_LONGIO` — Set to `1` to pass `-flongio` and keep long integer `printf` runtime support
- `DCC_ARGS` — Extra whitespace-separated `dcc` options such as `-DNAME=1 -UOLD`
- `NTVCM_ARGS` — Extra whitespace-separated `ntvcm` options such as `-p -s:4000000`
- `DCC`, `DCCPEEP`, `DCCRTLSTRIP`, `NTVCM`, `M80`, `L80` — Tool paths

Run `dcc-ma -Help` on Windows or `dcc-ma --help` on Linux/macOS for the full option map, including which `dcc` options
are owned by the helper pipeline.

## `runall.ps1`

Comprehensive test suite: builds and runs all main test applications with output
verification against per-app baselines in `tests/baselines/`. Uses `dccmake` to
build each app and `tests/_test_overrides.json` for test-specific runtime and
build settings. Comparison is keyed by app name, so test discovery order does not matter.
Pass `-Extended` to run the extended c-testsuite corpus after the main suite.
See [`tests/README.md`](../tests/README.md) for the test/baseline relationship.

**Runs in parallel by default** (each app builds in its own `build/<app>/`
subdirectory so concurrent builds don't clobber shared artifacts). In parallel
mode the whole run is isolated under a per-invocation `build/run-<pid>/` folder,
which is **removed automatically on exit** so `build/` does not accumulate one
folder per run; pass `-KeepBuild` to retain it (e.g. to inspect a failing
build's artifacts). Use `-Serial` to fall back to sequential builds in the
shared `build/` directory.
The lightweight stack-overflow guard (`-fstack-check`) is **on by default**;
pass `-NoStackCheck` to build without it.
Pass `-Report` to append per-app run-time and `.COM` size measurements while
the suite runs. Report mode implies `-NoStackCheck`; when using `ntvcm`, normal
app runs use `-s:0` for full speed and report runs use a fixed 400 MHz clock by
default for more comparable timings across host machines.

### Usage

```pwsh
pwsh ./scripts/runall.ps1 [options]
```

Run with no options, the suite uses these defaults: **parallel** execution, the
**stack-overflow guard on** (`-fstack-check`), and the **fast optimized-only**
build mode. Use `-Mode full` when you want both fast and nopeep builds.

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-Emulator` | `ntvcm` | Emulator command for running .COM files |
| `-NoStackCheck` | (off) | Disable `-fstack-check` (the guard is ON by default) |
| `-BuildDir` | `build` | Working directory for artifacts |
| `-BaselineDir` | `tests/baselines` | Directory of per-app `<app>.txt` baselines |
| `-Mode` | `fast` | Build mode: `fast` (optimized), `nopeep` (unoptimized), or `full` |
| `-Help` | (off) | Show help text and exit without building or running tests |
| `-Extended` | (off) | Also run the extended c-testsuite corpus after the main suite |
| `-Serial` | (off) | Run sequentially instead of the default parallel mode |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
| `-KeepBuild` | (off) | Keep the per-invocation `build/run-<pid>/` folder instead of removing it on exit (parallel mode) |
| `-Report` | (off) | Append per-app execution time and `.COM` size metrics to a CSV report; implies `-NoStackCheck` |
| `-ReportFile` | `perf_results.csv` | CSV path used by `-Report` |
| `-ReportClockHz` | `400000000` | ntvcm clock speed used for measured report runs; set to `0` for full-speed report runs |

### Build modes

The `-Mode` parameter selects which optimization pass(es) to build and verify.
**The default is `fast`.**

- **`fast`** — optimized: runs the `dccpeep` peephole optimizer after compiling.
- **`nopeep`** — unoptimized: skips `dccpeep`.
- **`full`** — builds and verifies each app **twice**, once in each
  mode, against the same baseline. A default run therefore performs two builds
  per app when you select `-Mode full` (this catches optimizer bugs that change
  a program's output).

### Examples

```pwsh
pwsh ./scripts/runall.ps1                       # quick optimized-only default
pwsh ./scripts/runall.ps1 -Help                 # show help and exit
pwsh ./scripts/runall.ps1 -Serial               # sequential fallback
pwsh ./scripts/runall.ps1 -NoStackCheck         # build without the stack guard
pwsh ./scripts/runall.ps1 -ThrottleLimit 8      # cap concurrency
pwsh ./scripts/runall.ps1 -Mode fast            # optimized build only
pwsh ./scripts/runall.ps1 -Mode nopeep          # unoptimized build only
pwsh ./scripts/runall.ps1 -Extended             # also run extended c-testsuite
pwsh ./scripts/runall.ps1 -KeepBuild            # keep build/run-<pid>/ for debugging
pwsh ./scripts/runall.ps1 -Report               # append perf_results.csv
pwsh ./scripts/runall.ps1 -ReportClockHz 0 -Report  # full-speed report run
```

Parallel mode is markedly faster on multi-core machines. Each app builds in its
own `build/<app>/` subdirectory so concurrent builds don't clobber shared
artifacts, and a live `[ n/total] PASS/FAIL` status prints as each app
completes. The run's `build/run-<pid>/` folder is removed on exit unless
`-KeepBuild` is passed.

### Output

Reports:
- Total apps discovered
- Passed/failed/skipped counts
- Per-app build and execution status (live in parallel mode)
- Output verification against baseline (if available)
- Optional CSV performance report when `-Report` is passed
- Exit code 0 on full success, 1 if any test fails

### Exit Status

- `0` — All tests passed
- `1` — One or more tests failed

## `stress-runall.ps1`

Stress-tests the suite by running `runall.ps1` repeatedly, stopping on the first
failure. Useful for shaking out intermittent/flaky failures under parallel load.
The failing iteration's full log is kept for inspection.

### Usage

```pwsh
pwsh ./scripts/stress-runall.ps1                # up to 50 parallel runs
pwsh ./scripts/stress-runall.ps1 -Iterations 100
pwsh ./scripts/stress-runall.ps1 -Serial -Iterations 10
pwsh ./scripts/stress-runall.ps1 -ThrottleLimit 8 -KeepLogs
```

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-Iterations` | `50` | Maximum number of suite runs |
| `-Serial` | (off) | Run the suite serially each iteration |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
| `-LogDir` | temp folder | Where per-iteration logs are written |
| `-KeepLogs` | (off) | Keep all logs, not just the failing one |

### Exit Status

- `0` — All iterations passed
- `1` — An iteration failed (loop stopped early)

## `convert-baseline.ps1`

Splits the legacy concatenated baseline `baseline_test_dcc.txt` into per-app
baseline files under `tests/baselines/` (one `<app>.txt` per test). Used to
(re)generate the baselines consumed by `runall.ps1`.

### Usage

```pwsh
pwsh ./scripts/convert-baseline.ps1
```

### Parameters

| Parameter | Default | Meaning |
| --------- | ------- | ------- |
| `-InputFile` | `baseline_test_dcc.txt` | Legacy concatenated baseline |
| `-OutputDir` | `tests/baselines` | Destination for per-app baseline files |
| `-AppList` | (from `runall.sh`) | Ordered app list used as split boundaries |

The converter slices the legacy file using the authoritative ordered app list
(`APPLIST` in `runall.sh`) so that output lines beginning with `test ` (e.g.
`test tstdc completed with great success`) are not mistaken for section headers.
The split reproduces the original baseline byte-for-byte.
