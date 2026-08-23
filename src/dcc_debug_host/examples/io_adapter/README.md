# I/O Adapter Example

This directory contains a minimal ABI v2 shared library for `dcc-debug-host`.
It demonstrates a terminal input pipeline without assigning any emulated I/O
ports.

The example:

- passes ordinary terminal bytes through unchanged;
- buffers ANSI escape sequences across callback invocations;
- translates arrow keys to sample CP/M control bytes;
- uses `terminal_poll` to emit a standalone Escape after 30 ms; and
- provides the required port and close callbacks.

The arrow mapping is example policy, not part of the debugger ABI. Replace the
values in `process_terminal_byte()` with the bytes expected by your target.
Callbacks must never return more bytes than `output_size`.

From the DCC repository root:

```sh
cmake -S src/dcc_debug_host -B build/dcc_debug_host
cmake --build build/dcc_debug_host --target dcc-debug-io-adapter-example
ctest --test-dir build/dcc_debug_host -R dcc-debug-io-adapter-example \
  --output-on-failure
```

The shared library is generated under
`build/dcc_debug_host/examples/io_adapter/`. Load it with:

```sh
./build/dcc_debug_host/dcc-debug-host --interpreter=mi \
  --io-adapter ./build/dcc_debug_host/examples/io_adapter/libdcc-debug-io-adapter-example.dylib
```

Use `.so` on Linux and `dcc-debug-io-adapter-example.dll` on Windows.
Set `DCC_DEBUG_HOST_BUILD_EXAMPLES=OFF` to omit example targets.
