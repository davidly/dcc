# C conformance and target exceptions

dcc is a CP/M 2.2 / Z80 cross-compiler with a C89 core and a growing set of
target-appropriate C99/C11 front-end features. It is not a hosted desktop C
implementation: the Z80 data model, CP/M file semantics, and DCCRTL runtime are
part of the language contract.

Use this rule of thumb when porting code:

- Ordinary C89 should compile unless it depends on a hosted runtime function not
  present in DCCRTL.
- C99/C11 front-end features listed below are supported when they fit the 16-bit
  CP/M target.
- Host ABI assumptions do not apply. `int`, pointers, `size_t`, and `ptrdiff_t`
  are 16-bit; `long` is 32-bit; there is no 64-bit integer type and no 8-byte
  floating type.

## Recognized keywords

dcc recognizes the C89 keyword set except for `double`. It also recognizes a
small number of later keywords as compatibility syntax.

| Category | Keywords |
| --- | --- |
| Types | `char`, `short`, `int`, `long`, `signed`, `unsigned`, `float`, `void` |
| Storage class | `auto`, `extern`, `register`, `static`, `typedef` |
| Type qualifiers | `const`, `volatile` |
| Aggregates / enums | `struct`, `union`, `enum` |
| Control flow | `if`, `else`, `switch`, `case`, `default`, `for`, `while`, `do`, `break`, `continue`, `goto`, `return` |
| Operators | `sizeof` |
| Accepted compatibility keywords | `inline` |

### Keyword exceptions

| Keyword | Status |
| --- | --- |
| `double` / `long double` | Not supported. dcc has 32-bit `float` as its only floating type. |
| `long long` | Not supported. dcc has no 64-bit integer type. |
| `restrict` | Not implemented. |
| `_Bool` | Not a compiler keyword. Include [`stdbool.h`](standard-lib/11-stdbool.md) for `bool`, `true`, and `false` as ordinary library definitions. |
| `_Complex` | Not implemented. |
| `_Atomic`, `_Generic`, `_Thread_local` | Not implemented. |

## Accepted-but-inert qualifiers

A few keywords are parsed so source compiles, but they do not change code
generation:

- `const` - honored only for constant folding of const-initialized variables. It
  does not place data in read-only memory.
- `volatile` - accepted but otherwise ignored.
- `register` - accepted as a hint only; it does not force register allocation.
- `auto` - accepted; since it is already the default storage for locals, it is a
  no-op.
- `inline` - accepted and ignored; functions are emitted normally.

## Supported post-C89 front-end features

These features are supported because they are useful for modern C source and fit
the Z80/CP/M target model.

### C99 comments and declarations

- `//` line comments are accepted everywhere C89 block comments are accepted,
  including trailing comments on preprocessor directives.
- `for`-loop init declarations are supported with C99 loop scope.
- Block-local declarations may appear inside nested `{ ... }` blocks and shadow
  outer locals for that block.

```c
int i = 99;
for (int i = 0; i < 3; i++)
    use(i);                 /* 0, 1, 2 */
/* outer i is still 99 here */
```

### C99/C11 declaration compatibility

- Forward enum declarations are accepted as `int`-sized enum types, including
  inside prototypes and function-pointer declarators.
- C11 anonymous struct/union members are accepted, including aggregate
  initialization through the anonymous member.
- GNU `__attribute__((...))` annotations are skipped in supported declaration
  positions.

### Variable-length arrays

Automatic one-dimensional VLAs with a simple identifier bound are supported:

```c
void f(int n)
{
    char buf[n];
    buf[0] = 0;
}
```

dcc reserves the storage on the C stack at runtime and restores the stack from
the function frame on return. Keep VLAs small: the CP/M transient program area
is shared by code, data, heap, and stack.

## Not implemented yet

The following are front-end compatibility candidates, not inherent CP/M/Z80
model conflicts. Code using them may become supportable in future dcc versions,
but they are not implemented today.

| Feature | Notes |
| --- | --- |
| C99 designated initializers | Includes `.field = value` initializers. |
| C99 array designators | Includes `[index] = value`, including nested designators. |
| C99 compound literals | Includes address-taking forms such as `&(struct S){...}`. |
| C99 variadic macros | Includes `__VA_ARGS__` and related empty-argument behavior. |
| C11 `_Generic` | Not implemented; some tests also require `long long`, which remains target-inapplicable. |
| GNU statement expressions | Includes `({ ... })`; `__builtin_expect` is also not recognized. |

## Target-model and runtime exceptions

These are not just missing parser features. They are consequences of the CP/M
2.2 / Z80 target, the dcc data model, or the DCCRTL runtime.

| Area | dcc behavior |
| --- | --- |
| `double` / `long double` | Not supported. Unsuffixed floating constants are treated as single-precision `float`. |
| `long long` / 64-bit integers | Not supported. Use 32-bit `long` / `unsigned long`. |
| Host ABI assumptions | Do not assume ILP32/LP64/LLP64 macros or host-sized `int`; dcc has 16-bit `int` and 16-bit pointers. |
| Host-sized integer expectations | Expressions that require host-width `int` produce 16-bit target results unless explicitly promoted to `long`. |
| Byte-stream stdio | CP/M text files use Ctrl-Z EOF semantics, and DCCRTL stdio is a subset of hosted C stdio. |
| Wide-character Unicode runtime | `wchar_t` is a 16-bit integer typedef, but Unicode/wide-character library behavior is not implemented. |
| POSIX / hosted services | No pthreads, C11 threads, POSIX process APIs, signals, locale, or time library support in the CP/M runtime. |

## Identifier significance

dcc exceeds C89's identifier-significance minimum of 31 characters for internal
identifiers. Externals are still constrained by the M80/L80 toolchain's symbol
handling, so keep exported names reasonably distinct near the front of the
identifier.
