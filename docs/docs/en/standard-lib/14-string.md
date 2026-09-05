# String handling (`string.h`)

Include [`string.h`](14-string.md) for byte-string and raw memory operations.

## Functions

<!-- STRING-FUNCTION-TABLE: all -->

## Runtime model

The string and memory functions operate on byte strings and raw byte buffers.
In the fixed [`C` locale](17-locale.md), `strcoll` is equivalent to `strcmp`
and `strxfrm` uses the identity transform.

## String and memory operations

String functions require NUL-terminated input and sufficient destination
storage. `strlen` excludes the NUL. Comparisons promise a negative, zero, or
positive result, not specifically `-1`, `0`, or `1`.

- Use `memmove` when source and destination overlap; `memcpy` requires
    non-overlapping objects. Memory functions operate on exact byte counts and
    do not stop at NUL.
- `strcpy` and `strcat` have no capacity argument. Include space for the
    terminating NUL when allocating the destination.
- `strncpy(dst, src, n)` pads a short source with NULs, but does **not**
    terminate the result when the source has at least `n` characters.
- `strncat` limits characters appended, not total destination capacity. It
    still writes a terminating NUL after them.
- `strchr`, `strrchr`, `strstr`, and `memchr` return pointers into the input
    object or `NULL`; they do not allocate a copy.

```c
#include <string.h>

char dst[16];
strcpy(dst, "hello");
if (strcmp(dst, "hello") == 0)
    memset(dst, 0, sizeof dst);
```

Tokenizing a copy of a string with `strtok` writes NULs into the buffer. Pass
only modifiable strings; do not pass string literals:

```c
#include <stdio.h>
#include <string.h>

char line[] = "alpha,beta,,gamma";
char *tok = strtok(line, ",");
while (tok) {
    puts(tok);                 /* alpha, beta, gamma */
    tok = strtok(NULL, ",");
}
```

!!! note "`strdup` is the exception"
    Most string routines link nothing extra, but `strdup` allocates, so it
    inherits the whole `malloc` chain. See the
    [appendix](../appendix/01-dccrtlstrip.md).

`strdup` and ASCII-only `stricmp` are extensions, not C89 functions. Check
`strdup` for `NULL` and release successful copies with `free`. `strtok` keeps
one internal parsing cursor, so interleaving tokenization of separate strings
overwrites the earlier scan state.
