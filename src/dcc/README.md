# dcc — modularised compiler source

`dcc` is a compact C compiler with a C89 base language plus selected C99 and
C11 features that fit the CP/M/Z80 target. It emits Z80 assembly for the
M80-compatible CP/M toolchain. The compiler implementation is portable C11 host
code built with modern Clang, GCC, or MSVC. This directory holds the
**modularised** form of the compiler: the original ~18.8k-line single file was
split into focused, separately compiled modules — one `.c` per subsystem plus a
shared umbrella header — so that both human and agentic developers can navigate
and modify one subsystem at a time.

> The original monolith is preserved at [`../ddc.c`](../ddc.c) as a reference
> snapshot. The repository-root [`../../dcc.c`](../../dcc.c) is an older
> standalone copy and is **not** part of this build.

---

## How the modular build is structured

`dcc` now lowers function bodies through a **function-local AST**. The parser
builds typed statement/expression trees for function bodies, then the AST
walker emits Z80 assembly by calling shared low-level emit helpers. The compiler
still shares foundational types/state through `dcc.h`; focused AST,
preprocessor, and register-allocation contracts use internal headers.

- [`dcc.h`](dcc.h) is the umbrella header. It declares everything shared:
  capacity macros, the type/storage/token constants, the core record types
  (`Token`, `Sym`, `Def`, `AsmName`, `TypeDef`, `FieldDef`, `StructDef`,
  `ConstVal`, `ByteOperand`), `extern` declarations for the shared globals, and
  the prototypes for every cross-module function (grouped by owning module).
- Each subsystem is a normal `.c` translation unit that starts with
  `#include "dcc.h"`. They are compiled separately and linked together.
- [`dcc_state.c`](dcc_state.c) **defines** the shared globals once; every other
  module reaches them through the `extern` declarations in [`dcc.h`](dcc.h).
- [`dcc.c`](dcc.c) is the driver translation unit and contains `main()`.

This is the traditional `.c` / `.h` layout. The split itself was
behaviour-preserving: at the time of the split the modular compiler generated
**byte-for-byte identical** assembly to the monolith, verified by the project's
regression suite (see *Verifying* below).

> The modular tree has since gained **correctness fixes that the monolith
> snapshot does not have** (notably the AST-driven expression and statement
> lowering work). Those changes intentionally diverge from
> [`../ddc.c`](../ddc.c); the regression baseline
> ([`../../baseline_test_dcc.txt`](../../baseline_test_dcc.txt)) now tracks the
> modular compiler itself, and the byte-identical guarantee is against that
> baseline, not against the monolith. The suite still compares program output,
> so a fix that only makes a previously-miscompiled program correct keeps every
> other app's output unchanged.

### Why there is one shared header instead of many

The parser and code generator genuinely share most of their data: the source
buffer, the lookahead token, the symbol/typedef/struct tables, and a set of
per-function codegen flags. Splitting those into per-module headers would just
produce a web of headers that all include each other. A single umbrella header
keeps the shared contract in one place and the modules free of cross-include
ordering puzzles.

### State ownership

Most mutable state is shared and therefore defined in [`dcc_state.c`](dcc_state.c)
and declared `extern` in [`dcc.h`](dcc.h). A small amount of state is private to
a single module and kept `static` there:

- `pp_expr_p` / `pp_expr_depth` → [`dcc_preproc.c`](dcc_preproc.c) (the `#if`
  expression cursor)
- `include_dirs` / `num_include_dirs` → [`dcc.c`](dcc.c) (the include search
  path)

### Function-local AST and MIR lowering

[`dcc_ast_build.c`](dcc_ast_build.c) parses function statements into
[`dcc_ast.h`](dcc_ast.h) nodes. MIR lowers each supported statement directly.
[`dcc_ast_metadata.c`](dcc_ast_metadata.c) and
[`dcc_ast_stmt_meta.c`](dcc_ast_stmt_meta.c) replay only parser metadata:
declarations/initializers, scopes and VLA exits, inline-temp types, string-pool
order, labels, diagnostics, and debug events. No AST function-body Z80 emitter
or discard stream remains.

