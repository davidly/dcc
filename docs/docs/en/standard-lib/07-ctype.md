# Character handling (`ctype.h`)

Include [`ctype.h`](07-ctype.md) for ASCII character classification and case
conversion functions.

## Functions

<!-- CTYPE-FUNCTION-TABLE: all -->

## Runtime model

The character functions use ASCII classification rules. Each function takes an
`int`; the classification functions return non-zero for a match, while
`toupper` and `tolower` return the converted character or the original character
when no conversion applies.

Pass either `EOF` or a value representable as `unsigned char`; this matters for
portable source that might handle bytes with the high bit set.

## Character classification and conversion

```c
#include <ctype.h>

void uppercase(char *s)
{
    char *p;

    for (p = s; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
}
```
