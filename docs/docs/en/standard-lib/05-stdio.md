# Console and file I/O (`stdio.h`)

Include [`stdio.h`](05-stdio.md). The predefined streams `stdin`, `stdout`, and
`stderr` are available.

## Types, Macros, and Streams

<!-- STDIO-SYMBOL-TABLE: all -->

## Functions

<!-- STDIO-FUNCTION-TABLE: all -->

## Runtime model

`stdin`, `stdout`, and `stderr` are console pseudo-streams. They are available
for portable-looking code, but they do not consume real file slots. Real file
streams are opened by `fopen`. The header advertises `FOPEN_MAX == 8`, the
minimum guaranteed stream count, while the current runtime has eight real-file
slots in addition to the three console pseudo-streams. Low-level `open` calls
and `fopen` share those slots. `FILE *` values encode handles; do not
dereference them or allocate your own `FILE` objects.

!!! tip "Console output is cheap"
    `putchar`, `puts`, and integer `printf` write through the console routine
    that is already part of every program. The stream-oriented functions
    (`fputc`/`fputs`/`fprintf`) have special `stdout`/`stderr` console paths at
    runtime, but their blocks also include real-file fallbacks; using them can
    therefore link more file I/O support than console-only calls need. Prefer
    the console functions for console-only work.
    See the [appendix](../appendix/01-dccrtlstrip.md) for the size figures.

## File streams

`fopen` returns a stream on success or `NULL` on failure, with `errno` set.
The first mode character selects the file operation:

| Mode | Operation |
| --- | --- |
| `"r"` | Open an existing file for reading. |
| `"w"` | Create or truncate a file for writing. |
| `"a"` | Open or create a file for writing at its tracked logical end. |

Add `+` for both reading and writing (`"r+"`, `"w+"`, `"a+"`). Access modes
are enforced. Add `b` for binary mode, including `"rb+"` or `"r+b"`;
`fgets` then treats Ctrl-Z as data rather than text EOF. Invalid modes,
including duplicate `+` or `b`, fail with `EINVAL`.

