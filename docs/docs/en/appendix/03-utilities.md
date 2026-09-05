# Utilities

Developer scripts for building and testing DCC C Compiler programs.

Use the source-built [`dccmake`](#build-pipeline-helper-dccmake) tools for
application projects. Run the optional scripts below from the DCC C Compiler
checkout. The Linux/macOS single-app shell helper does not require PowerShell.

## Build Driver (`ma.sh` / `ma.ps1`)

The build driver compiles one app, optionally runs `dccpeep`, strips the
runtime, assembles, and links a `.COM` executable.

Use `scripts/ma.sh` on Linux/macOS, or `scripts/ma.ps1` with Windows PowerShell
5.1 or PowerShell 7+. These are optional checkout helpers; use `dccmake`
directly for project builds.

### Build Driver Usage

```pwsh
./scripts/ma.ps1 <name> [mode] [options]
```

```sh
./scripts/ma.sh <name> [mode] [options]
```

- `<name>` — Test app name (e.g., `triangle`, `sieve`, `ttt`)
- `[mode]` — Build mode: `full` (both builds), `fast` (optimized), or `nopeep`
  (without the additional peephole pass). The shell driver defaults to `fast`; the PowerShell driver
  defaults to `full`.

### Build Driver Examples

```pwsh
./scripts/ma.ps1 triangle
./scripts/ma.ps1 sieve nopeep
./scripts/ma.ps1 cobint -Mode fast -BuildDir mybuild
```

```sh
./scripts/ma.sh triangle
./scripts/ma.sh sieve nopeep
./scripts/ma.sh cobint --mode fast --build-dir mybuild
```

### Build Driver Parameters

| Parameter | Default | Purpose |
| --------- | ------- | ------- |
| `-Name` / positional name | (required) | App name without `.c` extension |
| `-Mode` / `--mode` | `fast` (shell), `full` (PowerShell) | Build mode: `full`, `fast`, or `nopeep` |
| `-SourcePath` / `--source-path` | Search by name | Explicit C source path |
| `-BuildDir` / `--build-dir` | `build` | Build directory for artifacts |
| `-Emulator` / `--emulator` | `ntvcm` | Emulator command for CP/M tools |
| `-UseEmulatedM80` / `--emulated-m80` | off | Assemble with M80.COM under `ntvcm` instead of native [`m80c`](#native-assembler-m80c) |
| `-UseEmulatedL80` / `--emulated-l80` | off | Link with L80.COM under `ntvcm` instead of native [`l80c`](#native-linker-l80c) |

The wrapper accepts only the parameters above. Use `dccmake` directly for
project settings such as debug builds, per-format overrides, and multi-module
input lists.

### Environment Variables

- `DCC_STACK_SIZE` — C stack reserve in bytes; when unset, `dcc` uses its default
- `DCC_FORCE_STACK_CHECK` — Force `-fstack-check` on all builds
- `DCC_FLOATIO` — Set to `1` to force `%f` support on every `printf`-family call
- `DCC_LONGIO` — Set to `1` to force long-format support on every `printf`-family call
- `DCC_USE_EMULATED_M80`, `DCC_USE_EMULATED_L80` — Set to `1` to select the
  real CP/M assembler or linker under `ntvcm`
- `DCC_ARGS` — Extra whitespace-separated `dcc` options such as `-DNAME=1 -UOLD`
- `NTVCM_ARGS` — Extra whitespace-separated `ntvcm` options such as `-p -s:4000000`
- `DCC_HOME` — optional toolchain asset root; used to find `include/`, `lib/`, and CP/M tools
- `DCC_INCLUDE` — extra include directories, separated by the host path separator
- `DCC_LIB` — extra runtime/tool asset roots, separated by the host path separator
- `DCC_RUNTIME` — explicit path to `DCCRTL.MAC`
- `DCC`, `DCCPEEP`, `DCCRTLSTRIP`, `NTVCM`, `M80`, `M80C`, `L80`, `L80C` — Tool paths

Run `./scripts/ma.ps1 -Help` on Windows or `./scripts/ma.sh --help` on Linux/macOS for the full option map, including which
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
| `dccrtlstrip` | Application/runtime stripper | Host command that removes unreachable marked app blocks and writes a reduced runtime; see [DCCRTL strip appendix](01-dccrtlstrip.md) |
| `DCCRTL.MAC` | Runtime source | Full CP/M runtime consumed by `dccrtlstrip` |
| [`m80c`](#native-assembler-m80c) | Native assembler | Host command, LINK-80-`.REL`-compatible; default assembler, no `ntvcm` needed |
| [`l80c`](#native-linker-l80c) | Native linker | Host command, consumes the same `.REL` format; default linker, no `ntvcm` needed |
| `ntvcm` | CP/M emulator | Only needed for the real M80/L80 fallback path, and to run the final `.COM` programs |
| `m80.com` | CP/M assembler | Real Microsoft assembler; assembles `.MAC` to `.REL` under `ntvcm` when `dcc-use-emulated-m80=true` |
| `l80.com` | CP/M linker | Real Microsoft linker; links `.REL` files to `.COM` under `ntvcm` when `dcc-use-emulated-l80=true` |

## Native Assembler (`m80c`)

`m80c` is the host-native assembler used by the normal DCC build pipeline. It
accepts the 8080 and Z80 source forms used by DCC, performs two assembly passes,
and writes LINK-80-compatible `.REL` objects without running Microsoft
`M80.COM` under an emulator.

### m80c CLI usage

```text
m80c [rel-output[,listing-output]]=source[.MAC] [options]
```

An omitted extension defaults to `.MAC`, `.REL`, or `.PRN` as appropriate.
Use `*` in an output position to suppress that explicit name. Output basenames
use CP/M-style uppercase spelling.

Common examples:

```sh
m80c "=FOO.MAC" /X /O /Z /L
m80c "FOO.REL,FOO.PRN=FOO.MAC" /Z
m80c "=FOO.MAC" /X /O /Z /L /C
```

| Option | Default | Purpose |
| ------ | ------- | ------- |
| `/Z` | on | Select Z80 mnemonic interpretation. For example, operand-free `CPI` is the Z80 block-compare instruction. |
| `/I` | off | Select Intel 8080 mnemonic interpretation. For example, `CPI value` is immediate compare. |
| `/L` | off | Write the `.PRN` assembly listing. |
| `/R` | when a REL output is named | Request `.REL` object output. |
| `/O` | off | Request `.REL` object output in the conventional M80 command form. Native listings remain hexadecimal. |
| `/H` | accepted | Select hexadecimal listing compatibility; hexadecimal is already the native listing format. |
| `/M` | off | Materialize `DS` storage as zero bytes instead of leaving reserved gaps. |
| `/C` | off | Write the per-module `.SYM` sidecar used by native [`l80c`](#native-linker-l80c) to preserve relocated local symbols. |
| `/X` | accepted | Accepted for M80 command compatibility; native `m80c` has no separate cross-reference output mode. |

The assembler always writes `.LNK` segment-size metadata. When DCC source debug
markers are present, it also writes `.DBG` source/symbol metadata; `/C` is not
required for `.DBG`. Assembly errors are recorded in the listing when `/L` is
enabled and cause a nonzero exit status.

## Native Linker (`l80c`)

`l80c` is the host-native linker used by the normal DCC build pipeline. It
consumes the LINK-80-compatible `.REL` files produced by
[`m80c`](#native-assembler-m80c), resolves
PUBLIC/EXTRN symbols, relocates CSEG and DSEG values, and writes a CP/M `.COM`
image plus a linked `.SYM` file. Unlike `L80.COM`, it uses host memory rather
than CP/M's 64K address space, so large links do not exhaust the linker's own
workspace.

### l80c CLI usage

```text
l80c [/P:origin,]module1,module2,...,output/N/E/Y [-o output[.COM]] [-v]
```

Module and output names conventionally use CP/M uppercase spelling. `.REL` is
added to module names automatically. Repeating the final module with `/N/E/Y`,
as in a Microsoft LINK-80 command, does not link it twice.

| Option | Default | Purpose |
| ------ | ------- | ------- |
| `/P:<hex-address>` | `100` | Set the linked program origin. The address must fit in 16 bits. |
| `output/N/E/Y` | Last module | Select the output basename using conventional LINK-80 syntax. `/N`, `/E`, and `/Y` do not otherwise change native non-interactive linking. |
| `-o <name>` | Output marker or last module | Select the output basename explicitly. Either `APP` or `APP.COM` produces `APP.COM` and `APP.SYM`. |
| `-v` | off | Print loaded modules, origin, linked size, and output path. |

The normal DCC command is:

```sh
l80c "/P:100,RTLMIN,FOO,FOO/N/E/Y"
```

### Program origins

Use `/P:100` for ordinary CP/M applications. CP/M loads a headerless `.COM`
file at `0100H`, so this origin writes the linked program directly with no
entry wrapper.

`l80c` also implements LINK-80-compatible nonstandard origins:

- `0101H` or `0102H`: place the program at that address without generating a
  jump because the program occupies part of LINK-80's `0100H`-`0102H` entry
  slot.
- Above `0102H`: put `JP <start-address>` at `0100H`, pad to the requested
  origin, and place the relocated program there.
- Below `0100H`: store the payload after the CP/M entry point and add a small
  8080/Z80-compatible bootstrap that copies it to the linked origin before
  jumping to the program start.

Nonstandard origins are useful for fixed-address code, overlays or separately
loaded modules, programs reserving low TPA space for a loader or shared data,
and system utilities that deliberately relocate or take over the machine.
High origins remain normal CP/M programs through the generated entry jump.
Low origins, especially zero, overwrite CP/M low memory and therefore suit only
specialized programs that do not expect CP/M services to remain intact.

!!! important "Not a bare-metal output mode"
    `/P:0` still produces a CP/M-loadable `.COM` with an entry/copy bootstrap.
    It does not emit a raw image beginning at file offset zero. A future
    bare-metal target should use a separate explicit raw-binary option so its
    startup, memory map, stack, and runtime assumptions are unambiguous.

Native `l80c` currently supports the CSEG/DSEG REL records used by DCC. It
rejects ASEG, COMMON, malformed records, undefined externals, duplicate globals,
and images extending past `FFFFH` rather than guessing and producing a bad
executable.

## Build Pipeline Helper (`dccmake`)

`dccmake` is the lower-level build helper used by the test runner and by
repeatable local builds. It compiles one or more C source files, optionally runs
`dccpeep`, removes unreachable application functions and objects, strips the
runtime with `dccrtlstrip`, then assembles and links with
native [`m80c`](#native-assembler-m80c)/[`l80c`](#native-linker-l80c) by
default (or the real M80/L80 under `ntvcm` when
`dcc-use-emulated-m80`/`dcc-use-emulated-l80` is set - real L80 runs inside
`ntvcm`'s emulated 64K CP/M address space, so its own symbol/relocation
workspace can run out of memory on large `nopeep` builds well before the
target program itself would not fit; [`l80c`](#native-linker-l80c) has no such
ceiling).

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
dccmake tests/tprintf.c dcc-output=TPRINTF -ffloatio  # blanket force-on override
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
dcc-peep=true
dcc-build-dir=build
dcc-runtime=${DCC_DIR}/DCCRTL.MAC
dcc-include-directory=${DCC_DIR}
dcc-tool=${DCC_DIR}/dcc
dccpeep-tool=${DCC_DIR}/dccpeep
dccrtlstrip-tool=${DCC_DIR}/dccrtlstrip
m80c-tool=${DCC_DIR}/m80c
l80c-tool=${DCC_DIR}/l80c
```

With that file in place, set the tool roots and build the app:

```sh
export DCC_DIR="$HOME/GitHub/dcc"

dccmake
```

On Windows, set `DCC_DIR` in PowerShell and append `.exe` to the five host-tool
paths. Emulator paths are unnecessary for the default native build pipeline.

The generated CP/M executable lands in the configured build directory, for
example `build/ATTNC99.COM`. To place that executable beside your source,
copy just the selected output:

```sh
cp build/ATTNC99.COM .
```

For source debugging, keep its matching `.DBG` alongside it. Preserve the
build directory when it contains other applications, fixtures, or outputs you
still need.

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
| `dcc-floatio` | `false` | Force `%f` support on every `printf`-family call when true; literal formats are normally detected per call |
| `dcc-no-floatio` | `false` | Force `%f` support off even for matching literals or the non-literal fallback |
| `dcc-flongio` | `false` | Force long-format support on every `printf`-family call when true; literal formats are normally detected per call |
| `dcc-no-longio` | `false` | Force long-format support off even for matching literals or the non-literal fallback |
| `dcc-hexio` | `false` | Force `%x`/`%X` support on every `printf`-family call |
| `dcc-no-hexio` | `false` | Force `%x`/`%X` support off, even for matching formats |
| `dcc-octio` | `false` | Force `%o` support on every `printf`-family call |
| `dcc-no-octio` | `false` | Force `%o` support off, even for matching formats |
| `dcc-stack-bytes` | `512` | Stack reserve passed to `dcc` with `-stack` |
| `dcc-stack-check` | Environment/default | Pass `-fstack-check` to `dcc` |
| `dcc-no-narrow` | `false` | Pass `-fno-narrow` to disable byte-narrowing passes |
| `dcc-debug` | `false` | `true` emits full conservative debug metadata; `lines` emits release-identical optimized metadata with ranged variable locations. Both require native [`m80c`](#native-assembler-m80c) |
| `dcc-include-directory` | Auto-adds `.` when standard headers are in the current directory | Comma-separated include directories; `dcc-include` is an alias |
| `dcc-define` | none | Comma-separated `NAME[=value]` entries passed to `dcc` as `-D`; `dcc-defines` is an alias |
| `dcc-undefine` | none | Comma-separated names passed to `dcc` as `-U`; `dcc-undefines` is an alias |
| `dcc-peep` | `true` | Run `dccpeep` after compiling each `.MAC` file |
| `dcc-peep-debug` | `false` | Run `dccpeep` over full conservative `-g` output; use `dcc-debug=lines` for release-identical optimized code |
| `dcc-strip-unused` | `true` | Remove application functions and objects unreachable across all input modules. Set `false` when producing a module intended for a separate later link |
| `dcc-build-dir` | `build` | Artifact directory |
| `dcc-runtime` | `DCC_RUNTIME`, local `DCCRTL.MAC`, or `DCCRTL.MAC` | Runtime source passed to `dccrtlstrip` |
| `dcc-tool` | `DCC`, local `dcc`, or `dcc` | DCC compiler command |
| `dccpeep-tool` | `DCCPEEP`, local `dccpeep`, or `dccpeep` | Peephole optimizer command |
| `dccrtlstrip-tool` | `DCCRTLSTRIP`, local `dccrtlstrip`, or `dccrtlstrip` | Application/runtime stripper command |
| `ntvcm-tool` | `NTVCM` or `ntvcm` | Emulator command used to run M80/L80 (only when either is emulated) |
| `m80-command` | `M80` or `m80` | CP/M assembler command passed to `ntvcm`; emulated-M80 path only |
| `m80c-tool` | `M80C`, local `m80c`, or `m80c` | Native host assembler command (default, no `ntvcm`); see [`m80c`](#native-assembler-m80c) |
| `dcc-use-emulated-m80` | `false` | Assemble with real `M80.COM` under `ntvcm` instead of native [`m80c`](#native-assembler-m80c) |
| `l80-command` | `L80` or `l80` | CP/M linker command passed to `ntvcm`; emulated-L80 path only |
| `l80c-tool` | `L80C`, local `l80c`, or `l80c` | Native host linker command (default, no `ntvcm`); see [`l80c`](#native-linker-l80c) |
| `dcc-use-emulated-l80` | `false` | Link with real `L80.COM` under `ntvcm` instead of native [`l80c`](#native-linker-l80c) |

`dccmake` initializes its Boolean settings from the matching environment
variables before reading `dccmake.txt`: `DCC_FLOATIO`, `DCC_NO_FLOATIO`,
`DCC_LONGIO`, `DCC_NO_LONGIO`, `DCC_HEXIO`, `DCC_NO_HEXIO`, `DCC_OCTIO`,
`DCC_NO_OCTIO`, `DCC_FORCE_STACK_CHECK`, `DCC_NO_NARROW`, `DCC_DEBUG`,
`DCC_DEBUG_LINES`,
`DCC_PEEP_DEBUG`, `DCC_USE_EMULATED_M80`, and
`DCC_USE_EMULATED_L80`. File settings and then command-line settings override
those defaults.

With all formatted-I/O settings at their default `false`, `dccmake` passes no
override to dcc and adds no forced keep root to `dccrtlstrip`. dcc therefore
performs its normal per-call detection for float, long, hexadecimal, and octal
formats. The force-on settings are neutral when false; use a `dcc-no-*io`
setting only when that format support must be forced off.

Source input basenames and the output name must be CP/M 8.3-clean. For example,
`module1.c` is valid, but a generated module output base longer than eight
characters is not.

### dccmake dcc-style Options

| Option | Equivalent setting |
| ------ | ------------------ |
| `-f`, `-ffloatio` | `dcc-floatio=true` |
| `-fno-floatio` | `dcc-no-floatio=true` |
| `-fl`, `-flongio` | `dcc-flongio=true` |
| `-fno-longio` | `dcc-no-longio=true` |
| `-fhexio` | `dcc-hexio=true` |
| `-fno-hexio` | `dcc-no-hexio=true` |
| `-foctio` | `dcc-octio=true` |
| `-fno-octio` | `dcc-no-octio=true` |
| `-s <bytes>`, `-stack <bytes>`, `-stack=<bytes>` | `dcc-stack-bytes=<bytes>` |
| `-fstack-check` | `dcc-stack-check=true` |
| `-fno-narrow` | `dcc-no-narrow=true` |
| `-g` | `dcc-debug=true` |
| `-gline` | `dcc-debug=lines` |
| `-femulated-m80` | `dcc-use-emulated-m80=true` |
| `-femulated-l80` | `dcc-use-emulated-l80=true` |
| `-I <dir>`, `-Idir` | Add an include directory |
| `-D <name>[=value]`, `-Dname=value` | Pass a define to `dcc` |
| `-U <name>`, `-Uname` | Pass an undefine to `dcc` |
| `-v`, `--version` | Print `dccmake` version |

`-c` and `-module` are rejected because `dccmake` decides module mode from the
input order.

## Peephole Optimizer (`dccpeep`)

`dccmake` runs `dccpeep` after compilation when `dcc-peep=true`. It can also be
invoked directly:

```sh
dccpeep [-Ot|-Os] [-fstats] input.mac output.mac
```

| Option | Purpose |
| ------ | ------- |
| `-Ot` | Optimize for execution time (default) |
| `-Os` | Optimize for code size |
| `-fstats` | Print optimization statistics |

## Test Suite Runner (`runall.ps1`)

Builds and runs the test suite against per-app baselines in `tests/baselines/`.
It uses `dccmake` for builds and `tests/_test_overrides.json` for test-specific
runtime arguments, stack sizes, and optional DCC C Compiler build flags.

Runs in parallel by default:

- Each app builds in its own directory below a per-invocation
  `<build-root>/run-<pid>/` folder, so concurrent builds do not clobber shared
  artifacts.
- The per-invocation folder is removed automatically on exit; pass `-KeepBuild`
  to retain it. On Linux, the build root defaults to `/dev/shm/dcc-runall` when
  available; elsewhere it defaults to `build`.
- Failures print as each app completes. PASS lines are suppressed by default;
  pass `-FailuresOnly:$false` to show every result.
- Use `-Serial` to fall back to sequential builds in the shared `build/`
  directory.
- Pass `-Extended` to also run the imported c-testsuite single-exec corpus
  (via `runall-extended.ps1`) after the main suite.
- The lightweight stack-overflow guard (`-fstack-check`) is **on by default**;
  pass `-NoStackCheck` to build without it.
- Z80 cycle counts and `.COM` sizes are checked against
  `tests/perf_baselines.csv` by default, with no separate benchmark pass. Use
  `-NoPerfCheck` to skip this check or `-UpdatePerfBaseline` after an intentional
  measured change.
- Pass `-Report` to append cycles, `.COM` sizes, and clock-normalized times to a
  historical CSV report. Report mode implies `-NoStackCheck`.

### Test Runner Usage

```pwsh
./scripts/runall.ps1 [options]
```

With no options, the suite runs in parallel, enables `-fstack-check`, and uses
`-Mode fast`. Use `-Mode full` to run builds with and without `dccpeep`.

### Test Runner Examples

```pwsh
./scripts/runall.ps1                       # quick optimized-only default
./scripts/runall.ps1 -Help                 # show help and exit
./scripts/runall.ps1 -Serial               # sequential fallback
./scripts/runall.ps1 -NoStackCheck         # build without the stack guard
./scripts/runall.ps1 -ThrottleLimit 8      # cap concurrency
./scripts/runall.ps1 -Emulator altair
./scripts/runall.ps1 -Mode fast            # optimized build only
./scripts/runall.ps1 -Mode nopeep          # build without dccpeep only
./scripts/runall.ps1 -Apps tprintf,tlong   # selected apps only
./scripts/runall.ps1 -FailFast             # stop dispatching after a failure
./scripts/runall.ps1 -FailuresOnly:$false  # include PASS lines
./scripts/runall.ps1 -Extended             # also run extended c-testsuite
./scripts/runall.ps1 -KeepBuild            # keep the per-run build folder
./scripts/runall.ps1 -Report               # also append perf_results.csv
./scripts/runall.ps1 -Mode full -UpdatePerfBaseline
```

### Build Modes

The `-Mode` parameter selects which optimization pass(es) to build and verify.
**The default is `fast`.**

- **`fast`** — optimized: runs the `dccpeep` peephole optimizer after compiling.
  This produces the optimized CP/M Z80 binary.
- **`nopeep`** — skips `dccpeep`. Compiler MIR optimizations and application
  stripping still run; this is not an `-O0` build.
- **`full`** — builds and verifies each app **twice**, once in each mode,
  against the same baseline. This catches optimizer bugs that change a program's
  output.

### Test Runner Parameters

| Parameter | Default | Purpose |
| --------- | ------- | ------- |
| `-Emulator` | `ntvcm` | Emulator command for running .COM files |
| `-NoStackCheck` | (off) | Disable `-fstack-check` (the guard is ON by default) |
| `-UseEmulatedM80` | (off) | Assemble with M80.COM under `ntvcm` instead of native [`m80c`](#native-assembler-m80c) |
| `-UseEmulatedL80` | (off) | Link with L80.COM under `ntvcm` instead of native [`l80c`](#native-linker-l80c) |
| `-BuildDir` | `build` | Build directory for artifacts |
| `-NoRamDisk` | (off) | On Linux, disable the automatic `/dev/shm/dcc-runall` build root |
| `-BaselineDir` | `tests/baselines` | Directory of per-app `<app>.txt` baselines |
| `-Mode` | `fast` | Build mode: `fast` (with dccpeep), `nopeep` (without dccpeep), or `full` (both) |
| `-Apps` | all tests | Comma-separated app names to run |
| `-RunTimeout` | `60` | Per-build and per-emulator-run timeout in seconds |
| `-Help` | (off) | Show help text and exit without building or running tests |
| `-Extended` | (off) | Also run the extended c-testsuite corpus after the main suite |
| `-Serial` | (off) | Run sequentially instead of the default parallel mode |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
| `-KeepBuild` | (off) | Keep the per-invocation run folder instead of removing it on exit (parallel mode) |
| `-FailFast` | (off) | Stop dispatching new apps after the first correctness or performance failure |
| `-FailuresOnly` | on | Suppress PASS lines; pass `-FailuresOnly:$false` for full output |
| `-Report` | (off) | Append cycle, normalized-time, and `.COM` size metrics to a CSV report; implies `-NoStackCheck` |
| `-ReportFile` | `perf_results.csv` | CSV path used by `-Report` |
| `-ReportClockHz` | `400000000` | Nominal clock used to derive report milliseconds from Z80 cycles; does not throttle execution |
| `-NoPerfCheck` | (off) | Skip the default cycle-count and `.COM` size regression check |
| `-UpdatePerfBaseline` | (off) | Update checked performance columns for the modes built by this run |
| `-PerfBaselineFile` | `tests/perf_baselines.csv` | Checked cycle-count and `.COM` size baseline |
| `-NarrowDiff` | (off) | Compare normal and `-fno-narrow` runs to detect narrowing behavior changes |
| `-TimingBreakdown` | (off) | Print suite-phase and aggregate build-pipeline timing percentages |

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
./scripts/validate-unit-test.ps1 -Serial      # sequential fallback
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
| `-Serial` | (off) | Run sequentially instead of the default parallel mode |
| `-ThrottleLimit` | CPU core count | Max concurrent apps in parallel mode |
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
    {
      "name": "<app>",
      "args": "<string>",
      "fixtures": ["<file>"],
      "extra_scenarios": [
        { "suffix": "<name>", "args": "<string>", "fixtures": ["<file>"] }
      ]
    }
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
| `dcc_floatio` | boolean | no | environment/default | True forces `-ffloatio`; false leaves per-call auto-detection active for this app |
| `dcc_longio` | boolean | no | environment/default | True forces `-flongio`; false leaves per-call auto-detection active for this app |
| `fixtures` | string array | no | `[]` | Files from `tests/` copied into the app build directory under uppercase CP/M names |
| `extra_scenarios` | object array | no | `[]` | Additional argument/input/fixture scenarios run against the same built `.COM` |
| `ignore` | boolean | no | `false` | When `true`, the test is skipped entirely (not built or run) |
| `perf_ignore` | boolean | no | `false` | Exclude nondeterministic apps from cycle-count regression checks |
| `narrow_diff_ignore` | boolean | no | `false` | Exclude layout-sensitive apps from `-NarrowDiff` |
| `host` | boolean | no | `false` | Skip ordinary host validation; with the 32-bit requirement below, allow only the Linux `-m32` path |
| `requires-32bit-linux-host-compiler` | boolean | no | `false` | With `host: true`, allow host validation only when Linux GCC's probed `-m32` mode is active |
| `requires-non-msvc-host-compiler` | boolean | no | `false` | Skip host validation under MSVC |
| `requires-non-macos-host-compiler` | boolean | no | `false` | Skip host validation on macOS |
| `host-cflags` | string | no | `""` | Replace the default GCC/Clang flags for this app's host build |

Each `extra_scenarios` object accepts a required `suffix` plus optional `args`,
`stdin`, and `fixtures`. It reuses the primary `.COM` and compares output with
`tests/baselines/<app>_<suffix>.txt`. Entries with none of the optional
properties have no effect, so an app only appears here if it overrides at least
one default.

### Example

```json
{
  "apps": [
    {
      "name": "pint",
      "args": "e.pas",
      "stack_size": 768,
      "fixtures": ["E.PAS"],
      "extra_scenarios": [
        { "suffix": "ttt", "args": "ttt.pas", "fixtures": ["TTT.PAS"] }
      ]
    },
    { "name": "tkbd", "stdin": "x", "perf_ignore": true },
    { "name": "tstackov", "host": true, "narrow_diff_ignore": true },
    { "name": "na", "ignore": true }
  ]
}
```

### Common reasons to add an entry

- **Program reads a data file** — declare it in `fixtures` and pass its CP/M
  name in `args`; only that app's build directory receives the file.
- **One binary needs several datasets** — use `extra_scenarios` to rerun the
  same `.COM` with separate arguments, fixtures, and baselines.
- **Program reads from stdin** — set `stdin` for tests that require scripted
  keyboard/input text (for example, `tkbd` expects `x`).
- **Deep recursion** — apps such as `triangle` (768) and `cobint` (1536) need a
  larger `stack_size` than the 512-byte default, especially under
  `-fstack-check`.
- **Cannot be auto-tested** — set `ignore: true` for interactive programs
  (`na`, an editor that waits for keystrokes), tests that intentionally fail to
  compile (`tc89fltb`), or deliberate stack-smashers (`spsmash`).
- **Results are inherently nondeterministic** — use `perf_ignore` for timing-
  or filesystem-sensitive apps, and `narrow_diff_ignore` only when output
  legitimately depends on stack layout.
- **Host compiler coverage is conditional** — use the host requirement fields
  for ABI, compiler, or libc differences; `host-cflags` handles per-test C mode
  or optimizer requirements.

To change a test's run behavior, edit `tests/_test_overrides.json` and re-run
the suite. See also `tests/README.md` in the repository for how tests,
baselines, and this file relate.

## Performance Reporting (`runall.ps1 -Report`)

`runall.ps1 -Report` appends historical performance data during the normal
verified suite; it does not add a separate benchmark pass. Every `ntvcm` run
already uses `-p -s:0`, so execution remains unthrottled while the runner records
the emulator's host-independent Z80 cycle count and the `.COM` size.

Report mode implies `-NoStackCheck`. The `peep_ms` and `nopeep_ms` values are
derived as `cycles / ReportClockHz * 1000`, using a default nominal clock of
400 MHz; `ReportClockHz` never changes emulator speed. Set it to `0` to leave
the derived millisecond fields empty while retaining cycles and sizes.

```pwsh
./scripts/runall.ps1 -Report
./scripts/runall.ps1 -Report -ReportFile results.csv
./scripts/runall.ps1 -Report -ReportClockHz 0
```

Results are written to `perf_results.csv` by default. **Results append to the
file**, so each report run adds a new row per app:

```csv
machine,os,utc-timestamp,app,peep_ms,peep_cycles,peep_size,nopeep_ms,nopeep_cycles,nopeep_size,clock_hz
z80-lab,macOS,2026-08-18T12:00:00Z,sieve,0.75,300000,2176,,,,400000000
```

**Columns:**

- `machine` — Name of the machine running the benchmark
- `os` — Host operating-system name
- `utc-timestamp` — UTC timestamp (ISO 8601 format, e.g., `2026-06-16T07:18:39Z`)
- `app` — Application name
- `peep_ms` — Clock-normalized milliseconds for the optimized build
- `peep_cycles` — Z80 cycles reported for the optimized build
- `peep_size` — Binary size in bytes (optimized)
- `nopeep_ms` — Clock-normalized milliseconds for the build without dccpeep
- `nopeep_cycles` — Z80 cycles reported for the build without dccpeep
- `nopeep_size` — Binary size in bytes (without dccpeep)
- `clock_hz` — Nominal `ReportClockHz` used for the millisecond calculation

The `-ReportFile` parameter controls the output path. The `-Mode` parameter
controls which CSV columns are populated: `-Mode full` fills both `peep_*` and
`nopeep_*`; single-mode runs fill only the selected mode's columns. In the CSV,
`peep_*` columns hold optimized-build measurements.

### Checked performance baselines

Normal stack-checked runs compare each built mode's cycle count and `.COM` size
with `tests/perf_baselines.csv`. This check is on by default and uses the same
execution as output verification. `-NoPerfCheck`, `-NoStackCheck`, and `-Report`
skip it. After an intentional, verified change, run with
`-UpdatePerfBaseline`; only the mode columns built by that invocation are
rewritten.

## Stack Size Measurement (`stacksize.sh` / `stacksize.bat`)

Finds the minimum C stack reserve an app needs under DCC C Compiler's lightweight
stack-overflow guard (`-fstack-check`). See the
[Building and linking](../02-build-and-link.md#measuring-the-stack-an-app-needs)
section for full documentation, or run `scripts/stacksize.sh --help`.
