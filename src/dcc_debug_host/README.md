# DCC Debug Host

`dcc-debug-host` is a GDB/MI target for DCC programs. Unlike a BDOS shim, it
boots a real 63K CP/M 2.2 system and executes the CCP, BDOS, BIOS, 88-DCDD
controller, and Z80 core used by the target runtime.
VS Code's `cppdbg` adapter can therefore debug a program under the same CP/M
disk and console behavior it sees on the target system.

The project owns its Z80 engine, memory and disk hardware, support
headers, and CP/M disk images. I/O port assignments are intentionally absent:
an optional dynamic library supplies them through the versioned
`include/dcc_debug_io_adapter.h` C ABI. Compatible adapters implement that
contract independently. See
[`Z80_ENGINE.md`](Z80_ENGINE.md) for provenance, isolation, and the explicit
absence of Intel 8080 mode support.

The default disk assets are under `assets/`. Without `--io-adapter`, unmapped
input ports return zero and output ports are ignored. `--env-file` is passed
unchanged to a loaded adapter; the generic host does not read it.

The default CP/M drive layout is:

- A: the CP/M 2.2 boot disk, plus the temporary debugger boot marker;
- B: a synthetic in-memory drive generated from A:'s physical format and
  containing only the selected `.COM` and fixtures;
- C: a disposable copy of `assets/disks/disk_c_blank.dsk`; and
- D: a disposable copy of `assets/disks/disk_d_blank.dsk`.

C: and D: start as separate freshly formatted blank 88-DCDD images on every
debug session. Only A: contains persistent CP/M files.

## In-memory B: drive

The host makes the selected `.COM` file and optional fixtures visible on B:
without changing a repository disk or running `FT` inside CP/M. At launch it:

1. reads the files directly from the native filesystem;
2. builds CP/M directory entries, extents, and allocation blocks directly in a
  337,568-byte memory image;
3. wraps each logical sector in the Burcon/MITS 137-byte physical format;
4. mounts that buffer through the portable memory-backed disk controller; and
5. asks the real CCP to load and run the target.

The wrapper implements both Burcon sector layouts: tracks 0-5 carry data at
offset 3 and tracks 6-76 at offset 7. It also applies the CP/M translation table,
the 17-sector skew on later tracks, and each format's checksum/trailer.

## CP/M startup detection

The debugger does not infer a successful boot from banner text or an `A>`
prompt. It uses the Burcon cold-boot autorun records:

1. a temporary A: is generated with a small `DBGBOOT.COM` marker utility;
2. track 0 sector 4 is patched with the cold-boot command `A:DBGBOOT`;
3. bit 0 at byte 89 of track 1 sector 26 enables cold-boot autorun;
4. both modified physical-sector checksums are recomputed; and
5. CP/M is considered started only after `DBGBOOT` prints its startup GUID.

`DBGBOOT` then jumps through the normal warm-boot path. The warm-boot autorun
bit is clear, so CP/M returns to a clean `A>` prompt before debugger setup
continues.

The B: drive exists only in process memory and is released when the debugger
exits. The generated A: drive and disposable copies of C: and D: are deleted at
the same time. CP/M writes are session-local and are not copied back to native
fixture files.

