# Worked examples

Short, self-contained programs suitable for adding to a project, building with
`dccmake`, and running under an emulator such as ntvcm. For example, from the
DCC checkout:

```sh
dccmake tests/texsort.c dcc-output=TEXSORT
ntvcm build/TEXSORT.COM
```

See
[Building and linking](02-build-and-link.md) for build options and the manual
pipeline.

## Sorting and searching an `int` array

`qsort` orders the array, then `bsearch` locates a key with the *same*
comparator. The comparator returns negative / zero / positive — here the
branchless `(x > y) - (x < y)` idiom.

```c
--8<-- "tests/texsort.c:example"
```

Output: `found 13 at index 5`.

## Sorting an array of structs by a key field

Any element width works because `qsort` swaps whole elements byte-by-byte. The
comparator reads the field it sorts on — here a string member via `strcmp` — and
`bsearch` reuses it to look a record up by name.

```c
--8<-- "tests/texstrct.c:example"
```

Output:

```text
apples   9
kiwis    2
pears    4
kiwis: 2 in stock
```

## A `printf`-style logging wrapper

Forwarding a `va_list` to `vfprintf` supports custom diagnostic wrappers without
re-parsing the arguments.

```c
--8<-- "tests/texlog.c:example"
```

## Reading a text file line by line

```c
--8<-- "tests/texfile.c:example"
```

## Parsing input with `sscanf`

`sscanf` reads from a string using the same conversion subset as `scanf` and
`fscanf` (integers and strings; no floating input). Each conversion stores
through a pointer argument.

```c
--8<-- "tests/texscan.c:example"
```

Output:

```text
value=-12 word=hello hexval=42
big=123456
```

## Buffered console output with a user-declared buffer

`setvbuf` allows supplying a user-allocated buffer for console output, so output
accumulates instead of going to CP/M one character at a time.
A larger buffer means fewer BDOS calls. Drain it with `fflush`, and detach it
(`setvbuf(stdout, NULL, _IOLBF, 0)`) before the buffer's storage is reused — see
[Console output buffering](standard-lib/05-stdio.md#console-output-buffering).

```c
--8<-- "tests/tbufex.c:example"
```

Output ends with `sum of squares 1..20 = 2870`. The `static` buffer keeps it off
the small CP/M stack; a `malloc`'d buffer works too, but free it only *after*
detaching it from the stream. This snippet is pulled verbatim from the
`tests/tbufex.c` regression test, so the documented code is exactly what is
built and run by the suite.

## Waiting for an I/O-adapter timer

The example debugger I/O adapter provides a 16-bit millisecond timer on ports
24 and 25. Write the delay's high byte to port 24, then its low byte to port 25
to start the timer. Reading either port returns 1 while it is running and 0
after it expires.

```c
--8<-- "src/dcc_debug_host/examples/io_adapter/timer.c:example"
```

This program requires `dcc-debug-host` with the example I/O adapter loaded;
ordinary emulators need an equivalent device on those ports. The source lives
beside the adapter and builds with:

```sh
./dccmake src/dcc_debug_host/examples/io_adapter/timer.c dcc-output=TIMER
```

See [Direct port I/O](10-system-and-cpm.md#direct-port-io) for the `inp` and
`outp` runtime contract.

## Handling periodic I/O-adapter interrupts

The example adapter also provides a periodic maskable-interrupt source on port
52. Writing a value from 1 through 255 selects that many interrupts per second;
writing zero disables the source and clears pending requests. Reading the port
returns the configured rate.

This example installs a Z80 interrupt mode 1 vector at `0038H`, waits with
`HALT`, counts 50 interrupts at 10 Hz in a C function, then disables the source
and restores CP/M's original vector:

```c
--8<-- "src/dcc_debug_host/examples/io_adapter/dccint.c:example"
```

The assembly wrapper preserves both Z80 register sets before calling C. The C
handler only updates memory: interrupt code must not call BDOS, perform console
I/O, allocate memory, or use other non-reentrant runtime services.

Build the source with debug metadata using:

```sh
./dccmake -g src/dcc_debug_host/examples/io_adapter/dccint.c \
  dcc-output=DCCINT
```

Run `DCCINT.COM` under `dcc-debug-host` with the example I/O adapter loaded.
It counts for five seconds, prints `C handler count: 50`, restores the vector, and
returns normally to CP/M.
