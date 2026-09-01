# Building and linking

This page covers the path from a `.c` file to a runnable CP/M `.COM` program and
the compiler options that most affect the result. You typically do this in your
**own project directory** — DCC C Compiler and ntvcm are general-purpose tools for building
CP/M / Z80 C apps anywhere, not just inside the DCC C Compiler repo. As long as the tools
are on your `PATH` (see [Setting up the toolchain](00-setup-toolchain.md)), the
commands below work from any folder that holds your `.c` sources.

## The build script

The primary installed build command is `dcc-ma`. It compiles, optimizes,
strips the runtime, assembles, and links in one step.

With an installed package, use the common wrapper command on Windows, macOS, and
Linux:

```sh
dcc-ma foo --mode fast       # builds foo.c -> FOO.COM
dcc-ma foo --mode nopeep     # skip the dccpeep optimizer
```

From a source checkout, run the implementation script directly:

```sh
./scripts/ma.sh foo --mode fast
```

On Windows, use the PowerShell driver. It works with the Windows PowerShell 5.1
already included with Windows, as well as PowerShell 7+:

```pwsh
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 foo -Mode fast    # builds foo.c -> FOO.COM
powershell.exe -ExecutionPolicy Bypass -File .\scripts\ma.ps1 foo -Mode nopeep  # skip the dccpeep optimizer
```

This runs the compiler, the optional `dccpeep` peephole optimizer,
`dccrtlstrip`, [`m80c`](appendix/03-utilities.md#native-assembler-m80c), and
[`l80c`](appendix/03-utilities.md#native-linker-l80c). Whole-program
application stripping and runtime trimming are both part of the normal build
path, keeping unreachable app functions/objects and unused library routines out
of the final `.COM` file. The script resolves each tool from your `PATH` or its
documented environment-variable override.

The native linker normally uses `/P:100`, CP/M's standard `.COM` load address.
For fixed-address, overlay, or relocating system programs,
[`l80c`](appendix/03-utilities.md#native-linker-l80c) also supports LINK-80-style
nonstandard origins and generates the required CP/M entry jump/bootstrap. Its
utility reference covers the CLI options, origin layouts, and limitations.

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

    Replace `/path/to/dcc` (or `C:\path\to\dcc`) with the checkout or install
    directory containing the standard headers and `DCCRTL.MAC`. Native
    [`m80c`](appendix/03-utilities.md#native-assembler-m80c) accepts normal host
    line endings; no CP/M text conversion is needed.

    To cross-check compatibility with the original Microsoft tools, use
    `dcc-ma --emulated-m80 --emulated-l80` instead. That optional path stages
    `M80.COM`/`L80.COM`, converts text to CP/M CRLF, and runs them under
    `ntvcm`.

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
| `-fstack-check` | Emit a lightweight stack-overflow guard in each function prologue. |
| `-s bytes`, `-stack bytes`, `--stack bytes` | Reserve stack bytes; default is 512. |
| `-s=bytes`, `-stack=bytes`, `--stack=bytes` | Equivalent attached forms for the stack size. |
| `-I dir`, `-Idir` | Add an include search directory. |
| `-D name[=value]`, `-Dname[=value]` | Predefine a macro. |
| `-U name`, `-Uname` | Undefine a preprocessor macro. |
| `-v`, `--version` | Print the compiler version and exit. |
| `-h`, `--help` | Print compiler help and exit. |

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

- **Within one file**, DCC C Compiler catches it at compile time and stops with an error
  naming both symbols, for example:

    ```text
    global names 'i_idxins' and 'i_idxbld' are not distinguishable in M80's
    6 significant character public symbols (both become '_I_IDX'); rename one
    ```

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
