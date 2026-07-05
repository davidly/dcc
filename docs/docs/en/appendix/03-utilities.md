# Utilities

Developer scripts for building and testing DCC C Compiler programs.

Run these scripts from the DCC C Compiler checkout or from an installed package. Linux and
macOS packages include a native shell build driver, so normal package users do
not need PowerShell to build a single app.

## Build Driver (`dcc-ma`, `ma.sh` / `ma.ps1`)

The build driver compiles one app, optionally runs `dccpeep`, strips the
runtime, assembles, and links a `.COM` executable.

- Installed packages: use `dcc-ma` on Windows, macOS, and Linux.
- Source checkout: use `scripts/ma.sh` on Linux/macOS, or `scripts/ma.ps1` with Windows PowerShell 5.1 or PowerShell 7+.

### Build Driver Usage

```pwsh
./scripts/ma.ps1 <name> [mode] [options]
```

```sh
dcc-ma <name> [mode] [options]
```

```sh
./scripts/ma.sh <name> [mode] [options]
```

- `<name>` — Test app name (e.g., `triangle`, `sieve`, `ttt`)
- `[mode]` — Build mode: `full` (both builds, default), `fast` (optimized), or
  `nopeep` (unoptimized)

### Build Driver Examples

```pwsh
./scripts/ma.ps1 triangle
./scripts/ma.ps1 sieve nopeep
./scripts/ma.ps1 cobint -Mode fast -BuildDir mybuild
```

```sh
dcc-ma triangle
dcc-ma sieve nopeep
dcc-ma cobint --mode fast --build-dir mybuild
```

```sh
./scripts/ma.sh triangle
./scripts/ma.sh sieve nopeep
./scripts/ma.sh cobint --mode fast --build-dir mybuild
```

### Build Driver Parameters

| Parameter | Default | Purpose |
| --------- | ------- | ------- |
| `-Name` | (required) | App name without `.c` extension |
| `-Mode` | `full` | Build mode: `full`, `fast`, or `nopeep` |
| `-BuildDir` | `build` | Build directory for artifacts |
| `-Emulator` | `ntvcm` | Emulator command for CP/M tools |

### Environment Variables

- `DCC_STACK_SIZE` — C stack reserve in bytes; when unset, `dcc` uses its default
- `DCC_FORCE_STACK_CHECK` — Force `-fstack-check` on all builds
- `DCC_FLOATIO` — Set to `1` to pass `-ffloatio` and keep float `printf` runtime support
- `DCC_LONGIO` — Set to `1` to pass `-flongio` and keep long integer `printf` runtime support
- `DCC_ARGS` — Extra whitespace-separated `dcc` options such as `-DNAME=1 -UOLD`
- `NTVCM_ARGS` — Extra whitespace-separated `ntvcm` options such as `-p -s:4000000`
- `DCC_HOME` — DCC C Compiler package/install root; used to find `include/`, `lib/`, and CP/M tools
- `DCC_INCLUDE` — extra include directories, separated by the host path separator
- `DCC_LIB` — extra runtime/tool asset roots, separated by the host path separator
- `DCC_RUNTIME` — explicit path to `DCCRTL.MAC`
- `DCC`, `DCCPEEP`, `DCCRTLSTRIP`, `NTVCM`, `M80`, `L80` — Tool paths

Run `dcc-ma -Help` on Windows or `dcc-ma --help` on Linux/macOS for the full option map, including which
`dcc` options are owned by the helper pipeline.

## Toolchain Commands

The DCC C Compiler toolchain is a small set of host tools, CP/M tools, and
runtime assets. The build drivers resolve these commands from explicit settings
or environment variables first, then from the local checkout or `PATH`.

