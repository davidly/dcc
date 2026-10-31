# Building and linking

This page covers the path from a `.c` file to a runnable CP/M `.COM` program and
the compiler options that most affect the result. You typically do this in your
**own project directory** — DCC C Compiler and ntvcm are general-purpose tools for building
CP/M / Z80 C apps anywhere, not just inside the DCC C Compiler repo. As long as the tools
are on your `PATH` (see [Setting up the toolchain](00-setup-toolchain.md)), the
commands below work from any folder that holds your `.c` sources.

## Build with dccmake

`dccmake` is the native build driver. With the tools on `PATH` and the DCC
headers and runtime configured for your project, it builds one or more sources:

```sh
dccmake main.c dcc-output=MAIN
dccmake main.c helper.c dcc-output=MAIN dcc-stack-check=true
ntvcm build/MAIN.COM
```

The first source is the application translation unit; subsequent sources are
compiled with `-module`. Outputs go to `build/` by default. Source basenames
and the output name must be CP/M 8.3-compatible (at most eight characters before
the extension, and three after it). Host directory paths can be longer.

For repeatable builds, put settings in `dccmake.txt` in the working directory:

```ini
dcc-input=main.c,helper.c
dcc-output=MAIN
dcc-stack-bytes=1024
dcc-stack-check=true
dcc-peep=true
```