Two stderr-only debugging knobs are available: `DCC_AST_REPORT=1` logs the
`; AST-unsupported ...` statement/initializer that a support gate declined (it
prints just before the `unsupported AST statement` fatal), and `DCC_AST_BUILD=2`
dumps each built AST tree before it is lowered. Neither affects codegen.

Local declarations remain captured lexer spans, but explicit scan/replay APIs
now own their frame and initializer side effects. Production function assembly
comes only from a selected generated MIR candidate.

---

## Module architecture

```mermaid
graph TB
    subgraph SHARED["Shared contract — used by every module"]
        H["dcc.h<br/>macros · structs · externs · prototypes"]
        STATE["dcc_state.c<br/>definitions of the shared globals"]
        STATE -->|defines what dcc.h declares| H
    end

    subgraph FE["1 · Front end"]
        DRV["dcc.c<br/>driver · #include · CLI · main()"]
        PP["dcc_preproc.c<br/>preprocessor · macros · lexer"]
        DIAG["dcc_diag_emit.c<br/>diagnostics · alloc · emit"]
        ASM["dcc_asmname.c<br/>C name → asm symbol"]
    end

    subgraph TYP["2 · Types · symbols · constants"]
        TYPES["dcc_types.c<br/>type system · struct/typedef"]
        SYM["dcc_symbols.c<br/>symbol tables · access codegen"]
        CONST["dcc_constexpr.c<br/>integer const-expr parser"]
        FOLD["dcc_fold.c<br/>constant folding · sizeof/offsetof"]
    end

      subgraph AST["3 · Function-local AST"]
        ASTN["dcc_ast.c / dcc_ast.h<br/>arena · nodes"]
        ASTB["dcc_ast_build.c<br/>AST builder"]
        ASTM["dcc_ast_metadata.c / dcc_ast_stmt_meta.c<br/>non-emitting metadata"]
        ASTG["dcc_ast_gen*.c<br/>support + initializer compatibility"]
      end

      subgraph CG["4 · Code generation helpers"]
        EXPR["dcc_expr.c<br/>expressions · unary · calls"]
        OPS["dcc_ops.c<br/>arithmetic · bitwise · shifts"]
        CMP["dcc_cmp.c<br/>compare · branch"]
        ASSIGN["dcc_assign.c<br/>assignment · float"]
        STMT["dcc_stmt.c<br/>compound parser · MIR dispatch"]
        DECL["dcc_decl.c<br/>local decls · initializers"]
        SFAST["dcc_stmt_fast.c<br/>statement fast paths"]
    end

    subgraph TOP["5 · Top level & output"]
        FUNC["dcc_func.c<br/>functions · top-level parse"]
        DATA["dcc_data.c<br/>data-section emission"]
    end

    SHARED -. included by all .-> FE
    FE ==> TYP ==> AST ==> CG ==> TOP
```

*Reading the diagram:* the **Shared contract** (top) is `#include`d by every
module. The thick arrows are the dominant translation pipeline — front end →
types/symbols → code generation → top level & output. Within a stage the files
are peers; the per-file call relationships are summarised in the runtime flow
below.

### Compilation pipeline (runtime flow)

```mermaid
flowchart LR
    SRC([".c source"]) --> DRV["dcc.c<br/>read + #include splice"]
    DRV --> PPF["dcc_preproc.c<br/>#if filter + macro expand"]
    PPF --> LEX["dcc_preproc.c<br/>next_token · lexer"]
    LEX --> PARSE["dcc_func · dcc_stmt<br/>parse function bodies"]
    PARSE --> AST["dcc_ast_build.c<br/>build function-local AST"]
    AST --> EMIT["dcc_ast_gen*.c<br/>emit from AST"]
    EMIT --> DATA["dcc_data.c<br/>emit data section"]
    DATA --> OUT([".mac assembly"])
```

Calls flow roughly front-to-back, but because every module shares `dcc.h` any
module may call any other module's functions (the prototypes are all visible).
The arrows above show the dominant direction, not a hard layering restriction.

---

## The modules

