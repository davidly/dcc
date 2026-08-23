# DCC source debugging and fixtures

## Backend and artifacts

`dcc-debug-host` under `src/dcc_debug_host` is the sole source-debugging
backend. It boots a real CP/M 2.2 system and presents GDB/MI to VS Code.

Build the host and example adapter with the normal toolchain build:

```sh
pwsh ./scripts/build-dcc.ps1
```

Or build and test it directly:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host_tests \
   -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dcc_debug_host_tests --parallel
ctest --test-dir build/dcc_debug_host_tests --output-on-failure
```

Build the target with debug metadata:

```sh
dccmake -g app.c dcc-output=APP
```

The linked `APP.COM` and `APP.DBG` must have the same basename, be adjacent,
and come from the same build. `dcc-debug-host` derives the sidecar path from the
program path. Copy or rename both files together when a shared active-file
launch expects a different basename.

The host path is normally:

- macOS/Linux: `build/dcc_debug_host/dcc-debug-host`
- Windows: `build/dcc_debug_host/Release/dcc-debug-host.exe`

Launch it as the `cppdbg` `miDebuggerPath`, pass `--interpreter=mi`, and use
`launchCompleteCommand: "exec-continue"`.

## Terminal input

GDB/MI owns debugger stdin/stdout. For a dedicated interactive CP/M terminal,
start `src/dcc_debug_host/dcc_host_terminal_bridge.py` with an endpoint file and
pass that file to the host with `--terminal-endpoint-file`.

Without a connected target terminal, a blocking BDOS read or repeated BDOS 6
poll causes an input stop. Queue line input from the Debug Console with:

```text
-exec input TEXT
```

The host appends CP/M Return and resumes. Use `-exec input` with no text for a
blank line. Control keys and ANSI sequence policy belong to an optional ABI v2
I/O adapter; without adapter callbacks terminal bytes pass through unchanged.

## Fixture copy model

At launch the host builds a synthetic, in-memory B: drive containing the
selected `.COM` and fixtures. A:, C:, and D: are disposable session copies;
ordinary writes are deleted at debugger exit.

Use one or more of these input mechanisms:

1. **Automatic directory:** create `fixtures/` beside the selected `.COM`.
   Every non-hidden regular file is copied as binary data. Subdirectories are
   ignored.
2. **Explicit binary:** pass `--fixture FILE`.
3. **Explicit text:** pass `--text-fixture FILE`; LF becomes CRLF and Ctrl-Z is
   appended for CP/M text conventions.

All fixture basenames must be unique valid CP/M 8.3 names. The automatic
directory belongs beside the actual program path supplied to the debugger. For
example, `build/DCCDEBUG.COM` discovers `build/fixtures/`, not
`tests/fixtures/` unless the launch task copies files there first.

The checked-in DCC task stages source-adjacent `*.WTS`, `*.IN`, and `*.DAT`
files into `build/fixtures/`. Adapt the patterns for other projects rather than
copying unrelated files indiscriminately.

## Saving generated fixtures

Pass `--save-fixtures DIR` only for a program whose purpose is to generate or
update fixture files. The destination is published transactionally:

1. the target must return normally to CP/M;
2. the final synthetic B: is extracted to a temporary native directory;
3. the launched `.COM` is excluded; and
4. the temporary directory atomically replaces `DIR`.

If staging fails, the CPU halts, execution is interrupted, or the debugger is
quit, an existing destination directory remains untouched. CP/M stores file
lengths in 128-byte records, so extracted files include final-record padding.

Do not expect ordinary debugger writes to persist without `--save-fixtures`.
Do not save into a source fixture directory during exploratory debugging unless
the program is a deliberate fixture generator and replacement is intended.

## Regression-run fixtures are separate

`tests/_test_overrides.json` controls normal regression execution under the
configured CP/M emulator: arguments, stdin, source fixtures, stack settings,
ignored programs, and performance exclusions. It does not configure the
debugger host's synthetic B: drive. Honor it for runall/direct emulator tests;
use the mechanisms above for a GDB/MI debugging session.

## Debugger implementation and adapter development

- Host source: `src/dcc_debug_host/`
- ABI header: `src/dcc_debug_host/include/dcc_debug_io_adapter.h`
- Buildable adapter example: `src/dcc_debug_host/examples/io_adapter/`
- MkDocs guide: `docs/docs/en/00-debug-host.md`
- Source README: `src/dcc_debug_host/README.md`

The example demonstrates chunked `terminal_input`, timeout-based
`terminal_poll`, required port callbacks, ABI validation, and orderly close.
