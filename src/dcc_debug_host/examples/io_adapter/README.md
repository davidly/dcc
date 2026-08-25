# I/O Adapter Example

This directory contains a portable shared library for `dcc-debug-host`. It
demonstrates emulated timer ports and a terminal input pipeline.

The example:

- provides three 16-bit millisecond timers on ports 24/25, 26/27, and 28/29;
- provides a one-byte seconds timer on port 30;
- provides a periodic maskable-interrupt timer on port 52;
- returns 1 while a timer is running and 0 when it expires or is not set;
- passes ordinary terminal bytes through unchanged;
- buffers ANSI escape sequences across callback invocations;
- translates arrow keys to sample CP/M control bytes;
- uses `terminal_poll` to emit a standalone Escape after 30 ms; and
- provides the required close callback.

For each millisecond timer, write the delay's high byte to the even port, then
write its low byte to the odd port to start it. Write a delay in seconds directly
to port 30. The implementation uses only C11 time facilities so the same source
builds on Windows, Linux, and macOS. Unmapped port reads return zero and writes
are ignored.

The adjacent `timer.c` is a CP/M client for timer 0. Build it from the repository
root with:

```sh
./dccmake src/dcc_debug_host/examples/io_adapter/timer.c dcc-output=TIMER
```

The adjacent `dccint.c`, adapted from the Altair DCCINT application, installs a
Z80 interrupt mode 1 wrapper and counts 50 interrupts from port 52. Build it
with:

```sh
./dccmake -g src/dcc_debug_host/examples/io_adapter/dccint.c \
  dcc-output=DCCINT
```

Write a rate from 1 through 255 to port 52 to start the interrupt timer. Write
zero to disable it and clear pending requests; reading port 52 returns the
configured rate.

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