| File | Responsibility |
| --- | --- |
| [`dcc.h`](dcc.h) | Umbrella header included by every module: capacity macros (`MAX_*`), type/storage/token constants (`TYPE_*`, `SC_*`, `TOK_*`), the nine core record types, `extern` declarations of the shared globals, and grouped prototypes for every cross-module function. |
| [`dcc_state.c`](dcc_state.c) | Definitions of the shared globals declared `extern` in `dcc.h`: source buffer + lexer position + lookahead token, symbol/typedef/struct/field tables, the macro table, the `#if` stack, the string pool, per-function codegen flags, and parser scratch state. |
| [`dcc_asmname.c`](dcc_asmname.c) | Maps each C identifier to its emitted M80 assembler symbol: when to mangle (M80's 6-significant-character publics, reserved words), recognises fixed runtime-library entry points, and caches results in `asm_names[]`. |
| [`dcc_diag_emit.c`](dcc_diag_emit.c) | Plumbing: `fatal`/`error_here` diagnostics, `source_location_at` (`#line`-aware), `xmalloc`/`xstrdup2`, label allocation, the `emit*` assembly-output primitives, and the raw source readers `peekc`/`getc_src`. |
| [`dcc_preproc.c`](dcc_preproc.c) | Preprocessor + lexer: `#define`/`#undef`/`#if`/`#ifdef`, object- and function-like macro expansion (`#` stringize, `##` paste), the `#if` constant-expression evaluator, and the main tokenizer `next_token`. |
| [`dcc_types.c`](dcc_types.c) | Type system: base-type and declarator parsing, struct/union and typedef tables, bitfield layout, type sizing/promotion/arithmetic helpers, and enum-constant lookup. |
| [`dcc_constexpr.c`](dcc_constexpr.c) | Context-specific wrappers around typed `ConstVal` evaluation and C11 `_Static_assert` declaration parsing. |
| [`dcc_symbols.c`](dcc_symbols.c) | Symbol tables (locals, parameters, globals), the string-literal pool, EXTRN bookkeeping, and code that loads/stores a symbol's address or value, including post-increment/decrement fast paths. |
| [`dcc_fold.c`](dcc_fold.c) | The `cf_*` constant-folding engine (with C type/promotion rules), `sizeof`/`offsetof` evaluation, and emission of folded constant results. |
| [`dcc_ast.h`](dcc_ast.h), [`dcc_ast.c`](dcc_ast.c) | Function-local AST node definitions, list helpers, arena allocation, and debug dumping. |
| [`dcc_ast_build.c`](dcc_ast_build.c) | AST builder for expressions and statements, including declaration-span capture for local declarations. |
| [`dcc_ast_gen.c`](dcc_ast_gen.c), [`dcc_ast_gen_support.c`](dcc_ast_gen_support.c), [`dcc_ast_gen_expr.c`](dcc_ast_gen_expr.c), [`dcc_ast_gen_cond.c`](dcc_ast_gen_cond.c), [`dcc_ast_gen_internal.h`](dcc_ast_gen_internal.h) | Shared AST type/support classifiers and local-initializer compatibility helpers. Function-body statement emission has been removed. |
| [`dcc_ast_metadata.c`](dcc_ast_metadata.c), [`dcc_ast_stmt_meta.c`](dcc_ast_stmt_meta.c) | Non-emitting declaration, scope/VLA, inline, string, label, diagnostic, debug, and frame-sizing metadata walks. |
| [`dcc_expr.c`](dcc_expr.c) | Shared low-level expression helpers for the AST emitter: load/store through HL, struct copies, casts and conversions, bitfield extract/insert, pre/post increment-decrement, call cleanup, and declaration-side parsing helpers. |
| [`dcc_cmp.c`](dcc_cmp.c) | Relational/equality comparison codegen (signed/unsigned, 16- and 32-bit) and condition-to-branch lowering, including single-`cp` byte-operand comparators. |
| [`dcc_ops.c`](dcc_ops.c) | Binary-operator/arithmetic helpers for `+ - * / %`, shifts, bitwise ops, 16/32-bit and unsigned variants, integer promotion, pointer element-size scaling, float comparisons, and nonzero tests. |
| [`dcc_assign.c`](dcc_assign.c) | Shared assignment/float primitives for the AST emitter: materialising float constants and computing global byte-array element addresses. |
| [`dcc_stmt_fast.c`](dcc_stmt_fast.c) | In-place increment/decrement helper for lvalue addresses already in HL, covering byte, 16-bit, and 32-bit operands. |
| [`dcc_decl.c`](dcc_decl.c) | Local declaration and initializer codegen: scalars, arrays, structs/unions, bitfields, brace initializer lists, and const-scalar folding of local initializers. |
| [`dcc_stmt.c`](dcc_stmt.c) | Compound-block parser and statement-to-MIR/metadata dispatcher. |
| [`dcc_func.c`](dcc_func.c) | Function and top-level declaration parsing, frame sizing, MIR lifecycle, typedef declarations, and file-scope object parsing/emission. |
| [`dcc_data.c`](dcc_data.c) | Data-section emission: the string-literal pool and global object storage with initializers, rendered as `DEFB`/`DEFW`. |
| [`dcc.c`](dcc.c) | Driver and entry point: input file I/O, `#include` resolution and line-directive splicing, the active-source filtering pass, command-line option parsing, and `main()`. |

---

## Building

From the repository root:

```sh
# Option A: the build script (writes ./dcc at the repo root)
sh src/dcc/build-dcc.sh

# Option B: CMake
cmake -S src/dcc -B build/dcc
cmake --build build/dcc      # also writes ./dcc at the repo root
```

Both compile every module (`dcc.c`, `dcc_state.c`, and the `dcc_*.c` files) as
portable C11 and link them into `./dcc` at the repository root,
matching the conventions the `ma.sh` / `runall.sh` harness expects. The
companion tools `dccpeep` and `dccrtlstrip` are unchanged and are built by the
existing root scripts (`mmacos.sh` / `m.sh`).

Override the compiler or flags via environment variables:

```sh
CC=gcc CFLAGS="-std=c11 -O2" sh src/dcc/build-dcc.sh
```

> Note: linking may print `ld: warning: reducing alignment of section
> __DATA,__common ...`. That is benign — it reflects the compiler's large
> static tables and does not affect correctness.

---

## Verifying

The modular build must produce assembly identical to the monolith. To check a
single program:

```sh
./dcc -f sieve.c -o /tmp/sieve.mac
```

To run the full C89 regression suite (requires the `ntvcm` emulator on `PATH`):

```sh
export PATH="/path/to/ntvcm:$PWD:$PATH"
export DCC=./dcc DCCPEEP=./dccpeep DCCRTLSTRIP=./dccrtlstrip
sh ./runall.sh ntvcm
```

The suite writes `test_dcc.txt` (peephole-optimised) and `test_dccu.txt`
(unoptimised) and diffs them against `baseline_test_dcc.txt`. A non-zero exit
caused **only** by the `__DATE__` / `__TIME__` lines (the compile wall-clock
from the `tstdc` test) is expected and counts as green:

```sh
diff baseline_test_dcc.txt test_dcc.txt \
  | grep -vE '__DATE__|__TIME__|^[0-9]+(,[0-9]+)?c[0-9]+|^---$'
# empty output == GREEN
```

---

## Working in this codebase

- **Add or change behaviour** inside the relevant `dcc_*.c` module. Keep the
  change in the module that owns that responsibility.
- **Adding a new function that other modules call?** Define it in its module
  and add a prototype to the matching group in [`dcc.h`](dcc.h). Functions used
  only within one module can stay `static` and need no prototype in `dcc.h`.
- **Need a new shared constant or record type?** Add it to [`dcc.h`](dcc.h).
- **Need new shared state across modules?** Define it in [`dcc_state.c`](dcc_state.c)
  and add an `extern` declaration to [`dcc.h`](dcc.h). If it is used by only one
  module, prefer a `static` at the top of that module instead.
- **After any change**, rebuild and run the regression suite. For pure
  refactors, the filtered diff must stay empty.
- **Reaching for an operand's type before it is lowered?** Carry it on the
  AST node and lower through MIR/shared AST support. Avoid
  adding new shallow source-text peeks; the AST is the source of truth for typed
  expressions.