`fread` and character-at-a-time file reads transfer raw bytes in either mode;
do not assume every input function applies the `fgets` text-EOF convention.
Binary mode does not remove CP/M record padding or turn append into atomic I/O.
CP/M has no atomic append operation, so programs that share or alternate writes
to the same file must coordinate at a higher level. See
[File I/O and CP/M BDOS conventions](../10-system-and-cpm.md#file-io-and-cpm-bdos-conventions)
for how record-granularity storage affects exactly where that "true end"
lands.

For a complete program, see the worked
[file-reading example](../12-examples.md#reading-a-text-file-line-by-line).

## Formatted I/O

### printf-family output

The `printf` family shares one base formatting engine for characters, strings,
16-bit integers, and literal percent signs. The inline-argument forms are used
the usual way:

```c
printf("count=%d name=%s hex=%04x\n", n, name, addr);
sprintf(line, "%ld bytes", total);
fprintf(stderr, "error %d\n", errno);
```

The `v…` variants take a `va_list` (from
[`stdarg.h`](10-stdarg.md)) for wrappers that forward a format string; see the worked
[logging-wrapper example](../12-examples.md#a-printf-style-logging-wrapper).

!!! note "Automatic runtime selection"
  For a compile-time literal format, dcc detects float, long, hexadecimal, and
  octal conversions at each call and selects the smallest matching runtime
  entry. A non-literal format conservatively includes all optional paths.
  `-f` / `-ffloatio` and `-fl` / `-flongio` force float or long support on
  every `printf`-family call; they are not required for literal formats. The
  `-fno-*io` forms force paths off and are unsafe when a matching conversion
  can reach a call. These options do not add floating-point input.

### printf conversions

| Specifier | Meaning |
| --- | --- |
| `%d`, `%i` | Signed 16-bit decimal. |
| `%u` | Unsigned 16-bit decimal. |
| `%o` | Unsigned 16-bit octal. |
| `%x`, `%X` | Unsigned 16-bit hex (lower / upper case). |
| `%c` | Single character. |
| `%s` | NUL-terminated string. |
| `%f` | 32-bit float in fixed decimal notation. |
| `%%` | A literal percent sign. |

Length modifiers:

- `l` — 32-bit `long` conversions: `%ld`, `%lu`, `%lx`, `%lX`. `%ls` prints a
  16-bit wide string by emitting each character's low byte.
- `z` — `size_t` width. Since `size_t` is 16-bit here, `%zu` / `%zd` and friends
  behave like the plain 16-bit conversions.

Field width and flags:

- A decimal **field width** (`%6d`, `%10s`, `%8lx`) pads with spaces on the
  left for characters, strings, and integer conversions. `%f` does not apply
  field width.
- The `-` flag (`%-6d`) left-justifies `%c`, `%s`, and integer conversions
  within the field. `%ls` uses right justification only.
- A leading `0` flag on integer conversions (`%04d`, `%08lx`) zero-pads when
  there is no `-` flag and no explicit precision.
- A **precision** on integer conversions (`%.4d`) zero-fills to at least that
  many digits.
- A positive precision on `printf` `%f` (`%.2f`) controls the number of digits
  after the decimal point. Without a positive precision, `%f` prints six
  fractional digits. `%f` always emits a decimal point.
- A precision on `%s` (`%.3s`) limits output to at most that many characters,
  stopping at NUL first when the string is shorter. `%.0s` emits no characters
  and does not dereference the string argument.

```c
printf("|%6d|%-6d|%.4d|\n", 42, 42, 42);   /* |    42|42    |0042| */
printf("%lu items, %lx flags\n", count, mask);
printf("%.2f\n", ratio);
```

Not supported: the `+`, space, and `#` flags, and `*` (run-time) width or
precision. Output conversions such as `%e`, `%g`, `%p`, and `%n` are not
implemented. `%f` is supported across `printf`, `sprintf`, `fprintf`, their
`v...` variants, `snprintf`, and `vsnprintf`. Use fixed widths and precisions in
the format string.

### Bounded string output

Prefer `snprintf` to `sprintf` when the destination has a fixed capacity.
`snprintf` and `vsnprintf` write at most `n - 1` characters and a terminating
NUL when `n > 0`. With `n == 0`, they write nothing and the destination may
be `NULL`. The return value counts the characters that would have been
written, excluding NUL, so a result at least as large as the capacity means
truncation:

```c
char line[32];
int count = snprintf(line, sizeof line, "%ld bytes", 123456L);
if (count < 0 || (size_t)count >= sizeof line)
  puts("formatted output did not fit");
```

The count uses DCC's 16-bit `int`; keep the total formatted length within
`INT_MAX`, even when the destination itself is small. The runtime count is
not a wider host-sized integer.

### scanf-family input

`scanf`, `sscanf`, and `fscanf` share a separate, non-floating C89 subset. A
whitespace character in the format skips any amount of input whitespace, and a
non-`%` character must match literally.

| Specifier | Meaning |
| --- | --- |
| `%d` | Signed decimal integer. |
| `%i` | Signed integer with base auto-detected (`0`, `0x`). |
| `%u` | Unsigned decimal integer. |
| `%x`, `%X` | Unsigned hexadecimal, optional `0x` prefix. |
| `%o` | Unsigned octal. |
| `%c` | Character input; does not skip leading whitespace. |
| `%s` | Non-whitespace string, NUL-terminated by the runtime. |
| `%%` | A literal percent sign. |

Supported modifiers:

- A decimal field width limits the maximum input consumed by a conversion.
- `*` suppresses assignment (input is consumed but no argument is read).
- `h` is accepted for integer conversions; since `short` is the same size as
  `int`, it behaves like the plain conversion.
- `l` stores integer conversions through a `long *` / `unsigned long *`.
- Integer conversions skip leading whitespace and accept an optional `+` or
  `-`. `%i` auto-detects decimal, octal (`0`), or hexadecimal (`0x` / `0X`).
- `%s` skips leading whitespace and writes a terminating NUL. `%c` does not skip
  whitespace; without a width it reads exactly one character, and with a width
  it does not add a terminating NUL.

```c
int  value;
char word[16];
long big;

sscanf("-12 hello 0x2a", "%d %15s %i", &value, word, &value);
sscanf("123456", "%ld", &big);
```

The return value is the number of assignments completed, or `EOF` when input
fails before any assignment. Check it before using the destinations. Always
bound `%s` to one less than the destination capacity; unlike `%s`, `%c` does
not append NUL.

For a complete, tested program, see the worked
[`sscanf` parsing example](../12-examples.md#parsing-input-with-sscanf).

Not supported: floating input (`%f`, `%e`, `%g`), scansets (`%[...]`), `%n`, and
`%p`. No DCC C Compiler option enables floating-point `scanf` input.

## Character and string I/O

`putchar` and `puts` write to the console. `putc` and `fputc` are separate C
names that map to stream-character output, and `getc` and `fgetc` likewise
share the stream-character input path. `fgets` reads up to `n-1` characters and
terminates the buffer with NUL; `fputs` writes a string without adding a newline.

`getchar` is the console input form. In the runtime it calls CP/M BDOS function
1 (console input), so it waits for a character and returns that character as a
non-negative `int`. For real file streams, `getc` and `fgetc` read through the
low-level `_read` routine. See
[CP/M extensions](../10-system-and-cpm.md#non-blocking-console-input) for more on
the non-blocking convention.

`gets` is provided for C89 compatibility only. It cannot limit the number of
characters written to the destination buffer; use `fgets` for new code.

For non-blocking console input, use `kbhit()` to test whether a key is waiting,
then `getch()` to read it. `kbhit()` never blocks; `getch()` only blocks if you
call it when no key is ready, so the two are normally paired:

```c
#include <stdio.h>

int main(void)
{
  int ch;

  puts("Press Q to quit.");
  for (;;) {
    if (kbhit()) {              /* non-blocking: a key is waiting */
      ch = getch();             /* safe: will not block here */
      if (ch == 'q' || ch == 'Q')
        break;
      putchar(ch);
    }

    /* do other periodic work here */
  }
  return 0;
}
```

Because `getch()` builds on BDOS function 6 (which uses `0` as its "no
character" sentinel), this convention is best for keyboard polling and command
loops, not for input protocols where a NUL byte is meaningful. `getch()` and
`getchar()` flush any pending buffered console output before they block, so a
prompt printed just before them is always visible first.

## Block I/O

`fread` and `fwrite` transfer `nmemb` elements of `size` bytes and return the
number of complete elements transferred. A zero size or count returns zero.
If `size * nmemb` exceeds 16-bit `size_t`, they return zero without transferring
data. A short transfer can indicate EOF or an error; inspect `feof` and
`ferror` after the operation. Raw file reads can include trailing record
padding, even on a text stream.

File-stream writes are write-through. Console writes through `stdout` or
`stderr` use the console output buffer described below.

## Positioning and status

`fseek` repositions a real file stream using `SEEK_SET`, `SEEK_CUR`, or
`SEEK_END`; it returns zero on success and nonzero on failure. `ftell` reports
the current 32-bit byte offset, or `-1L` on failure, not the file's length.
`fgetpos`/`fsetpos` save and restore that offset through `fpos_t`. A successful
seek clears EOF and discards pushback. `rewind` seeks to the beginning and
clears status. `feof`, `ferror`, and `clearerr` inspect or reset the status
flags; `feof` does not predict whether the next read will succeed.

`ungetc` provides one character of pushback per stream. Check for `EOF` on
failure; a successful pushback clears EOF. `tmpfile` creates a temporary update
stream removed on `fclose` or normal exit. Prefer it to generating a name with
`tmpnam` and opening that name separately. Explicitly close ordinary files and
check `fclose` for failure: write-through data does not mean CP/M directory
metadata is finalized before close.

`fflush(NULL)`, `fflush(stdout)`, and `fflush(stderr)` all flush the console.
Calling `fflush` on a real file stream is accepted; file writes are already
write-through.

## Console output buffering

Console output (`printf`, `puts`, `putchar`, `fputs`/`fwrite`/`fprintf` to
`stdout`/`stderr`) is **buffered**. Characters accumulate in a buffer and are
sent to CP/M in batches, which is far faster than one BDOS call per character.
The buffer is drained automatically:

- when it fills,
- when a newline is written (in the default line-buffered mode),
- on `fflush()`,
- before any blocking console **input** (`getchar`, `getch`, `scanf`, `fgets`),
  so a prompt printed just before input is always visible first,
- and at normal program exit (`main` return, `exit()`).

`setvbuf` returns `0` on success, non-zero only for an invalid `mode`. `setbuf`
is shorthand for `setvbuf(fp, buf, buf ? _IOFBF : _IONBF, BUFSIZ)`.

`setvbuf` must be called **before** any output on the stream. The `mode` is one
of:

| Mode | Meaning |
| --- | --- |
| `_IOFBF` | Fully buffered — drain only when the buffer fills, on `fflush`, before input, or at exit. |
| `_IOLBF` | Line buffered — additionally drain at each newline. This is the default. |
| `_IONBF` | Unbuffered — drain after every character. |

If you pass a non-`NULL` `buf` (with `size >= 2`), the runtime **adopts your
buffer**: console output is accumulated there and a larger buffer means fewer
BDOS calls. One byte of `size` is reserved internally, so the usable capacity is
`size - 1`. A `NULL` `buf` (or `size < 2`) uses the runtime's small internal
buffer instead.

!!! warning "Buffer lifetime"
    An adopted buffer must remain valid for as long as the stream uses it. Before
    freeing it, detach it first — e.g. `setvbuf(stdout, NULL, _IOLBF, 0)` — so
    the automatic exit-time flush does not touch freed memory.

`setvbuf`/`setbuf` only configure the **console** (`stdout`/`stderr`). Called on
a file stream they are accepted but have no effect. `stdout` and `stderr` share
the one console buffer, so their output is interleaved in the exact order
written.

```c
#include <stdio.h>
#include <stdlib.h>

char *buf = malloc(4096);
setvbuf(stdout, buf, _IOFBF, 4096);  /* batch up to ~4 KB before each flush */

for (i = 0; i < 1000; i++)
    printf("line %d\n", i);          /* accumulates; few BDOS calls */

setvbuf(stdout, NULL, _IOLBF, 0);    /* detach before freeing */
free(buf);
```