| Tool | Role | Notes |
| ---- | ---- | ----- |
| `dcc` | C compiler | Host command that translates C source to M80-compatible `.MAC` assembly |
| `dccmake` | Build pipeline helper | Owns the normal compile, optimize, strip, assemble, and link pipeline; see [Build Pipeline Helper (`dccmake`)](#build-pipeline-helper-dccmake) |
| `dccpeep` | Peephole optimizer | Host command that rewrites generated `.MAC` files when `dcc-peep=true` |
| `dccrtlstrip` | Runtime stripper | Host command that scans app `.MAC` files and writes a reduced runtime; see [DCCRTL strip appendix](01-dccrtlstrip.md) |
| `DCCRTL.MAC` | Runtime source | Full CP/M runtime consumed by `dccrtlstrip` |
| `ntvcm` | CP/M emulator | Runs CP/M tools such as M80 and L80, and runs the final `.COM` programs |
| `m80.com` | CP/M assembler | Assembles app and runtime `.MAC` files to `.REL` files under `ntvcm` |
| `l80.com` | CP/M linker | Links `RTLMIN.REL` and app `.REL` files into a `.COM` executable under `ntvcm` |

## Build Pipeline Helper (`dccmake`)

`dccmake` is the lower-level build helper used by the test runner and by
repeatable local builds. It compiles one or more C source files, optionally runs
`dccpeep`, strips the runtime with `dccrtlstrip`, assembles with M80 under
`ntvcm`, and links the final `.COM` with L80.

Use `dccmake` directly when you want one command that owns the whole DCC C
Compiler pipeline but still lets you choose the exact source files, output name,
runtime, include directories, and tool paths.

### dccmake CLI Usage

```sh
dccmake [key=value ...] [dcc-style-options]
dccmake --dcc-input main.c,module.c --dcc-output APP
dccmake main.c module.c dcc-output=APP dcc-peep=true
```

Command-line settings may be written as `key=value`, `--key=value`, or
`--key value`. Positional `.c` arguments are treated as `dcc-input` files. Files
after the first input are compiled with `-module` automatically.

### dccmake CLI Examples

```sh
dccmake tests/sieve.c dcc-output=SIEVE
dccmake tests/sieve.c dcc-output=SIEVE dcc-peep=false
dccmake main.c module1.c module2.c dcc-output=APP dcc-include-directory=include
dccmake tests/attnc99.c dcc-output=ATTNC99 dcc-stack-bytes=768 dcc-peep=true
```

`dccmake` also accepts common `dcc`-style options and maps them onto pipeline
settings:

```sh
dccmake tests/tprintf.c dcc-output=TPRINTF -ffloatio
dccmake tests/app.c dcc-output=APP -I include -DDEBUG=1 -UOLD
dccmake tests/app.c dcc-output=APP -stack 1024 -fstack-check
```

### dccmake.txt Files

When a `dccmake.txt` file exists in the current directory, `dccmake` reads it
first and then applies command-line settings as overrides. The file uses one
`key=value` setting per line. Blank lines are ignored, and text after `#` is a
comment.

Values may reference environment variables with `${NAME}`. The variable must be
set, and malformed references are errors. This is useful for checking a project
configuration into source control without hard-coding checkout-specific paths.

```text
# dccmake configuration for ATTNC99
dcc-input=attnc99.c
dcc-output=ATTNC99
dcc-floatio=false
dcc-flongio=false
dcc-peep=true
dcc-build-dir=build
dcc-runtime=${DCC_DIR}/DCCRTL.MAC
dcc-include-directory=${DCC_DIR}
dcc-tool=${DCC_DIR}/dcc
dccpeep-tool=${DCC_DIR}/dccpeep
dccrtlstrip-tool=${DCC_DIR}/dccrtlstrip
ntvcm-tool=${NTVCM_DIR}/ntvcm
m80-command=${DCC_DIR}/m80.com
l80-command=${DCC_DIR}/l80.com
```

With that file in place, set the tool roots and build the app:

```sh
export DCC_DIR=$HOME/GitHub/dcc
export NTVCM_DIR=$HOME/GitHub/ntvcm

dccmake
```

The generated CP/M executable lands in the configured build directory, for
example `build/ATTNC99.COM`. If you want the `.COM` beside your source file and
do not need to keep the intermediate `.MAC`, `.REL`, `.PRN`, and stripped
runtime files, copy it back and remove the build directory:

```sh
mv -f build/*.COM .
rm -rf build
```

Command-line values override the file, so this builds the same app without the
peephole optimizer:

```sh
dccmake dcc-peep=false
```

### dccmake Settings

| Setting | Default | Purpose |
| ------- | ------- | ------- |
| `dcc-input` | (required) | Comma-separated C sources; positional `.c` arguments are also accepted |
| `dcc-output` | First input base name | CP/M 8-character output base name |
| `dcc-floatio` | Environment/default | Pass `-ffloatio` to `dcc` and keep float `printf` runtime support |
| `dcc-flongio` | Environment/default | Pass `-flongio` to `dcc` and keep long integer `printf` runtime support |
| `dcc-stack-bytes` | `512` | Stack reserve passed to `dcc` with `-stack` |
| `dcc-stack-check` | Environment/default | Pass `-fstack-check` to `dcc` |
| `dcc-include-directory` | Auto-adds `.` when standard headers are in the current directory | Comma-separated include directories; `dcc-include` is an alias |
| `dcc-define` | none | Comma-separated `NAME[=value]` entries passed to `dcc` as `-D`; `dcc-defines` is an alias |
| `dcc-undefine` | none | Comma-separated names passed to `dcc` as `-U`; `dcc-undefines` is an alias |
| `dcc-peep` | `true` | Run `dccpeep` after compiling each `.MAC` file |
| `dcc-build-dir` | `build` | Artifact directory |
| `dcc-runtime` | `DCC_RUNTIME`, local `DCCRTL.MAC`, or `DCCRTL.MAC` | Runtime source passed to `dccrtlstrip` |
| `dcc-tool` | `DCC`, local `dcc`, or `dcc` | DCC compiler command |
| `dccpeep-tool` | `DCCPEEP`, local `dccpeep`, or `dccpeep` | Peephole optimizer command |
| `dccrtlstrip-tool` | `DCCRTLSTRIP`, local `dccrtlstrip`, or `dccrtlstrip` | Runtime stripper command |
| `ntvcm-tool` | `NTVCM` or `ntvcm` | Emulator command used to run M80 and L80 |
| `m80-command` | `M80` or `m80` | CP/M assembler command passed to `ntvcm` |
| `l80-command` | `L80` or `l80` | CP/M linker command passed to `ntvcm` |

Source input basenames and the output name must be CP/M 8.3-clean. For example,
`module1.c` is valid, but a generated module output base longer than eight
characters is not.

### dccmake dcc-style Options

| Option | Equivalent setting |
| ------ | ------------------ |
| `-f`, `-ffloatio` | `dcc-floatio=true` |
| `-fl`, `-flongio` | `dcc-flongio=true` |
| `-s <bytes>`, `-stack <bytes>`, `-stack=<bytes>` | `dcc-stack-bytes=<bytes>` |
| `-fstack-check` | `dcc-stack-check=true` |
| `-I <dir>`, `-Idir` | Add an include directory |
| `-D <name>[=value]`, `-Dname=value` | Pass a define to `dcc` |
| `-U <name>`, `-Uname` | Pass an undefine to `dcc` |
| `-v`, `--version` | Print `dccmake` version |

`-c` and `-module` are rejected because `dccmake` decides module mode from the
input order.

## Test Suite Runner (`runall.ps1`)

Builds and runs the test suite against per-app baselines in `tests/baselines/`.
It uses `dccmake` for builds and `tests/_test_overrides.json` for test-specific
runtime arguments, stack sizes, and optional DCC C Compiler build flags.

Runs in parallel by default:

- Each app builds in its own `build/<app>/` subdirectory so concurrent builds
  don't clobber shared artifacts.
- The whole run is isolated under a per-invocation `build/run-<pid>/` folder that
  is removed automatically on exit; pass `-KeepBuild` to retain it for debugging.
- A live `[ n/total] PASS/FAIL` status prints as each app completes.
- Use `-Serial` to fall back to sequential builds in the shared `build/`
  directory.
- Pass `-Extended` to also run the imported c-testsuite single-exec corpus
  (via `runall-extended.ps1`) after the main suite.
- The lightweight stack-overflow guard (`-fstack-check`) is **on by default**;
  pass `-NoStackCheck` to build without it.
- Pass `-Report` to append per-app run time and `.COM` size measurements to a
  CSV report. Report mode implies `-NoStackCheck`.

### Test Runner Usage

```pwsh
./scripts/runall.ps1 [options]
```

With no options, the suite runs in parallel, enables `-fstack-check`, and uses
`-Mode fast`. Use `-Mode full` to run both optimized and unoptimized builds.

### Test Runner Examples

```pwsh
./scripts/runall.ps1                       # quick optimized-only default
./scripts/runall.ps1 -Help                 # show help and exit
./scripts/runall.ps1 -Serial               # sequential fallback
./scripts/runall.ps1 -NoStackCheck         # build without the stack guard
./scripts/runall.ps1 -ThrottleLimit 8      # cap concurrency
./scripts/runall.ps1 -Emulator altair
./scripts/runall.ps1 -Mode fast            # optimized build only
./scripts/runall.ps1 -Mode nopeep          # unoptimized build only
./scripts/runall.ps1 -Extended             # also run extended c-testsuite
./scripts/runall.ps1 -KeepBuild            # keep build/run-<pid>/ for debugging
./scripts/runall.ps1 -Report               # also append perf_results.csv
```

### Build Modes

The `-Mode` parameter selects which optimization pass(es) to build and verify.
**The default is `fast`.**

- **`fast`** — optimized: runs the `dccpeep` peephole optimizer after compiling.
  This produces the optimized CP/M Z80 binary.
- **`nopeep`** — unoptimized: skips `dccpeep`. This produces the unoptimized
  CP/M Z80 binary.
- **`full`** — builds and verifies each app **twice**, once in each mode,
  against the same baseline. This catches optimizer bugs that change a program's
  output.

### Test Runner Parameters

| Parameter | Default | Purpose |
| --------- | ------- | ------- |
| `-Emulator` | `ntvcm` | Emulator command for running .COM files |
| `-NoStackCheck` | (off) | Disable `-fstack-check` (the guard is ON by default) |
| `-BuildDir` | `build` | Build directory for artifacts |
| `-BaselineDir` | `tests/baselines` | Directory of per-app `<app>.txt` baselines |
| `-Mode` | `fast` | Build mode: `fast` (optimized), `nopeep` (unoptimized), or `full` |
| `-Help` | (off) | Show help text and exit without building or running tests |
| `-Extended` | (off) | Also run the extended c-testsuite corpus after the main suite |
| `-Serial` | (off) | Run sequentially instead of the default parallel mode |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
| `-KeepBuild` | (off) | Keep the per-invocation `build/run-<pid>/` folder instead of removing it on exit (parallel mode) |
| `-Report` | (off) | Append per-app execution time and `.COM` size metrics to a CSV report; implies `-NoStackCheck` |
| `-ReportFile` | `perf_results.csv` | CSV path used by `-Report` |
| `-ReportClockHz` | `1000000000` | ntvcm clock speed used for measured app runs in report mode; set to `0` for full-speed report runs |

### Output

Reports:

- Total apps discovered
- Passed/failed/skipped counts
- Per-app build and execution status (live in parallel mode)
- Output verification against baseline
- Optional CSV performance report when `-Report` is passed
- Exit code 0 on success, 1 on failure

## Host Unit Test Validator (`validate-unit-test.ps1`)

Compiles each `tests/*.c` program with a native host C compiler, runs the host
executable, and compares stdout with `tests/baselines/<app>.txt`. This is a
read-only baseline check: it never rewrites baseline files. It is useful for
checking that the unit-test sources and expected output still make sense on a
normal C implementation before comparing them with the DCC C Compiler output.

Host compiler selection follows `scripts/build-dcc.ps1`:

- Windows uses MSVC `cl.exe` after locating the Visual Studio C++ build tools.
  On Windows ARM64, it uses the native ARM64 MSVC tools.
- macOS uses `clang` by default.
- Linux uses `gcc` by default.
- Unix-like hosts can override the compiler with `-CC` or the `CC` environment
  variable, for example `-CC clang` or `CC=clang`.

Tests that need CP/M or Z80-only behavior, such as BDOS calls, direct port I/O,
`getch`/`kbhit`, inline `#asm`, or CP/M vector reads, are skipped because a host
compiler cannot run those semantics. The script also honors
`tests/_test_overrides.json` for app arguments, stdin, ignored apps, and
host-only skip settings.

### Host Validator Usage

```pwsh
./scripts/validate-unit-test.ps1 [options]
```

### Host Validator Examples

```pwsh
./scripts/validate-unit-test.ps1              # validate every runnable test
./scripts/validate-unit-test.ps1 -App tprintf # validate one test app
./scripts/validate-unit-test.ps1 -CC clang    # use clang on Linux/macOS
./scripts/validate-unit-test.ps1 -Help        # show help and exit
```

### Host Validator Parameters

| Parameter | Default | Purpose |
| --------- | ------- | ------- |
| `-BuildDir` | `build/host-validate` | Directory for host compiler outputs |
| `-BaselineDir` | `tests/baselines` | Directory of per-app `<app>.txt` baselines |
| `-CC` | platform default | C compiler override on macOS/Linux; ignored on Windows |
| `-App` | all tests | Validate one test app, without the `.c` extension |
| `-RunTimeout` | `10` | Seconds to allow each host executable to run |
| `-Help` | (off) | Show help text and exit without building or running tests |

### Linux 32-bit Validation

On Linux, the validator can extend coverage by using GCC's `-m32` mode when the
compiler can build and link 32-bit executables. The script probes this
automatically: if the probe succeeds, Linux GCC host validations run with
`-m32`; if it fails, the script keeps using the normal compiler mode.

This matters because the DCC C Compiler has 16-bit pointers and 32-bit `long`, so a 32-bit host
build can run a few host-only tests that are skipped on a normal 64-bit Linux
compiler. Install the normal C build tools plus the 32-bit development libraries
for your distribution, then rerun the validator.

Common Linux packages:

| Distribution | Command |
| ------------ | ------- |
| Debian/Ubuntu | `sudo apt update && sudo apt install build-essential gcc-multilib libc6-dev-i386` |
| Fedora | `sudo dnf groupinstall "Development Tools" && sudo dnf install glibc-devel.i686 libgcc.i686` |
| RHEL/CentOS | `sudo dnf groupinstall "Development Tools" && sudo dnf install glibc-devel.i686 libgcc.i686` |
| Arch | Enable the `multilib` repository, then `sudo pacman -S base-devel lib32-glibc` |
| openSUSE | `sudo zypper install -t pattern devel_C_C++ && sudo zypper install gcc-32bit glibc-devel-32bit` |

After installation, this should be enough to enable the extended path:

```pwsh
./scripts/validate-unit-test.ps1
```

The script prints the selected compiler line near the start of the run. When the
32-bit probe succeeds on Linux GCC, that line includes `(-m32)`.

## Test Overrides (`tests/_test_overrides.json`)

Per-test run configuration used by `runall.ps1`.
It lives in the `tests/` folder (alongside the test sources it configures) and
is named with a leading underscore so it sorts to the top of the directory.

Most tests need no entry — they compile cleanly, take no arguments, and use the
default 512-byte stack. This file only lists the exceptions.

### Schema

The file is a single JSON object with an `apps` array. Each element configures
one test, keyed by `name`:

```json
{
  "apps": [
    { "name": "<app>", "args": "<string>", "stdin": "<string>", "stack_size": <int>, "dcc_args": "<string>", "dcc_floatio": <bool>, "dcc_longio": <bool>, "ignore": <bool> }
  ]
}
```

| Property | Type | Required | Default | Purpose |
| -------- | ---- | -------- | ------- | ------- |
| `name` | string | yes | — | Test name, without the `.c` extension (e.g. `ttt`, `cobint`) |
| `args` | string | no | `""` | Command-line arguments passed to the program when run. Multi-token strings are split on whitespace (e.g. `"a bb ccc"`) |
| `stdin` | string | no | `""` | Text piped to the program's standard input during execution (for keyboard/input-driven tests) |
| `stack_size` | integer | no | `512` | C stack reserve in bytes, passed to `dcc` as `-stack`. Used by recursive apps that need more headroom |
| `dcc_args` | string | no | `""` | Extra DCC C Compiler build arguments passed through `dccmake` (for example `-DNAME=1 -UOLD`) |
| `dcc_floatio` | boolean | no | environment/default | When set, controls `dccmake` `dcc-floatio` and dcc `-ffloatio` for this app |
| `dcc_longio` | boolean | no | environment/default | When set, controls `dccmake` `dcc-flongio` and dcc `-flongio` for this app |
| `ignore` | boolean | no | `false` | When `true`, the test is skipped entirely (not built or run) |

Entries with none of the optional properties have no effect, so an app only
appears here if it overrides at least one default.

### Example

```json
{
  "apps": [
    { "name": "ttt", "args": "10" },
    { "name": "pint", "args": "e.pas" },
    { "name": "tkbd", "stdin": "x" },
    { "name": "cobint", "args": "e.cob", "stack_size": 1536 },
    { "name": "triangle", "stack_size": 768 },
    { "name": "na", "ignore": true },
    { "name": "tc89fltb", "ignore": true },
    { "name": "spsmash", "ignore": true }
  ]
}
```

### Common reasons to add an entry

- **Program reads a data file** — interpreters like `pint`, `cobint`, `forint`
  take a fixture filename as `args` (e.g. `e.pas`, `e.cob`).
- **Program reads from stdin** — set `stdin` for tests that require scripted
  keyboard/input text (for example, `tkbd` expects `x`).
- **Deep recursion** — apps such as `triangle` (768) and `cobint` (1536) need a
  larger `stack_size` than the 512-byte default, especially under
  `-fstack-check`.
- **Cannot be auto-tested** — set `ignore: true` for interactive programs
  (`na`, an editor that waits for keystrokes), tests that intentionally fail to
  compile (`tc89fltb`), or deliberate stack-smashers (`spsmash`).

To change a test's run behavior, edit `tests/_test_overrides.json` and re-run
the suite. See also `tests/README.md` in the repository for how tests,
baselines, and this file relate.

## Performance Reporting (`runall.ps1 -Report`)

`runall.ps1 -Report` collects performance data during the normal verified test
suite. It appends per-app execution time and `.COM` size metrics to a CSV report,
while still checking output against the usual baselines.

Report mode implies `-NoStackCheck` so timings reflect normal builds. When using
`ntvcm`, measured app runs use a fixed 1 GHz emulator clock by default; set
`-ReportClockHz 0` for full-speed report runs.

```pwsh
./scripts/runall.ps1 -Report
./scripts/runall.ps1 -Report -ReportFile results.csv
./scripts/runall.ps1 -Report -ReportClockHz 0
```

Results are written to `perf_results.csv` by default. **Results append to the
file**, so each report run adds a new row per app:

```csv
machine,utc-timestamp,app,peep_ms,peep_size,nopeep_ms,nopeep_size
mycomputer,2026-06-16T07:18:39Z,tstring,17000,6400,19000,6912
mycomputer,2026-06-16T07:18:39Z,sieve,1000,2176,3000,2304
mycomputer,2026-06-16T07:24:21Z,tstring,17000,6400,17000,6912
mycomputer,2026-06-16T07:24:21Z,sieve,1000,2176,3000,2304
```

**Columns:**

- `machine` — Name of the machine running the benchmark
- `utc-timestamp` — UTC timestamp (ISO 8601 format, e.g., `2026-06-16T07:18:39Z`)
- `app` — Application name
- `peep_ms` — Execution time in milliseconds (optimized with dccpeep)
- `peep_size` — Binary size in bytes (optimized)
- `nopeep_ms` — Execution time in milliseconds (unoptimized)
- `nopeep_size` — Binary size in bytes (unoptimized)

The `-ReportFile` parameter controls the output path. The `-Mode` parameter
controls which CSV columns are populated: `-Mode full` fills both `peep_*` and
`nopeep_*`; single-mode runs fill only the selected mode's columns. In the CSV,
`peep_*` columns hold optimized-build measurements.

## Stack Size Measurement (`stacksize.sh` / `stacksize.bat`)

Finds the minimum C stack reserve an app needs under DCC C Compiler's lightweight
stack-overflow guard (`-fstack-check`). See the
[Building and linking](../02-build-and-link.md#measuring-the-stack-an-app-needs)
section for full documentation, or run `scripts/stacksize.sh --help`.