## Build and test

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host
cmake --build build/dcc_debug_host
ctest --test-dir build/dcc_debug_host --output-on-failure
```

The normal DCC build also builds `dcc-debug-host` and the example I/O adapter:

```sh
pwsh ./scripts/build-dcc.ps1
```

The adapter is written to `build/dcc_debug_host/examples/io_adapter/` as
`libdcc-debug-io-adapter-example.dylib` on macOS,
`libdcc-debug-io-adapter-example.so` on Linux, or under that directory's
`Release/` subdirectory as `dcc-debug-io-adapter-example.dll` on Windows.
The build script treats both the host and example adapter as required outputs.
Normal CI runs that script on macOS, Linux, and Windows; release builds also
cover Windows ARM64 and Linux ARM64.

The host compiles against its public adapter ABI header but does not link any
adapter implementation. CMake rejects source paths or parent-directory
includes that escape the debugger source tree and verifies that all default
disk assets are present.

When `DCC_DIR` is set, or CMake is configured with `-DDCC_ROOT=/path/to/dcc`,
CTest registers seven layers of coverage:

- `debug-metadata`: every current `DCCDBG 2` record, scope lifetime, shadowing,
  globals, structures, fields, arrays, VLAs, function pointers, and bitfields;
- `debug-evaluator`: types, formatting, pointer/aggregate traversal, integer
  promotions/operators, C character escapes, `sizeof`, address/dereference,
  multidimensional addressing, 16-bit range checks, children, and writes;
- `directory-disk`: exact and overflowing extent, allocation-block, directory,
  empty-file, duplicate-name, and full-disk boundaries;
- `debugger-z80-core`: debugger-owned Z80 interrupts, block instructions,
  indexed operations, undocumented flags/registers, and alternate registers;
- `dcc-debug-host-mi`: real CP/M boot, multi-extent disk loading, breakpoints,
  stepping, pause, terminal attach/detach, control input, both CP/M exit paths,
  strict memory operations, registers, disassembly, and variable objects;
- `dcc-debug-host-dcc`: real DCC builds covering recursion, selected frames,
  step-out, shadowed locals, structures/unions/bitfields, function pointers,
  fixed and variable multidimensional arrays, globals, command arguments,
  scalar types, and multi-module relocation;
- `dcc-debug-io-adapter-example`: ABI v2 initialization, chunked ANSI input,
  translated output, and timeout polling.

The MI fixture deliberately places executable code across the track-6 disk
format boundary. It also validates OpenDebugAD7 separately when an adapter path
is supplied.

An ASan/UBSan run can be configured independently:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/dcc_debug_host-sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/dcc_debug_host-sanitize --output-on-failure
```

To include the installed VS Code C++ adapter in the test:

```sh
python3 src/dcc_debug_host/tests/dcc_host_mi_test.py \
  build/dcc_debug_host/dcc-debug-host \
  "$HOME/.vscode/extensions/ms-vscode.cpptools-*/debugAdapters/bin/OpenDebugAD7"
```

Expand the adapter glob to its installed version before running the command.

## VS Code

Open a DCC C source file and select **DCC CP/M: Debug active C file**. The
pre-launch task:

1. runs `dccmake -g`, using the app's `dccmake.txt` when present;
2. places matching `.COM` and `.DBG` files beside the active source; and
3. builds `dcc-debug-host`; and
4. opens a dedicated **DCC full CP/M debug terminal** in VS Code.

Set `DCC_DIR` to the DCC repository before launching. Source breakpoints,
source stepping, stack frames, registers, memory, and disassembly use the
sidecar `DCCDBG 2` metadata and the emulator's instruction-boundary API.

## Target terminal

The default full-system launch routes CP/M stdin/stdout through the dedicated
VS Code integrated terminal. Type target input there normally; no debugger
command prefix is required. ANSI/VT100 output and control characters pass as
raw terminal bytes.

- **Ctrl+C** is delivered to CP/M as byte `0x03`.
- **Ctrl+]** detaches the target terminal. If this happens, the debugger keeps
  running and its emergency MI fallback remains available.
- Use VS Code's **Pause** command to interrupt the debugger itself.

Terminal input is offered to the optional I/O adapter through the ABI v2
`terminal_input` and `terminal_poll` callbacks. This lets a machine-specific
adapter buffer escape sequences, translate keys, and release timeout-dependent
input without coupling that policy to the debugger. When no adapter callback
is installed, terminal bytes pass through unchanged. The Windows bridge only
normalizes native extended-key events into standard ANSI sequences.

[`examples/io_adapter`](examples/io_adapter/README.md) contains a buildable
shared-library example with chunked ANSI arrow parsing and a delayed standalone
Escape implementation.

