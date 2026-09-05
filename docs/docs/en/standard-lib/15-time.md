# Date and time (`time.h`)

Include [`time.h`](15-time.md) for calendar types, clock access, broken-down
time conversion, and fixed-C-locale formatting.

## Types and Macros

<!-- TIME-SYMBOL-TABLE: all -->

`struct tm` contains the standard C89 fields. DCC uses 16-bit `int` members,
32-bit `time_t` and `clock_t`, and no timezone extension fields.

## Functions

<!-- TIME-FUNCTION-TABLE: all -->

## Clock and calendar model

`clock()` returns `(clock_t)-1`: CP/M 2.2 has no processor-time service.
`time()` reads BDOS function 105 when the system or emulator implements it and
otherwise returns `(time_t)-1`. The result is the BDOS wall-clock reading
encoded as a signed Unix timestamp.

This is an epoch-based encoding of the BDOS clock fields, not a guarantee that
the clock is set to UTC. There is no timezone correction. The checked upper
limit is `2038-01-19 03:14:07`; later timestamps or invalid clock fields return
`(time_t)-1`. If a non-NULL output pointer is supplied to `time`, the result,
including the failure value, is stored there too.

There is no timezone database. `localtime()` is therefore identical to
`gmtime()`, and `%Z` in `strftime()` expands to an empty string. `asctime()` and
the broken-down-time conversion functions use internal static storage. A later
`gmtime()` or `localtime()` call replaces their shared result, but `mktime()` on
an unrelated `struct tm` does not.

## Fixed-C-locale `strftime`

`strftime()` supports every C89 conversion available from `struct tm`, plus
the `%C` century extension:

| Conversion | Fixed C-locale result |
| --- | --- |
| `%a`, `%A` | abbreviated or full weekday name |
| `%b`, `%B` | abbreviated or full month name |
| `%C` | calendar century (`year / 100`), at least two digits |
| `%c` | `Www Mmm dd hh:mm:ss yyyy` (space-padded one-digit day) |
| `%d`, `%H`, `%I`, `%j`, `%m`, `%M`, `%S` | zero-padded numeric fields |
| `%p` | `AM` or `PM` |
| `%U`, `%W` | Sunday-first or Monday-first week number |
| `%w` | weekday number, Sunday is zero |
| `%x` | `mm/dd/yy` |
| `%X` | `hh:mm:ss` |
| `%y`, `%Y` | two-digit or full year |
| `%Z` | empty string |
| `%%` | literal percent sign |

The locale is always `"C"`; no locale data is linked. `tm_isdst` is ignored,
and fields are not normalized or cross-checked against each other.

`max == 0` returns zero without accessing any pointer. For a positive bound,
all pointers must be non-NULL and the range-bearing `struct tm` fields must be
valid. An unknown conversion or trailing `%` is rejected. Invalid input or an
insufficient destination returns zero and leaves the longest fitting prefix
NUL-terminated; a successful empty result also returns zero.
