# String handling (`string.h`)

Include [`string.h`](14-string.md) for byte-string and raw memory operations.

## Functions

<!-- STRING-FUNCTION-TABLE: all -->

## Runtime model

The string and memory functions operate on byte strings and raw byte buffers.
In the fixed [`C` locale](17-locale.md), `strcoll` is equivalent to `strcmp`
and `strxfrm` uses the identity transform.

## String and memory operations

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
