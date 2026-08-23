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
pwsh ./scripts/build-dcc.ps1
```

The shared library and debugger host are published in the repository root.
Load the example with:

```sh
./dcc-debug-host --interpreter=mi \
  --io-adapter ./libdcc-debug-io-adapter-example.dylib
```

Use `.so` on Linux and `dcc-debug-io-adapter-example.dll` on Windows.
Set `DCC_DEBUG_HOST_BUILD_EXAMPLES=OFF` to omit example targets.
