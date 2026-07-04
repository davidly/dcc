# Error reporting (`errno.h`)

Include [`errno.h`](02-errno.md) for the runtime error indicator and numeric
error codes.

## Variables and Macros

<!-- ERRNO-SYMBOL-TABLE: all -->

## Runtime model

The DCC C Compiler runtime is single-threaded, so `errno` is one global `int`. Runtime calls
set it when they fail; successful calls are not required to clear it. Test the
function result first, then inspect `errno` only on failure.

C89 requires `EDOM` and `ERANGE`. DCC C Compiler also defines conventional small-system and
POSIX-style file errors used by the CP/M file and directory support.

## Error checks

```c
#include <errno.h>
#include <stdio.h>

FILE *fp = fopen("DATA.TXT", "r");
if (!fp) {
    if (errno == ENOENT)
        puts("missing file");
    else
        printf("open failed: %d\n", errno);
}
```

Use `strerror(errno)` from [`string.h`](14-string.md) or `perror`
from [`stdio.h`](05-stdio.md) when you want runtime text for an error code.
