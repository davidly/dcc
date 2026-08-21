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
`dccrtlstrip`, then M80 and L80. Runtime trimming is part of the normal build
path because it keeps unused library routines out of the final `.COM` file. The
script resolves each tool from your `PATH` or from the `DCC`, `DCCPEEP`, and
`DCCRTLSTRIP` environment variables.

The native linker normally uses `/P:100`, CP/M's standard `.COM` load address.
For fixed-address, overlay, or relocating system programs, `l80c` also supports
LINK-80-style nonstandard origins and generates the required CP/M entry
jump/bootstrap. See [Native Linker (`l80c`)](appendix/03-utilities.md#native-linker-l80c)
for its CLI options, origin layouts, and limitations.

??? note "The manual pipeline (click to expand)"

    For manual builds or custom build systems, the full pipeline for `foo.c` is
    shown below. `dcc`, `dccpeep`, and `dccrtlstrip` run on the host;
    `m80.com` and `l80.com` are CP/M programs, so run them through `ntvcm` or
    another CP/M emulator:

    === "Windows"

        Define a CRLF helper using PowerShell/.NET APIs.

        ```powershell
        function Convert-ToCrlf($Path) {
            $text = [IO.File]::ReadAllText($Path) -replace "`r?`n", "`r`n"
            [IO.File]::WriteAllText($Path, $text)
        }
        ```

        Compile the C source to M80 assembly, then optionally run the peephole
        optimizer.

        ```powershell
        dcc -I C:\path\to\dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        Move-Item -Force _PEEPOUT.MAC FOO.MAC
        ```

        Convert the app assembly to CP/M CRLF text and assemble it with M80 under
        `ntvcm`.

        ```powershell
        Convert-ToCrlf FOO.MAC
        ntvcm m80 "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime to only the blocks used by the app.

        ```powershell
        Copy-Item C:\path\to\dcc\DCCRTL.MAC DCCRTL.MAC
        Convert-ToCrlf DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        ```

        Convert, assemble, and link the trimmed runtime with the app.

        ```powershell
        Convert-ToCrlf RTLMIN.MAC
        ntvcm m80 "=RTLMIN.MAC" /X /O /Z
        ntvcm l80 "/P:100,RTLMIN,FOO,FOO/N/E"
        ```

    === "macOS"

        Define a CRLF helper: prefer `unix2dos` if it is installed, otherwise
        use Perl (available on macOS).

        ```sh
        to_crlf() {
            if command -v unix2dos >/dev/null 2>&1; then
                unix2dos "$1" >/dev/null 2>&1 || true
            else
                perl -0pi -e 's/\r?\n/\r\n/g' "$1"
            fi
        }
        ```

        Compile the C source to M80 assembly, then optionally run the peephole
        optimizer.

        ```sh
        dcc -I /path/to/dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        mv _PEEPOUT.MAC FOO.MAC
        ```

        Convert the app assembly to CP/M CRLF text and assemble it with M80 under
        `ntvcm`.

        ```sh
        to_crlf FOO.MAC
        ntvcm m80 "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime to only the blocks used by the app.

        ```sh
        cp /path/to/dcc/DCCRTL.MAC DCCRTL.MAC
        to_crlf DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        ```

        Convert, assemble, and link the trimmed runtime with the app.

        ```sh
        to_crlf RTLMIN.MAC
        ntvcm m80 "=RTLMIN.MAC" /X /O /Z
        ntvcm l80 "/P:100,RTLMIN,FOO,FOO/N/E"
        ```

    === "Ubuntu"

        Define a CRLF helper: prefer `unix2dos` if it is installed, otherwise
        use Perl (available on Ubuntu).

        ```sh
        to_crlf() {
            if command -v unix2dos >/dev/null 2>&1; then
                unix2dos "$1" >/dev/null 2>&1 || true
            else
                perl -0pi -e 's/\r?\n/\r\n/g' "$1"
            fi
        }
        ```

        Compile the C source to M80 assembly, then optionally run the peephole
        optimizer.

        ```sh
        dcc -I /path/to/dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        mv _PEEPOUT.MAC FOO.MAC
        ```

        Convert the app assembly to CP/M CRLF text and assemble it with M80 under
        `ntvcm`.

        ```sh
        to_crlf FOO.MAC
        ntvcm m80 "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime to only the blocks used by the app.

        ```sh
        cp /path/to/dcc/DCCRTL.MAC DCCRTL.MAC
        to_crlf DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        ```

        Convert, assemble, and link the trimmed runtime with the app.

        ```sh
        to_crlf RTLMIN.MAC
        ntvcm m80 "=RTLMIN.MAC" /X /O /Z
        ntvcm l80 "/P:100,RTLMIN,FOO,FOO/N/E"
        ```

    === "Ubuntu ARM64"

        Define a CRLF helper: prefer `unix2dos` if it is installed, otherwise
        use Perl (available on Ubuntu).

        ```sh
        to_crlf() {
            if command -v unix2dos >/dev/null 2>&1; then
                unix2dos "$1" >/dev/null 2>&1 || true
            else
                perl -0pi -e 's/\r?\n/\r\n/g' "$1"
            fi
        }
        ```

        Compile the C source to M80 assembly, then optionally run the peephole
        optimizer.

        ```sh
        dcc -I /path/to/dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        mv _PEEPOUT.MAC FOO.MAC
        ```

        Convert the app assembly to CP/M CRLF text and assemble it with M80 under
        `ntvcm`.

        ```sh
        to_crlf FOO.MAC
        ntvcm m80 "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime to only the blocks used by the app.

        ```sh
        cp /path/to/dcc/DCCRTL.MAC DCCRTL.MAC
        to_crlf DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        ```

        Convert, assemble, and link the trimmed runtime with the app.

        ```sh
        to_crlf RTLMIN.MAC
        ntvcm m80 "=RTLMIN.MAC" /X /O /Z
        ntvcm l80 "/P:100,RTLMIN,FOO,FOO/N/E"
        ```

    === "Windows ARM64"

        Define a CRLF helper using PowerShell/.NET APIs.

        ```powershell
        function Convert-ToCrlf($Path) {
            $text = [IO.File]::ReadAllText($Path) -replace "`r?`n", "`r`n"
            [IO.File]::WriteAllText($Path, $text)
        }
        ```

        Compile the C source to M80 assembly, then optionally run the peephole
        optimizer.

        ```powershell
        dcc -I C:\path\to\dcc -stack 512 foo.c -o FOO.MAC
        dccpeep FOO.MAC _PEEPOUT.MAC
        Move-Item -Force _PEEPOUT.MAC FOO.MAC
        ```

        Convert the app assembly to CP/M CRLF text and assemble it with M80 under
        `ntvcm`.

        ```powershell
        Convert-ToCrlf FOO.MAC
        ntvcm m80 "=FOO.MAC" /X /O /Z /L
        ```

        Copy and trim the runtime to only the blocks used by the app.

        ```powershell
        Copy-Item C:\path\to\dcc\DCCRTL.MAC DCCRTL.MAC
        Convert-ToCrlf DCCRTL.MAC
        dccrtlstrip -r DCCRTL.MAC -o RTLMIN.MAC FOO.MAC
        ```

        Convert, assemble, and link the trimmed runtime with the app.

        ```powershell
        Convert-ToCrlf RTLMIN.MAC
        ntvcm m80 "=RTLMIN.MAC" /X /O /Z
        ntvcm l80 "/P:100,RTLMIN,FOO,FOO/N/E"
        ```

    `scripts/ma.ps1` stages `m80.com` and `l80.com` before invoking `ntvcm`.
    For a manual build, keep those `.COM` files and `DCCRTL.MAC` in the working
    directory where you run the pipeline, or adjust the paths to match your
    layout. Replace `/path/to/dcc` (or `C:\path\to\dcc`) with the DCC C Compiler repo path
    that contains the standard headers; if you run from the DCC C Compiler repo root, the
    explicit `-I` is usually unnecessary.

    M80 expects CP/M-style CRLF text files; LF-only files can be misread. The Unix
    function shown above uses Perl or `unix2dos`; the Windows function uses
    PowerShell/.NET APIs.

## The compiler invocation

```text
dcc [options] input.c [-o output.mac]
```

Common options:

| Option | Meaning |
| --- | --- |
| `-o file` | Write M80 assembly to `file`; default is `out.mac`, `-` is stdout. |
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

M80 and L80 only keep the **first 6 characters** of a public (external) symbol.
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

- **Across different files**, DCC C Compiler cannot see the clash. L80 may report
  `%Mult. Def. Global`, or — worse — silently bind a call to the wrong
  definition, so the program links but misbehaves at runtime.

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
