# Types and conventions

The DCC C Compiler uses a compact 16-bit model. Knowing the exact widths up front avoids most
overflow and precision surprises.

## Type sizes

| Type | Size | Notes |
| --- | --- | --- |
| `char` | 8 bits | signed by default; range -128..127 |
| `unsigned char` | 8 bits | range 0..255; useful for raw bytes and table indexes |
| `int`, `short` | 16 bits | signed range -32768..32767; `int` is 16-bit, so watch for overflow |
| `unsigned int`, `unsigned short` | 16 bits | range 0..65535; use `%u` / `%x` / `%X` for formatted output |
| `long` | 32 bits | signed range -2147483648..2147483647; use `%ld` and the `l` length modifier |
| `unsigned long` | 32 bits | range 0..4294967295; use `%lu` / `%lx` / `%lX` |
| `float` | 32 bits | the only floating type — **no `double`** |
| pointer | 16 bits | flat CP/M address space |
| `size_t` | 16 bits | unsigned `int` |
| `ptrdiff_t` | 16 bits | signed `int`; result of subtracting two pointers |
| `wchar_t` | 16 bits | unsigned `int`; shared by [stddef.h](standard-lib/12-stddef.md) and [stdint.h](standard-lib/13-stdint.md) |
| `FILE` | 16 bits | `typedef int FILE`; streams are small handles |

The practical consequences:

- Use `long` (and `%ld`) whenever a value can exceed ±32767.
- Use `unsigned` / `unsigned long` when you need wraparound arithmetic or a
  logical right shift; signed right shift sign-extends.
- `float` carries about 7 decimal digits (a 24-bit significand). Integers up to
  `2^24` (16,777,216) are exact; beyond that, converting a large `long`
  to `float` rounds to the nearest single. See [Floating-point math](standard-lib/08-math.md)
  for the full set of precision gotchas.

## Useful constants

From [stdio](standard-lib/05-stdio.md):

- `EOF` = -1, `BUFSIZ` = 256, `SEEK_SET` / `SEEK_CUR` / `SEEK_END` = 0 / 1 / 2.

From [stdlib](standard-lib/06-stdlib.md):

- `EXIT_SUCCESS` = 0, `EXIT_FAILURE` = 1, `RAND_MAX` = 32767, `NULL` = 0.

From [errno.h](standard-lib/02-errno.md):

- `EDOM` = 33, `ERANGE` = 34.

## Fixed-width integer names (`stdint.h`)

[`stdint.h`](standard-lib/13-stdint.md) provides fixed-width typedefs that match the target model:

| Name | Definition |
| --- | --- |
| `int8_t` | signed 8-bit `char` |
| `uint8_t` | unsigned 8-bit `char` |
| `int16_t` | signed 16-bit `int` |
| `uint16_t` | unsigned 16-bit `int` |
| `int32_t` | signed 32-bit `long` |
| `uint32_t` | unsigned 32-bit `long` |
| `wchar_t` | unsigned 16-bit `int` |

## Integer limits (`limits.h`)

See [Integer limits](standard-lib/04-limits.md) for the generated `limits.h`
reference.

| Macro | Value |
| --- | ---: |
| `CHAR_BIT` | 8 |
| `SCHAR_MIN` / `SCHAR_MAX` | -128 / 127 |
| `UCHAR_MAX` | 255 |
| `CHAR_MIN` / `CHAR_MAX` | -128 / 127 |
| `SHRT_MIN` / `SHRT_MAX` | -32768 / 32767 |
| `USHRT_MAX` | 65535 |
| `INT_MIN` / `INT_MAX` | -32768 / 32767 |
| `UINT_MAX` | 65535 |
| `LONG_MIN` / `LONG_MAX` | -2147483648 / 2147483647 |
| `ULONG_MAX` | 4294967295 |
| `UINT32_MAX` | 4294967295 |

## Floating limits (`float.h`)

[`float.h`](standard-lib/03-float.md) describes DCC C Compiler's single-precision reality:

| Macro | Value |
| --- | ---: |
| `FLT_RADIX` | 2 |
| `FLT_MANT_DIG` | 24 |
| `FLT_DIG` | 6 |
| `FLT_EPSILON` | 1.19209290e-07F |
| `FLT_MIN` | 1.17549435e-38F |
| `FLT_MAX` | 3.40282347e+38F |
| `FLT_MIN_EXP` / `FLT_MAX_EXP` | -125 / 128 |
| `FLT_MIN_10_EXP` / `FLT_MAX_10_EXP` | -37 / 38 |

The DCC C Compiler has no `double` or `long double`. The `DBL_*` and `LDBL_*` macros are
defined as aliases of the `FLT_*` values so source that references those names
still compiles, but they intentionally reflect the single-precision target:

- `DBL_MANT_DIG`, `DBL_DIG`, `DBL_EPSILON`, `DBL_MIN`, `DBL_MAX`,
  `DBL_MIN_EXP`, `DBL_MAX_EXP`, `DBL_MIN_10_EXP`, `DBL_MAX_10_EXP`
- `LDBL_MANT_DIG`, `LDBL_DIG`, `LDBL_EPSILON`, `LDBL_MIN`, `LDBL_MAX`,
  `LDBL_MIN_EXP`, `LDBL_MAX_EXP`, `LDBL_MIN_10_EXP`, `LDBL_MAX_10_EXP`

## Zero-initialized data

In a normal final application build, uninitialized globals and uninitialized
function-scope `static` objects are backed by the DCC C Compiler's synthetic BSS range. The
compiler emits the range as `__bssb .. __bsse`, and the runtime `start`
entrypoint zeroes that range before calling `main`. So an uninitialized global
array is guaranteed to be all zeros, as C89 requires:

```c
char buffer[4096];   /* in BSS, guaranteed zero at program start */

int main(void)
{
    return buffer[0]; /* always 0 */
}
```

  Function-scope `static` objects use the same storage model: the compiler gives
  them hidden global backing storage, then ordinary references inside the function
  refer to that backing object.

  ```c
  int next_id(void)
  {
    static int counter;     /* zero before the first call */
    return ++counter;
  }
  ```

  Separately compiled helper modules (`dcc -c` / `-module`) use ordinary `DS`
  storage for their uninitialized globals so multiple modules do not overlap the
  final application's synthetic BSS range. The zeroing guarantee above describes
  the normal final app translation unit linked with `DCCRTL.MAC` / `RTLMIN.MAC`.