Run `dccmake` without source arguments to use that configuration. For projects
outside the checkout, also configure `dcc-include-directory` and
`dcc-runtime` as described in the [dccmake reference](appendix/03-utilities.md#build-pipeline-helper-dccmake).
That reference covers tool overrides, configuration precedence, debug modes,
and the optional emulated assembler/linker paths.

Run `dccmake -help` for the self-contained option reference and copyable
application, library-contract and composite workflows. `-h`, `--help` and `/?`
are aliases. Help succeeds even in a directory with an invalid configuration
and does not build anything. Builds return zero on success and nonzero on
configuration or stage failure, so agents can use the same commands as humans.

The native linker normally uses `/P:100`, CP/M's standard `.COM` load address.
For fixed-address, overlay, or relocating system programs,
[`l80c`](appendix/03-utilities.md#native-linker-l80c) also supports LINK-80-style
nonstandard origins and generates the required CP/M entry jump/bootstrap. Its
utility reference covers the CLI options, origin layouts, and limitations.

### Composite projects

A parent `dccmake.txt` can build an ordered collection of child projects:

```ini
# LIBDEMO/dccmake.txt
dcc-projects=lib,host
```

Run `dccmake` in the parent directory. Each listed directory must contain
its own `dccmake.txt`; paths resolve relative to the containing parent.
The library builds first and generates the typed host wrappers before the
host compiles. Each child retains its own inputs, output format, tools,
and build directory. Nested composite projects are supported, with cycle
detection and a maximum of 16 composite levels. A failed child stops the
remaining builds and makes the root invocation fail.

Parent configurations contain only `dcc-projects` and an optional
`dcc-library-linker`. Keep compilation settings in children; common
command-line overrides are forwarded to each child. `dcc-input` and
`dcc-output` cannot be overridden on a composite invocation.

```sh
dccmake dcc-library-linker=/path/to/dcc/l80c
```

The library-specific linker override applies only to LIB children, leaving
COM children on their ordinary linker. It can also be used for a standalone
library build. A composite builds outputs in their child build directories;
it does not install files or make disk updates transactional.

### Fixed-address bank-1 libraries

`dcc-output-format=lib` builds a fixed-address CP/M bank-1 library, not a
LINK-80 object archive. DCC's native `l80c` implements this format through
its `--lib` mode; emulated `L80.COM` does not.

Keep the host and library in separate directories, each with a
`dccmake.txt` and `build/` directory. For example, the library configuration
can contain:

```ini
dcc-input=LIB.C
dcc-output=SPREADS
dcc-output-format=lib
dcc-exports=auto
dcc-library-api=auto
dcc-library-host-dir=../host/generated
dcc-stack-bytes=512
dcc-runtime=${DCC_DIR}/DCCRTL.MAC
dcc-include-directory=${DCC_DIR}
l80c-tool=${DCC_DIR}/l80c
```

Run `dccmake` in that directory to produce `build/SPREADS.LIB` and
`build/SPREADS.SYM`. Configure compiler/assembler tools as for ordinary
application builds. The host's own configuration keeps the default `com`
format and uses the same native linker without `--lib`.

The library pipeline uses DCC's linked BSS/heap symbols, omits transient
startup, and leaves heap initialization to the matching bank-1 loader.
Library output requires `dcc-exports=auto`: the 1–8 exports are the public C
definitions discovered by the compiler, and each one is linked with its
canonical signature (see [Automatic typed libraries](#automatic-typed-libraries)).
Export names are at most eight characters. All application functions are
retained so externally called exports cannot be stripped as unreachable.
Runtime stripping and optional peephole optimization still apply.

Library stacks must be 256–2,048 bytes in 256-byte units. If DCC raises the
first translation unit's stack requirement, `dccmake` rejects the mismatch
instead of publishing a smaller stack in the header; increase the configured
reserve to an aligned value. Choose sufficient stack space for helper
modules and the complete library call chain as well.

The linked image, BSS and at least one heap byte must end below the reserved
stack, which ends at `C7FFh`. With the default 2 KiB stack an image without
BSS may therefore extend through `BFFEh`. Native `l80c` enforces the same
image, heap and stack limits as the resident bank-1 loader.

The linker records the heap base after the linked image/BSS, and the loader
bounds the heap below the reserved stack. Library `main`, process
exit/chaining, and re-entry into the bank-switch gateway are not supported.

The `.LIB` file (library format version 1) is a header, the export table,
then the image bytes, padded to a 128-byte record multiple. All multi-byte
fields are little-endian:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `B1LB` |
| 4 | 1 | Library format version, `1` |
| 5 | 1 | ABI major, `1` |
| 6 | 1 | ABI minor, `0` |
| 7 | 1 | Call revision, `1` |
| 8 | 2 | Origin, `0100h` |
| 10 | 2 | Image length |
| 12 | 2 | Export count, 1–8 |
| 14 | 2 | Reserved, zero |
| 16 | 2 | Stack bytes |
| 18 | 2 | Header length, 26 + 12 × exports |
| 20 | 2 | `__brk` image offset, or `FFFFh` |
| 22 | 2 | `__hlimit` image offset, or `FFFFh` |
| 24 | 2 | Absolute heap base |

Each 12-byte export entry holds a zero-padded 8-byte name, the adapter's
image-relative offset, and the image-relative offset of its NUL-terminated
canonical signature string. Every export has a signature.

#### Automatic typed libraries

Library interfaces are generated from compiler type information; there is
no export list or separate interface manifest to maintain:

```ini
# lib/dccmake.txt (run dccmake here before building the host)
dcc-input=DATA.C
dcc-output=DATA
dcc-output-format=lib
dcc-exports=auto
dcc-library-api=auto
dcc-library-host-dir=../host/generated
dcc-stack-bytes=512
dcc-runtime=${DCC_DIR}/DCCRTL.MAC
dcc-include-directory=${DCC_DIR}
l80c-tool=${DCC_DIR}/l80c
```

With `dcc-library-api=auto`, library source is the single source of truth.
DCC compiles every configured library source with its normal include paths,
defines and compiler settings, emitting resolved type metadata with `-bank-api`.
It generates `host/generated/API.H`, retaining parameter names and resolving
typedefs to primitive C types, plus the client wrappers. Do not include this
generated header from library source: generation must work without prior
outputs. No source-text C parser or Python generator is used.
Every non-static definition must have a supported signature; public helpers
must either join the API or become `static`. Unsupported definitions fail
instead of disappearing.

Alternatively, `dcc-library-api=../API.H` selects a handwritten header: the
library and host share that header, and every public definition must match a
declaration there. The header is validated, never overwritten. Exports are
still discovered from definitions and linked with generated signatures.
Assembly definitions, unresolved prototypes, public data, static helpers,
and functions owned by the separately linked runtime are not auto exports.
There must be 1–8 discovered functions, and names are uppercased for lookup
and limited to eight characters. M80 external-name collisions and reserved
generated symbol names are rejected.

The build produces:

- `lib/build/B1ADAPT.C`: bank-1 dispatch adapters and canonical signature strings.
- `host/generated/API.H`: public prototypes and buffer annotations in auto mode.
- `host/generated/B1HOST.C`: client implementations of the public C API.
- `lib/build/B1M01.API` and later: resolved source metadata, including definition
  parameter names. Handwritten-header mode also produces `B1M00.API` for the
  selected header. Metadata-pass `.MAC` files are build intermediates.
- `lib/build/DATA.LIB`: the library image with its typed export table; no
  hand-maintained manifest.

**Generate the contract before client integration**, without an assembler,
linker or runtime file:

```sh
# In the library directory, or the composite root containing lib and host:
dccmake dcc-contract-only=true
```

This still compiles the library sources to validate types and obtain metadata,
but does not assemble, link or install anything. Composite COM children are
skipped. A previously linked LIB is left untouched. Normal builds refresh the
contract automatically. Contract-only mode requires LIB output.
Failed generation invalidates generated interfaces instead of leaving stale
headers for clients. Existing handwritten headers in the generated location are
protected against accidental overwriting.

The host includes `API.H` and retains an ordinary COM configuration, adding
`generated/B1HOST.C` to `dcc-input`, both `generated` and the directory containing
the common `BANKLIB.H` to `dcc-include-directory`, and compiling BANKLIB/BIOSBANK runtime
sources. Auto-generated client wrappers include their adjacent API header;
bank adapters and handwritten-header wrappers use a build-time absolute path,
so regenerate them after moving the source tree. Missing directories beneath
`dcc-library-host-dir` are created automatically. Full automatic rebuilds also
invalidate the previous LIB before compiling.

**Transfer types and annotations.** The supported set is `void`
results, signed/unsigned 8-, 16-, and 32-bit integers, opaque `unsigned int`
handles, explicitly counted byte buffers, and annotated pointers to single
named flat records. Typedefs resolve through the
compiler: a handle typedef remains canonical `u16`, not a raw pointer.
Plain `char` is signed on this target. Pointer results, unannotated pointers,
pointer graphs, function pointers, varargs, old-style/unprototyped functions,
floats, and by-value structure/union arguments/results are rejected explicitly.
Records have a checked target-layout contract, not implicit pointer or
deep-object serialization.

For example:

```c
typedef unsigned int handle;
unsigned int onload(void);
unsigned int onunload(void);
unsigned int add16(unsigned int left, unsigned int right);
unsigned int upper(unsigned char *data, unsigned int count);
unsigned int filerd(unsigned char *data, unsigned int count);
struct BlockSpec { unsigned int seed; unsigned char label[8]; };
struct BlockInfo {
    unsigned int handle, size;
    unsigned long byte_sum;
    signed char state;
    unsigned char label[8];
};
handle blknew(const struct BlockSpec *spec, struct BlockInfo *info);
unsigned int blkread(handle block, unsigned int offset,
                     unsigned char *data, unsigned int count);
unsigned int blkfree(struct BlockInfo *info);
#pragma dcc bank_buffer upper 1 inout 2 128
#pragma dcc bank_buffer filerd 1 out 2 128
#pragma dcc bank_buffer blkread 3 out 4 128
#pragma dcc bank_record blknew 1 in
#pragma dcc bank_record blknew 2 out
#pragma dcc bank_record blkfree 1 inout
```

The maintained
[LIBDEMO example](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/LIBDEMO)
builds this data service from `lib/DATA.C` and its eight-step client/test
program from `host/DATATEST.C`. Its outputs are `DATA.LIB` and `DATATEST.COM`.
The shared BANKLIB/BIOSBANK client support lives in
[`examples/BANKSDK`](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/BANKSDK),
not inside another runnable example.
Its public C API uses descriptive `bank_library_*`, `bank_memory_*` and
`bank_bios_*` names, aliased to the short linker symbols, so they add no
runtime wrapper overhead.
The generated wrappers make ordinary C calls possible; only the deliberate
ABI-rejection checks use low-level call frames.

The pragma syntax is
`#pragma dcc bank_buffer FUNCTION BUFFER_ARG in|out|inout COUNT_ARG MAX_BYTES`.
Argument positions are **1-based**; the count must resolve to `unsigned int`
(`u16`); the maximum is a literal decimal integer from 1 to 256. Annotate
each byte pointer once (identical repeated annotations are harmless).
In auto-header mode place pragmas in library sources or headers they include;
annotations are collected across configured modules. In handwritten-header mode
they must occur in the selected API header.
`const` byte pointers support `in` only. Zero count permits NULL; nonzero
count with NULL is invalid. No pointers or pointer graphs cross banks;
buffers are independent flat snapshots, without pointer-alias preservation.
The sum of result, scalar arguments and maximum buffer sizes must fit 256
bytes. These are byte lengths, never implicit NUL-terminated strings.

For a single record use
`#pragma dcc bank_record FUNCTION RECORD_ARG in|out|inout`.
The 1-based argument must point to a complete **named struct** with
unqualified signed/unsigned 8-, 16- or 32-bit integer fields or fixed
one-dimensional arrays of those types. Pointer members, nested/anonymous
aggregates, unions, bitfields, const/volatile members, and flexible arrays
are not supported. Record pointers cannot be NULL; `const` record pointers
permit only IN. Records passed by value or returned by value remain
unsupported.

The compiler emits version-2 bank metadata with resolved tags, member
names/types, sizes, offsets and array lengths. This is obtained from the
type model, never by parsing source text. Automatic contracts use
`#pragma once` instead of macro include guards and contain
complete `struct TAG` definitions, annotations and compile-time target-layout
checks; original typedef aliases are resolved, not reproduced. Handwritten
contracts are checked against the same schemas. Conflicting definitions
across source modules are rejected, including same-size field changes.
Use matching updated `dcc` and `dccmake` executables.

VS Code's C/C++ IntelliSense uses native host widths and alignment, not
DCC's 16-bit packed target model. Generated layout assertions are skipped
only while `__INTELLISENSE__` is defined by the editor; struct definitions
and prototypes remain visible. Real compilation, including DCC and native
compiler validation, retains all assertions. Do not define this editor
macro in build settings to bypass incompatible-layout failures.

Record copies use the verified DCC packed little-endian representation;
these headers are not a portable native-host ABI. A compiler with different
widths/padding fails the generated static assertions. Wrappers copy complete
records with bounded `memcpy`; no heap allocation, per-field serialization
or runtime layout discovery is needed. A fixed-size call uses constant frame
offsets, an exact-size host array, and one adapter body-length check.
Only return/OUT regions are zeroed, rather than clearing inputs before
overwriting them. Record and byte-buffer arguments may be mixed.

OUT records start zero-filled. IN/OUT/INOUT direction and successful-dispatch
copy-back rules are the same as for byte buffers; a zero C result with a
successful transport still publishes outputs. Copied arguments are independent
snapshots; overlapping arguments involving a record are rejected if either
is writable. The callee must not retain its temporary record pointer. Large
record arrays should remain bank-owned, with bounded slices exposed separately.

**Canonical signatures and framing.** Type names are `void`, `i8`, `u8`,
`i16`, `u16`, `i32`, and `u32`. Functions use `RESULT(ARG,...)`, with no
whitespace: `u16()`, `void()`, `u16(u16)`, and `void(u16)`. Byte buffers
replace the pointer type with `DIRECTION:u8[COUNT_ARG<=MAX_BYTES]`, regardless
of signedness of the byte pointer. The example `objread` has exactly
`u16(u16,u16,out:u8[4<=128],u16)`. Full NUL-terminated ASCII strings
(maximum 255 characters excluding NUL) are linked at `b1s00`, `b1s01`, etc.;
adapters use `b1e00`, `b1e01`, etc. The linker receives both
`--export OBJREAD=_b1e05` and `--signature OBJREAD=_b1s05`, avoiding M80's
six-significant-character limit and signature-hash collisions. Export
entries are 12 bytes: name[8], image-relative adapter offset, image-relative
signature-string offset (both offsets little-endian uint16).

The packed **body** is result bytes first, then scalar arguments in
declaration order (pointer arguments consume no pointer slot), then copied
buffers and records in declaration order. Integers use the target's little-endian representation.
An OUT buffer begins zero-filled; IN copies only into bank 1; OUT/INOUT copy
back only after successful dispatch. Results and count scalars occupy their
normal 1/2/4-byte widths, without alignment padding. For `objread`, the body
is result2, handle2, offset2, count2, then `count` bytes: 8–136 bytes.
Records occupy one fixed-size region each among the copied arguments, in
declaration order, and do not consume pointer slots. `blknew` uses 29 body
bytes and `blkfree` 19. Record signatures use
`DIRECTION:rec1le:TAG[SIZE]{FIELD:TYPE@OFFSET;ARRAY:TYPE[COUNT]@OFFSET}`.
The grammar version and `le` specify packed little-endian layout. Full
field names/order, widths/signedness, array bounds, offsets and size are
included, so equal names or sizes alone never establish compatibility.
The 255-character signature and 256-byte body limits apply;
overflow is a generation error, not a truncated signature or short hash.

Host wrappers keep one four-byte static `bank_library_binding_t` and call
`bank_library_call_bound(&binding, name, signature, body, body_size)` from
`BANKLIB.H`. BANKSDK compares the full signature once per module generation,
then reuses the bound export index. Open reserves a 512-byte bank-1 frame
extent; steady-state calls do not allocate/free. Module generation and pool
epoch checks reject stale bindings/ownership before frame writes and again at
dispatch. Raw `blcall()` is also available, with full signature validation on
every call. Clients and libraries use BIOS ABI 1.0, call revision 1.
On the bank wire,
the runtime prepends a little-endian uint16 body-size prefix and passes a
pointer to that prefix to `unsigned int b1e00(unsigned char *wire)`.
The adapter checks the prefix against the minimum fixed size **before**
reading scalars, checks each count/bound and the 256-byte cap, then requires
an exact body-size match before rebasing pointers or invoking C. It returns
1 for dispatch or 0 for invalid framing. The common runtime must ensure the
allocation really contains the claimed body bytes; the prefix itself is
outside the 256-byte body limit.

On local NULL/count/bounds validation failures wrappers call
`blfail(BL_ARGUMENT)` or `blfail(BL_BOUNDS)` before any copying or transport.
On transport failure they preserve the runtime's `blerr()`, do not copy out,
and return deterministic zero (or return from a void function). Callers
**must check `blerr()`**, since a legitimate result can also be zero.
Optional `onload(void)`/`onunload(void)` lifecycle functions are ordinary
`u16()` exports; the common runtime owns their automatic invocation.

<!-- markdownlint-disable MD046 -->
??? note "The manual pipeline (click to expand)"

    For manual builds or custom build systems, the full pipeline for `foo.c` is
    shown below. Every build tool runs natively on the host; only the resulting
    `FOO.COM` needs CP/M or an emulator.

    === "Windows"

        Compile and optionally optimize:

        ```powershell
        dcc -I C:\path\to\dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        Move-Item -Force _PEEPOUT.MAC FOO.MAC
        ```

        Strip unreachable application blocks, then assemble:

        ```powershell
        dccrtlstrip --strip-apps FOO.MAC
        m80c "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime, then assemble and link:

        ```powershell
        Copy-Item C:\path\to\dcc\DCCRTL.MAC DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        m80c "=RTLMIN.MAC" /X /O /Z
        l80c "/P:100,RTLMIN,FOO,FOO/N/E/Y"
        ```

    === "macOS / Linux"

        Compile and optionally optimize:

        ```sh
        dcc -I /path/to/dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        mv _PEEPOUT.MAC FOO.MAC
        ```

        Strip and assemble the application, trim and assemble the runtime,
        then link:

        ```sh
        dccrtlstrip --strip-apps FOO.MAC
        m80c "=FOO.MAC" /X /O /Z /L
        cp /path/to/dcc/DCCRTL.MAC DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        m80c "=RTLMIN.MAC" /X /O /Z
        l80c "/P:100,RTLMIN,FOO,FOO/N/E/Y"
        ```

    Replace `/path/to/dcc` (or `C:\path\to\dcc`) with the checkout
    directory containing the standard headers and `DCCRTL.MAC`. Native
    [`m80c`](appendix/03-utilities.md#native-assembler-m80c) accepts normal host
    line endings; no CP/M text conversion is needed.

    To cross-check compatibility with the original Microsoft tools, use
    `dccmake foo.c dcc-output=FOO dcc-use-emulated-m80=true dcc-use-emulated-l80=true`
    with the project's headers, runtime, and emulated tools configured. That optional path stages
    `M80.COM`/`L80.COM`, converts text to CP/M CRLF, and runs them under
    `ntvcm`.
<!-- markdownlint-enable MD046 -->

## The compiler invocation

```text
dcc [options] input.c [-o output.mac]
```

Common options:

| Option | Meaning |
| --- | --- |
| `-o file` | Write [`m80c`](appendix/03-utilities.md#native-assembler-m80c)-compatible assembly to `file`; default is `out.mac`, `-` is stdout. |
| `-c`, `-module` | Emit a separately compilable module, not a final program translation unit. |
| `-f`, `-ffloatio` | Force `%f` support on every `printf`-family call. |
| `-fl`, `-flongio` | Force 32-bit `long` formats on every `printf`-family call. |
| `-fno-floatio`, `-fno-longio` | Force the corresponding format paths off, overriding automatic detection. |
| `-fhexio`, `-fno-hexio` | Force hexadecimal output support on or off. |
| `-foctio`, `-fno-octio` | Force octal output support on or off. |
| `-g` | Emit full source-debug metadata with conservative code generation. |
| `-gline` | Emit optimized debug metadata while retaining release code generation. |
| `-fstack-check` | Guard function prologues and VLA allocations against stack reserve overflow. |
| `-fno-narrow` | Disable integer-array, scalar, and loop-counter byte-narrowing optimizations. |
| `-s bytes`, `-stack bytes`, `--stack bytes` | Reserve stack bytes; default is 512. |
| `-s=bytes`, `-stack=bytes`, `--stack=bytes` | Equivalent attached forms for the stack size. |
| `-I dir`, `-Idir` | Add an include search directory. |
| `-D name[=value]`, `-Dname[=value]` | Predefine a macro. |
| `-U name`, `-Uname` | Undefine a preprocessor macro. |
| `-v`, `--version` | Print the compiler version and exit. |
| `-h`, `--help` | Print compiler help and exit. |

There is no `-std=c89`, `-std=c99`, or `-std=c11` mode selector: the
[documented language subset](01-c-conformance.md) is always in effect. Unknown
compiler options are currently ignored, so an accepted command line does not
prove that a familiar desktop-compiler flag took effect.

## Options that affect the runtime

For each `printf`-family call with a compile-time literal format, dcc detects
`%f`, long, hexadecimal, and octal conversions and selects the smallest matching
runtime entry automatically. Calls with non-literal formats conservatively
include all of those conversion paths.

- **`-f` / `-ffloatio`** — force floating-point `%f` support on every
    `printf`-family call, including calls whose literal format does not use it.
    This is normally useful only when forcing a whole-program policy; non-literal
    formats already use a conservative fallback.
- **`-fl` / `-flongio`** — similarly force 32-bit `long` formats (`%ld`, `%lu`,
    `%lx`, `%lX`, `%ls`) on every `printf`-family call.
- **`-fno-floatio` / `-fno-longio`** — force the corresponding support off,
    even for a literal that uses it or a non-literal fallback. Use these
    size-oriented overrides only when no affected conversion can reach any call.
    None of these options adds floating-point `scanf` input.
- **`-s` / `-stack` / `--stack`** — reserve stack space (default 512; accepted
  range 0..32767). The heap used by `malloc` lives between the end of BSS and
  the bottom of the stack, so growing the stack shrinks the heap and vice versa.
  By default there are no runtime checks that stop the stack from smashing the
  heap.
- **`-fstack-check`** — opt in to a lightweight stack-overflow guard. The DCC C Compiler emits
  a short `call __stchk` in each function prologue (after the frame is set up)
  that compares the live stack pointer against the heap ceiling. If the stack
  has grown into the heap, the program prints `?stack overflow` and exits with
  return code `0FFh` instead of silently corrupting memory. The guard costs a
  few bytes and one call per function, so it is **off by default**; turn it on
    while developing or for deeply recursive code. The `stacksize` utility
    (below) uses this guard to measure the minimum `-stack` reserve an app needs.
    This option sets the initial state for the translation unit; source can then
    use [`#pragma stack_check(on)` / `#pragma stack_check(off)`](03-types-and-conventions.md#supported-pragmas)
    to control guard emission in source order.
- **`-Dname[=value]`** — predefine a macro. `_DCC_=1` is always defined.

### Measuring the stack an app needs

The repo ships a `stacksize` utility that builds your app with `-fstack-check`
forced on and sweeps the `-stack` reserve upward until it runs without tripping
the guard, then prints the minimum and a recommended value with headroom. Run it
against an app/test name (and pass any program arguments after `--`):

<!-- markdownlint-disable MD046 -->
=== "Windows"

    ```bat
    rem simple app
    scripts\stacksize.bat triangle

    rem app that needs a data-file argument
    scripts\stacksize.bat cobint -- e.cob
    ```

=== "macOS"

    ```sh
    # simple app
    scripts/stacksize.sh triangle

    # app that needs a data-file argument
    scripts/stacksize.sh cobint -- e.cob
    ```

=== "Ubuntu"

    ```sh
    # simple app
    scripts/stacksize.sh triangle

    # app that needs a data-file argument
    scripts/stacksize.sh cobint -- e.cob
    ```

=== "Ubuntu ARM64"

    ```sh
    # simple app
    scripts/stacksize.sh triangle

    # app that needs a data-file argument
    scripts/stacksize.sh cobint -- e.cob
    ```

=== "Windows ARM64"

    ```bat
    rem simple app
    scripts\stacksize.bat triangle

    rem app that needs a data-file argument
    scripts\stacksize.bat cobint -- e.cob
    ```
<!-- markdownlint-enable MD046 -->

Both honour the same `START` / `STEP` / `MAX` / `MODE` / `EMU` environment
variables; see [`scripts/README.md`](https://github.com/davidly/dcc/blob/main/scripts/README.md)
for the full reference.

## Including headers

Include the standard headers as usual:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
```

DCC searches the name as given (including the working directory), then the
including file's directory, then each `-I` directory in command-line order.
The bundled headers live beside `DCCRTL.MAC` at the root of the source checkout.
For an external `dccmake` project, set `dcc-include-directory=/path/to/dcc`
and `dcc-runtime=/path/to/dcc/DCCRTL.MAC`, replacing `/path/to/dcc` with that
checkout's path. When invoking the compiler directly, use `-I /path/to/dcc`.

!!! warning "Missing system headers are silently ignored"
    An unresolved `<header.h>` include is skipped; an unresolved
    `"header.h"` include is an error. Successful compilation therefore does
    not prove that a system header was found. Missing prototypes can produce
    wrong calls, especially for pointer, `long`, or `float` results. Use DCC's
    headers, not your host compiler's standard-library headers.

## Multi-module symbol names

[`m80c`](appendix/03-utilities.md#native-assembler-m80c) and
[`l80c`](appendix/03-utilities.md#native-linker-l80c) keep only the **first 6
characters** of a public (external) symbol.
DCC C Compiler emits each external C identifier as `_` followed by the name, so the leading
underscore consumes one of those six characters. The practical rule for any
program built from more than one `.c` file is:

> **Every non-`static` function and non-`static` file-scope variable must be
> unique within its first 5 characters across all linked modules.**

Names that only differ after the fifth character collapse to the same public
symbol. For example `i_idxins`, `i_idxbld`, and `i_idxlookup` all become
`_I_IDX` and are indistinguishable to the linker.

Anything used in only one translation unit should be declared `static`. A
`static` symbol has internal linkage, so DCC C Compiler gives it a private, generated
assembler name and the 6-character rule does not apply to it.

### How a collision shows up

<!-- markdownlint-disable MD046 -->
- **Within one file**, DCC C Compiler catches it at compile time and stops with an error
  naming both symbols, for example:

    ```text
    global names 'i_idxins' and 'i_idxbld' are not distinguishable in M80's
    6 significant character public symbols (both become '_I_IDX'); rename one
    ```
<!-- markdownlint-enable MD046 -->

- **Across different files**, DCC C Compiler cannot see the clash.
  [`l80c`](appendix/03-utilities.md#native-linker-l80c) reports a multiply
  defined global rather than silently binding a call to the wrong definition.

### Fixing collisions

- Rename the offending identifiers so they differ within the first 5
  characters (put the distinguishing letters early: `ixins`, `ixbld`,
  `ixlook` rather than a shared `i_idx…` prefix).
- Or make single-file helpers `static`.

Struct, union, and enum tags, `typedef` names, struct members, macros, enum
constants, and local variables never become public symbols, so they are exempt.

### Detecting collisions

After a build, scan the emitted `.MAC` modules for external names that share a
6-character prefix:

```sh
grep -rhiE '^[[:space:]]*public ' build/*.MAC \
  | awk '{print $2}' | sort -u \
  | awk '{k=toupper(substr($0,1,6));
          if (seen[k]) print "COLLISION " k ": " first[k] " <> " $0;
          else { seen[k]=1; first[k]=$0 }}'
```

Any line printed is a pair you must rename or make `static`.

## Memory layout

CP/M loads `.COM` files in one way. BSS begins immediately after the loaded
image. The CP/M loader itself sets `SP` to its own small CCP stack, not to the
top of free memory; DCCRTL's startup code is what then sets `SP` to the
highest free byte before your program runs. The heap grows on demand between
the end of BSS and the bottom of the stack. Because there is no guard between
them by default, size the stack deliberately with `-stack` for programs with
deep recursion or large frames — or build with `-fstack-check` (above) to turn
an overflow into a clean `?stack overflow` exit.