MI still owns the debugger process's stdin/stdout. The terminal task therefore
opens an authenticated, loopback-only TCP listener and publishes its ephemeral
port in `build/dcc_debug_host/terminal.endpoint`. The debugger connects to it
at launch. The endpoint file is owner-readable and is removed when the bridge
exits. Target output is sent to one place: the integrated terminal while it is
connected. The MI target-output/input path is retained for automated tests and
unexpected terminal-disconnect recovery, but is not exposed as a separate VS
Code launch profile. Input from either internal path is paced through the real
CP/M console path so BDOS flow-control polling does not consume later
characters.

## Debug support

The host consumes the complete current `DCCDBG 2` model:

- source lines and assembly/source function names;
- arguments, locals, nested lifetimes, shadowed names, and globals;
- signed/unsigned scalar types, `_Bool`, 32-bit `long` and `float`;
- pointers and function pointers;
- one- and multidimensional arrays and automatic VLAs;
- structures, unions, fields, and signed/unsigned bitfields.
- optimized frame/register/constant location ranges and explicit optimized-out
  values, including editable top-frame register scalars.

VS Code receives locals, arguments, expandable arrays/pointers/aggregates,
editable scalar values, watch updates, and out-of-scope status through GDB/MI
variable objects. Side-effect-free watch expressions support integer literals
and character constants, casts, `sizeof`, unary/arithmetic/shift/comparison/
bitwise/logical/conditional/comma operators, address-of, dereference, array
subscripts, and `.` / `->` field access. Expressions that would invoke target
code or mutate state are rejected; assignments are handled explicitly by the
debugger's variable-assignment command.

Execution support includes source and instruction step/next, finish, until,
conditional and ignored breakpoints, temporary/disabled breakpoints, and async
pause. On macOS, OpenDebugAD7 delivers pause as `SIGTRAP`; the host converts it
to an instruction-boundary stop without exposing the signal to CP/M.

## Direct use

```sh
./build/dcc_debug_host/dcc-debug-host --interpreter=mi
```

To load an I/O adapter:

```sh
./build/dcc_debug_host/dcc-debug-host --interpreter=mi \
  --io-adapter /path/to/libdcc-debug-io-adapter.dylib \
  --env-file /path/to/debugger.env
```

Use `.so` instead of `.dylib` on Linux and
`dcc-debug-io-adapter.dll` on Windows. A missing library, missing init symbol,
ABI mismatch, or initialization failure is reported as a startup error.

To attach a separate terminal outside the VS Code task, start the bridge first
and pass its endpoint file to the host:

```sh
python3 src/dcc_debug_host/dcc_host_terminal_bridge.py \
  --endpoint-file build/dcc_debug_host/terminal.endpoint

./build/dcc_debug_host/dcc-debug-host --interpreter=mi \
  --terminal-endpoint-file build/dcc_debug_host/terminal.endpoint
```

Optional native files can be added to the in-memory B: drive:

```text
--fixture FILE        copy a binary file unchanged
--text-fixture FILE   convert LF to CP/M CRLF and append Ctrl-Z
--save-fixtures DIR   replace DIR with B: files after normal target exit
```

The normal project convention requires no launch arguments: place binary files
in a `fixtures/` directory beside the program's `.COM` file. Every non-hidden
regular file in that directory is copied into synthetic B: before CP/M boots.
Fixture names must be unique CP/M 8.3 names; subdirectories are ignored.

Use `--save-fixtures` only for generator programs. The host stages the final
contents of synthetic B:, excludes the launched `.COM`, and replaces the
destination only after the target returns normally to CP/M. Interrupting or
quitting the debugger, halting the CPU, or failing to stage the files leaves an
existing destination unchanged. CP/M stores lengths in 128-byte records, so
saved files include their final record padding.

All file names must fit CP/M 8.3 syntax. `--drive-a`, `--drive-c`, and
`--drive-d` override the default project images. `--io-adapter` selects an
optional port library and `--env-file` supplies its environment-file path.
B: has no image option because it is always synthetic and memory-backed.

## Current limitations

The native-backed drive is generated in memory once at launch rather than being
a live host-directory filesystem. CP/M writes remain disposable unless an
explicit `--save-fixtures` generator run exits normally. There is no post-boot
CPU snapshot; each debugging session performs an authentic cold boot.
