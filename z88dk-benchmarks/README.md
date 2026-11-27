# z88dk CP/M benchmark reproduction

This folder contains the 16 benchmark applications used for the **sccz80** and
**zsdcc** rows in [`images/dcc.xlsx`](../images/dcc.xlsx) and the README's table.
It includes the source snapshots, compatibility headers, input files, expected
outputs, and build/run scripts. You can copy this folder to another machine;
building these programs does not require dcc.

## Requirements

- **Python 3.7 or later**, using only its standard library. See the main
  [Python setup instructions](../README.md) if needed.
- [z88dk](https://z88dk.org/), including both compiler backends and its CP/M libraries.
- [ntvcm](https://github.com/davidly/ntvcm), for running CP/M executables and counting
  simulated Z80 cycles. Use its Z80 mode, which is the default.

Put `zcc` and `ntvcm` on PATH, or specify their locations below. The recorded Windows
installation used sccz80 `25065-40a990bd2d-20260709` and zsdcc `4.5.0 #15242`
(MINGW64, August 1, 2025). `reference-toolchain.json` records executable hashes and
source/input hashes; `reference-results.csv` records the measurements. Git attributes
preserve snapshot and fixture bytes, including their original line endings. Different
compiler or library revisions can produce different results.

## Run the table benchmarks

From this folder on Windows:

```bat
python bench.py run --compiler both --z88dk C:\z88dk --ntvcm C:\ntbin\ntvcm.exe --jobs 4 --allow-known-failures
```

On Linux or macOS, install native z88dk and ntvcm, put their executables on PATH,
and run:

```sh
python3 bench.py run --compiler both --jobs 4 --allow-known-failures
```

The default is one job and zsdcc only. The 200,000 allocation-attempt budget can
make large programs take many minutes to compile. Use `--jobs` to run independent
builds concurrently; it does not change generated code or simulated timings.
`--build-timeout` defaults to 1,800 seconds per app, and `--run-timeout` to 300 host
seconds. Both can be increased on slower machines.

For a shorter run or a single app:

```sh
python3 bench.py run --compiler both --apps sieve,e,tm,ttt
python3 bench.py run pihex --compiler zsdcc
```

Build without running:

```sh
python3 bench.py build sieve --compiler zsdcc
python3 bench.py build pihex --compiler sccz80
```

Windows wrappers preserve the familiar commands: `m sieve`, `m sieve old`,
`mf pihex`, and `mf pihex old`. `old` selects sccz80. Both build wrappers choose
the appropriate integer or floating-point profile from the app name. `p.bat`
and `runall.bat` run `bench.py run` with any supplied options. They propagate
failure exit codes.

`--z88dk` (or the `Z88DK` environment variable) selects an installation root.
`--zcc` / `ZCC_TOOL` and `--ntvcm` / `NTVCM_TOOL` select explicit executables.
Windows also checks `C:\z88dk\bin\zcc.exe` and `C:\ntbin\ntvcm.exe`.

## Outputs and measurement conventions

Each app gets an isolated `build/<compiler>/<app>/` directory containing its COM
file, input fixtures, assembly listings/map, `build.log`, `run.stdout`, and
`run.stderr`. Each invocation writes `build/results.csv` and `build/results.json`.
Use `--build-dir` to preserve separate runs. Generated files are ignored by Git.

- **Time:** Z80 cycles divided by **4,000,000**, giving simulated seconds at 4 MHz.
  Compilation and host/emulator wall time are excluded.
- **TTT:** run with `10`, then divide the total time by ten to report **one pass**.
  Its cycle column retains the full ten-pass count; `passes` records the divisor.
- **Size:** report the raw COM length and `ceil(bytes / 128) * 128`, matching CP/M
  file records and the spreadsheet. For example, 5,761 bytes rounds to 5,888.
- **Compiler flags:** zsdcc uses `-SO3 --max-allocs-per-node200000`; sccz80 uses
  `-O2`. Integer and floating-point library profiles preserve the original
  `m.bat` / `mf.bat` options, including the printf converters in `fppragma.inc`.
- **Inputs:** `a1 -x -a:400 6502TEST.HEX`; the seven language interpreters run
  `TTT.ADA`, `TTT.BAS`, `TTT.C`, `TTT.COB`, `TTT.F`, `TTT.FOR`, or `TTT.PAS`.
  Other apps use their default workload. Fixtures are copied beside each COM.

The runner checks output, not just the emulator exit code. Matrix sums allow a
one-part-per-million tolerance for the different floating-point libraries.
`reference-results.csv` can be compared with a new `build/results.csv` by app and
compiler. Rows marked `known-failure` are timings of failing programs.

## Compatibility and known failures

Thirteen C sources match `../tests` apart from line endings. The other three
retain the same benchmark bodies with these target-specific additions:

- **tstring:** `benchmark_rand.h` supplies dcc's 16-bit xorshift sequence. zsdcc
  uses the same assembly instruction sequence as DCCRTL; sccz80 uses equivalent C.
  Printing and the zero-length `memchr` check match dcc's workload.
- **tm:** CRT heap initialization replaces the former 50 KB static heap and C
  replacement for `memset`. The benchmark now uses the library `memset`, the same
  counters, and the same static pointer array as dcc.
- **pihex:** CRT heap initialization, `fmodf` naming, and missing float definitions
  are adapted in `benchmark_nextafter.h`. zsdcc gets an exact adjacent IEEE
  binary32 step. The installed sccz80 math48 nextafter routines cannot link, so
  that backend retains the documented relative-epsilon approximation. Its
  termination threshold and floating-point precision therefore differ from dcc.

`fixtures/TTT.COB` is byte-identical to `../tests/TTT.COB`. It maintains the
player by toggling `PM` instead of recomputing depth parity through division.
The older division-based program remains in `../tests/TTTPAR.COB` as a separate
cobint regression scenario.

The assembly RNG was checked against 1,280 independently generated values for
each backend. The binary32 step passed 16 boundary/NaN cases, and zsdcc pihex
passed both the 128- and 1,024-digit reference checks. To repeat the latter after
building, run `ntvcm -p pihex.com 0 8` in `build/zsdcc/pihex/`.

With this toolchain, sccz80 cannot compile `a1` or the seven interpreters; the
runner reports them as `unsupported` and leaves their results empty.

**Both backends' `tbig` programs fail the past-8-MB boundary check:** the write
wraps around and corrupts record zero. The runner labels this exact failure
`known-failure`. Without `--allow-known-failures` it returns a failing exit code;
other build, run, or output failures always cause a failing exit code. `tbig`
creates an approximately 8 MB test file inside its isolated build directory.

The 6502 test fixture is the same image as `../tests/6502FUN.HEX`, supplied here
under the original local benchmark filename `6502TEST.HEX`. It is Klaus Dormann's
6502 functional test; see the [upstream test project](https://github.com/Klaus2m5/6502_65C02_functional_tests).
The remaining source and helper files follow the included [MIT license](LICENSE).

Run the reporting checks without either external tool:

```sh
python3 -m unittest discover -s . -p test_*.py
```
