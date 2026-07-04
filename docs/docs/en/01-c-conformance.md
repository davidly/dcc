# C language conformance

The DCC C Compiler is a C compiler for CP/M 2.2 on the Z80. This page describes language
support for that target, not for a hosted desktop system. Its base language is
C89, with selected C99 and C11 features added where they fit CP/M and the Z80.

Read the tables additively: start with the C89 support, then add the supported
C99 features, then add the supported C11 features.

## Target model

These limits apply at every language level.

| Feature | DCC C Compiler behavior |
| --- | --- |
| `char` | 8 bits, signed |
| `short`, `int` | 16 bits |
| pointers | 16 bits |
| `long` | 32 bits |
| `float` | 32 bits |
| `_Bool` | 8 bits, normalized to `0` or `1` |
| `double`, `long double` | not distinct types; use `float` |
| `long long` | not supported |
| hosted environment | outside the CP/M 2.2 target model |
| processes, threads, signals, locales | outside the CP/M 2.2 target model |

## Practical implications of the target model

These follow directly from the table above and are often the source of
portability surprises when code is moved from a hosted desktop compiler.

| Practical rule | What it means |
| --- | --- |
| `int` is 16-bit | Use `long` (and `%ld`) for values beyond +/-32767. Code that assumes host-sized `int` or 64-bit arithmetic is outside the target model. |
| `float` is the only floating type | Unsuffixed floating constants are treated as `float`; there is no wider `double` fallback. |
| `float` precision is ~24 bits | Integers above about +/-16,777,216 are not all exactly representable as `float`; converting large `long` values rounds to nearest representable single precision value. |
| `%` is integer-only | Use `fmodf` for floating-point remainder; `float % float` is a compile error. |

## C89 support

| Language feature | Status |
| --- | --- |
| Basic scalar types: `char`, `short`, `int`, `long`, `float`, `void` | Supported, with the target sizes above |
| Signed and unsigned integer types | Supported |
| Pointers, arrays, pointer arithmetic | Supported |
| `struct`, `union`, `enum` | Supported |
| Integer bit-fields | Supported, packed into 16-bit units |
| Function declarations and prototypes | Supported |
| Old-style function declarations and definitions | Supported |
| `typedef` | Supported |
| Storage classes: `auto`, `extern`, `register`, `static` | Supported; `auto` is a no-op and `register` is only a hint |
| Type qualifiers: `const`, `volatile` | Accepted for source compatibility; no CP/M/Z80 memory-model semantics are provided |
| Expressions and usual arithmetic conversions | Supported within the target type model |
| `if`, `switch`, loops, `break`, `continue`, `goto`, `return` | Supported |
| `sizeof` | Supported |
| Preprocessor macros and conditional inclusion | Supported |
| `__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`, `__STDC__` | Supported |
| String literals and adjacent string literal concatenation | Supported |
| Global and automatic initializers | Supported |

## Missing from C89

| Language or environment feature | Status |
| --- | --- |
| Distinct `double` and `long double` arithmetic | Not supported |
| Full hosted C library behavior | Outside the CP/M 2.2 target model |
| Locale-sensitive execution environment | Outside the CP/M 2.2 target model |
| Standard signal environment | Outside the CP/M 2.2 target model |
| Wide-character library behavior | Outside the CP/M 2.2 target model |
| Read-only storage for `const` objects | Outside the CP/M 2.2 memory model |
| Strict `volatile` memory/device access semantics | Outside the CP/M 2.2 memory model |
| Forced register allocation from `register` | Not implemented |

## C99 additions

| C99 feature | Status |
| --- | --- |
| `_Bool` | Supported as a real scalar type |
| `stdbool.h` aliases: `bool`, `true`, `false` | Supported |
| `//` comments | Supported |
| Declarations in `for` loop initializers | Supported |
| Mixed declarations and statements in nested blocks | Supported |
| Unnamed parameters in prototypes | Supported |
| Array parameters adjusted to pointers | Supported |
| C99 array parameter qualifiers: `int a[const 5]`, `int a[static 5]`, `int a[volatile 5]`, `int a[restrict 5]`, `int a[const *]` | Accepted as syntax compatibility; array parameters still decay to pointers |
| Function-typed parameters adjusted to pointers | Supported |
| `restrict` qualifier | Accepted as source compatibility; no alias-analysis optimization semantics are provided |
| Variadic macros and `__VA_ARGS__` | Supported |
| Empty variadic macro arguments | Supported |
| Designated initializers for struct and array members | Supported |
| File-scope compound literals in constant initializers | Supported |
| Address-taking block-scope compound literals | Supported |
| `inline` keyword | Accepted; plain external `inline` is emitted as an ordinary function |
| `static inline` simple helper functions | Supported for a useful subset |
| Trailing comma in enum lists | Accepted as syntax compatibility |

## Missing from C99

| C99 feature | Status |
| --- | --- |
| `long long` and 64-bit integer types | Not supported |
| Variable-length arrays | Not supported |
| `_Complex` and complex arithmetic | Not supported |
| Full C99 compound literal value semantics | Partly supported only |
| Flexible array member initialization | Not supported |
| C99 external `inline` linkage rules | Not implemented |
| Full C99 floating-point environment | Outside the CP/M 2.2 target model |
| Full C99 hosted library | Outside the CP/M 2.2 target model |

## C11 additions

| C11 feature | Status |
| --- | --- |
| Anonymous `struct` members | Supported |
| Anonymous `union` members | Supported |
| Initialization through anonymous aggregate members | Supported |

## Missing from C11

| C11 feature | Status |
| --- | --- |
| `_Generic` | Not supported |
| `_Atomic` | Outside the CP/M 2.2 target model |
| `_Thread_local` | Outside the CP/M 2.2 target model |
| C11 threads | Outside the CP/M 2.2 target model |
| C11 atomics library | Outside the CP/M 2.2 target model |
| C11 bounds-checking interfaces | Outside the CP/M 2.2 target model |
| Hosted C11 library additions | Outside the CP/M 2.2 target model |

## Extensions accepted for compatibility

| Extension | Status |
| --- | --- |
| `__attribute__((...))` in common declaration positions | Ignored |

## Extensions not supported

| Extension | Status |
| --- | --- |
| GNU range designators, such as `[0 ... 3]` | Not supported |
| Empty structures | Not supported |
| Statement expressions, such as `({ ... })` | Not supported |
| `__builtin_expect` | Not supported |
