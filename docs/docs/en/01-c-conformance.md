# C conformance and target exceptions

dcc is a CP/M 2.2 / Z80 cross-compiler with a C89 language core, a first-class
`_Bool` scalar type, and selected target-appropriate C99/C11 front-end
compatibility. It aims for strong C89 source compatibility within the CP/M/Z80
target contract, not hosted desktop C conformance.

Read this page by exception:

- The baseline is ordinary C89, subject to the target-model and runtime limits
  below.
- Later C99/C11/GNU features are supported only when listed in
  [Supported post-C89 front-end features](#supported-post-c89-front-end-features).
- Anything listed in [Unsupported or target-inapplicable features](#unsupported-or-target-inapplicable-features)
  should be treated as unavailable even if a hosted desktop compiler accepts it.

## Target contract

The target contract applies to every source level. These are not temporary parser
gaps; they follow from CP/M 2.2, the Z80 data model, or the DCCRTL runtime.

| Area | dcc behavior |
| --- | --- |
| Integer and pointer model | `int`, `short`, pointers, `size_t`, and `ptrdiff_t` are 16-bit. `long` is 32-bit. |
| Boolean model | `_Bool` is a first-class 8-bit scalar type. Nonzero values normalize to `1` when stored, cast, initialized, loaded as parameters, or returned as `_Bool`. |
| Floating point | `float` is the only floating type. Unsuffixed floating constants are treated as single-precision `float`. |
| `double` / `long double` | Not supported as distinct types. Use `float`. |
| `long long` / 64-bit integers | Not supported. Use 32-bit `long` / `unsigned long`. |
| Host ABI assumptions | Do not assume ILP32/LP64/LLP64 macros, host-sized `int`, or host-width expression results. |
| Byte-stream stdio | CP/M text files use Ctrl-Z EOF semantics, and DCCRTL stdio is a subset of hosted C stdio. |
| Wide-character Unicode runtime | `wchar_t` is a 16-bit integer typedef, but Unicode/wide-character library behavior is not implemented. |
| POSIX / hosted services | No pthreads, C11 threads, POSIX process APIs, signals, locale, or time library support in the CP/M runtime. |

## Recognized keywords

dcc recognizes the C89 keyword set except for `double`. It also recognizes
`_Bool` as a first-class scalar type and a small number of later keywords as
compatibility syntax.

| Category | Keywords |
| --- | --- |
| Types | `char`, `short`, `int`, `long`, `signed`, `unsigned`, `float`, `void`, `_Bool` |
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

`inline` is accepted as a function specifier. The supported optimization form is
`static inline`; plain externally linked `inline` is parsed but emitted as a
normal externally linked function.

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

- `_Bool` is a first-class 1-byte scalar type in dcc's type system, not a macro
  or library simulation. Assignments, casts, initializers, parameter loads, and
  return values normalize nonzero values to `1`; include
  [`stdbool.h`](standard-lib/11-stdbool.md) for the portable aliases `bool`,
  `true`, and `false`.
- Forward enum declarations are accepted as `int`-sized enum types, including
  inside prototypes and function-pointer declarators.
- C11 anonymous struct/union members are accepted, including aggregate
  initialization through the anonymous member.
- GNU `__attribute__((...))` annotations are skipped in supported declaration
  positions.
- `static inline` functions are supported for small helper functions. dcc can
  inline simple return-expression bodies, early-return `if` chains lowered to
  conditional expressions, simple struct/pointer member accessors, single
  expression-statement `void` helpers such as `*dst = value`, and scalar
  `int`/pointer/`long`/`float` expression helpers. `void` helpers inline only
  when called as a statement; their assignment/store expression may contain
  ordinary helper calls, for example `*dst = clamp((long)*dst + v)`. When all
  call sites inline,
  dcc removes the private out-of-line static function body; if a call cannot be
  inlined safely, it keeps and calls the private static fallback body. Hidden
  caller-frame temporaries preserve single evaluation for multi-use 16-bit
  parameters such as `max(i++, j++)`. Multi-use `long`/`float` parameters with
  side-effecting arguments fall back to the out-of-line body. Plain `inline`
  without `static` is accepted for source compatibility but does not yet receive
  C99 external-inline linkage semantics or call-site inlining.

  ```c
  static inline int max_int(int a, int b)
  {
    return a > b ? a : b;
  }

  static inline int clamp8(int x)
  {
    if (x < 0)
      return 0;
    if (x > 255)
      return 255;
    return x;
  }

  static inline void add_int(int *dst, int v)
  {
    *dst = clamp8(*dst + v);
  }

  value = max_int(i++, limit);    /* i++ is evaluated once */
  byte = clamp8(sample + bias);   /* helper body can be removed if all calls inline */
  add_int(&total, delta);         /* statement-context helper */
  ```

### C99 aggregate initializers and compound literals

- Struct/union field designators such as `.field = value` are supported in
  global and automatic aggregate initializers.
- Array designators such as `[index] = value` are supported, including nested
  array designators in multidimensional aggregate initializers. GNU range
  designators such as `[0 ... 3] = value` are not supported.
- File-scope compound literals used in global constant initializers are
  supported, including address-taking forms such as `&(struct S){ 1, 2 }`.
- Address-taking automatic/block-scope compound literals such as
  `&(struct S){ 1, 2 }` are supported by creating hidden automatic storage for
  the enclosing function.

## Unsupported or target-inapplicable features

The table below separates target-model limits from front-end compatibility gaps.
Target-model limits are part of dcc's CP/M/Z80 contract. Front-end gaps may
become supportable later, but code should not depend on them today.

| Source level | Feature | Status |
| --- | --- | --- |
| C89 | `double` / `long double` | Target-inapplicable. dcc has single-precision `float` as its only floating type. |
| C89 hosted library | Hosted stdio, locale, signal, time, process, and wide-character runtime behavior | Runtime-inapplicable or absent in DCCRTL. |
| C99 | `long long` and 64-bit integer typedefs/operations | Target-inapplicable. The Z80 model uses 16-bit `int`/pointers and 32-bit `long`. |
| C99 | Variable-length arrays | Not implemented. Use `malloc` and an explicit pointer when runtime-sized storage is required. |
| C99 | `restrict` | Not implemented. |
| C99 | `_Complex` | Not implemented. |
| C99 | Variadic macros | Not implemented; includes `__VA_ARGS__` and empty-argument behavior. |
| C99 | Block-scope compound literal value/copy forms | Not implemented. Address-taking forms are supported as described above. |
| C99 | Flexible-array member initialization | Not implemented. |
| C11 | `_Generic` | Not implemented; some imported tests also require unsupported `long long`. |
| C11 | `_Atomic`, `_Thread_local`, C11 threads | Not implemented or runtime-inapplicable. |
| GNU/TCC extensions | Range designators | Not implemented; includes `[first ... last] = value`. |
| GNU/TCC extensions | Empty structs | Not implemented; empty `struct {}` is an extension. |
| GNU extensions | Statement expressions and branch prediction builtins | Not implemented; includes `({ ... })` and `__builtin_expect`. |

## Identifier significance

dcc exceeds C89's identifier-significance minimum of 31 characters for internal
identifiers. Externals are still constrained by the M80/L80 toolchain's symbol
handling, so keep exported names reasonably distinct near the front of the
identifier.
