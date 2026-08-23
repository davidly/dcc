# Debugger host and I/O adapters

`dcc-debug-host` is DCC's full-system GDB/MI backend. It boots a real 63K
CP/M 2.2 image and executes CCP, BDOS, BIOS, disk-controller, and Z80 code.
Unlike a BDOS shim, it preserves the disk and console behavior seen by a
program on the target runtime.

The source project is under `src/dcc_debug_host`. The normal cross-platform
DCC build includes the host and example I/O adapter:

```powershell
pwsh ./scripts/build-dcc.ps1
```

The script explicitly builds, verifies, and publishes both targets in the
repository root as:

- `dcc-debug-host` on macOS/Linux or `dcc-debug-host.exe` on Windows; and
- `libdcc-debug-io-adapter-example.dylib` on macOS;
- `libdcc-debug-io-adapter-example.so` on Linux; or
- `dcc-debug-io-adapter-example.dll` on Windows.

DCC's normal CI runs this same build on Windows, Linux, and macOS. The release
matrix additionally covers Windows ARM64 and Linux ARM64.

It can also be built and tested directly:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host_tests \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dcc_debug_host_tests --parallel
ctest --test-dir build/dcc_debug_host_tests --output-on-failure
```

The direct CMake build keeps its executable in the selected CMake build tree;
the normal cross-platform build publishes it to the repository root.

## Generic host boundary

The debugger owns CPU execution, CP/M disks, GDB/MI, target-terminal transport,
and input pacing. It does not compile in an application-specific I/O port map.
Without an adapter:

- unmapped input ports return zero;
- unmapped output ports are ignored; and
- terminal bytes pass through unchanged.

An optional shared library supplies machine-specific behavior through the
versioned C ABI in `src/dcc_debug_host/include/dcc_debug_io_adapter.h`.
Load one with:

```sh
./dcc-debug-host --interpreter=mi \
  --io-adapter /path/to/libdcc-debug-io-adapter.so \
  --env-file /path/to/debugger.env
```

Use `.dylib` on macOS and `.dll` on Windows. A missing library, missing init
symbol, ABI mismatch, or failed initialization is reported before CP/M starts.
Only load adapters from trusted sources: they execute native code inside the
debugger process.

## Adapter lifecycle

An adapter exports one function:

```c
int dcc_debug_io_adapter_init(
    const dcc_debug_io_adapter_config_t *config,
    dcc_debug_io_adapter_t *adapter,
    char *error,
    size_t error_size);
```

Initialization receives:

- the ABI version and structure sizes;
- an optional environment-file path;
- the native session-files root; and
- host interrupt registration, raise, and clear services.

The adapter returns its context plus required port input, port output, and
`close` callbacks. The host calls `close` before unloading the shared library,
so it must stop worker threads and release resources there.

Adapters that use interrupts register providers through the supplied host
services. Optional poll functions execute on the emulator thread. The adapter
does not link against debugger internals.

## ABI v2 terminal pipeline

ABI v2 adds two optional callbacks:

```c
size_t (*terminal_input)(
    void *context,
    const uint8_t *input,
    size_t input_size,
    uint8_t *output,
    size_t output_size,
    uint64_t now_ms);

size_t (*terminal_poll)(
    void *context,
    uint8_t *output,
    size_t output_size,
    uint64_t now_ms);
```

`terminal_input` receives bytes from the target terminal before they enter the
CP/M console queue. An adapter can pass them through, suppress them, buffer a
partial escape sequence, or emit translated bytes. Input sequences may be
split across callback invocations, so parsing state belongs in the adapter
context.

`terminal_poll` lets an adapter release timeout-dependent buffered input while
no new terminal byte is arriving. A typical use is distinguishing a standalone
Escape key from the start of an ANSI cursor-key sequence.

Both callbacks:

- receive a monotonic millisecond timestamp;
- may return zero output bytes;
- must not write beyond `output_size`; and
- must never return a count greater than `output_size`.

When either callback is `NULL`, the host uses its generic fallback:
`terminal_input` passes bytes through and `terminal_poll` emits nothing. The
Windows terminal bridge converts native extended-key events into standard ANSI
sequences, but target-specific translation remains adapter policy.

## Buildable adapter example

`src/dcc_debug_host/examples/io_adapter` contains a complete shared-library
example. It includes:

- ABI and structure-size validation;
- required no-op port callbacks;
- a `close` callback;
- ordinary-byte pass-through;
- ANSI cursor parsing across split callback invocations;
- sample cursor-to-CP/M-control-key translation; and
- a 30 ms standalone-Escape timeout implemented with `terminal_poll`.

Build and test only the example:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host_tests \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dcc_debug_host_tests \
  --target dcc-debug-io-adapter-example-test
ctest --test-dir build/dcc_debug_host_tests \
  -R dcc-debug-io-adapter-example --output-on-failure
```

This standalone test build keeps the library under
`build/dcc_debug_host_tests/examples/io_adapter/`; the normal cross-platform
build publishes it to the repository root. The sample cursor control bytes are
illustrative policy, not part of the ABI; replace them with the bytes your
target expects.

Set `DCC_DEBUG_HOST_BUILD_EXAMPLES=OFF` when configuring CMake to omit example
targets.

## Target terminal

The separate terminal bridge keeps GDB/MI on debugger stdin/stdout while CP/M
uses an authenticated loopback socket. Start it before the host:

```sh
python3 src/dcc_debug_host/dcc_host_terminal_bridge.py \
  --endpoint-file build/dcc_debug_host/terminal.endpoint

./dcc-debug-host --interpreter=mi \
  --terminal-endpoint-file build/dcc_debug_host/terminal.endpoint
```

Ctrl+C is delivered to CP/M. Ctrl+] detaches the target terminal without
terminating the debugger. Use VS Code's Pause command to interrupt debugger
execution itself.

## Further source documentation

The source tree's `src/dcc_debug_host/README.md` covers disk staging, CP/M boot
detection, debugger features, fixtures, direct commands, and the complete test
matrix. The example directory also contains a focused README with platform
library names and launch commands.
