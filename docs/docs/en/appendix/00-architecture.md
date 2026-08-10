# Appendix: compiler architecture

This appendix describes the DCC C Compiler toolchain: the compiler `dcc`, the peephole
optimizer `dccpeep`, the runtime size reducer `dccrtlstrip`, and the assembler
and linker - either the host-native `m80c`/`l80c`, or the Microsoft `M80`/`L80`
originals running under `ntvcm`.

!!! note "Host-resident tools"
  `dcc`, `dccpeep`, `dccrtlstrip`, `m80c`, and `l80c` run on Windows, macOS,
  and Linux. They never run on a Z80. They emit Z80 assembly text and CP/M
  `.COM` files that run under CP/M-80, for example via the `ntvcm` emulator.
  `m80c`/`l80c` are clean-room, LINK-80-object-format-compatible
  reimplementations of `M80`/`L80` with unbounded host memory instead of
  CP/M's own 64K tables - large `nopeep` builds can exhaust real `L80`'s own
  in-emulator linking workspace well before the target program itself would
  not fit; `l80c` has no such ceiling. They are the default; pass
  `dcc-use-emulated-m80=true`/`dcc-use-emulated-l80=true` to `dccmake` (or
  `--emulated-m80`/`--emulated-l80` to `ma.sh`/`ma.ps1`) to use the real
  `M80.COM`/`L80.COM` under `ntvcm` instead, e.g. to cross-check output.

  The compiler implementation is portable C11 host code built by modern Clang,
  GCC, or MSVC. That implementation language is independent of `dcc`'s C89
  base language and selected C99/C11 additions for the 16-bit Z80 target.

## The toolchain at a glance

A single `.c` file becomes a CP/M `.COM` executable through a short pipeline.
Each stage has one job and hands a text or object file to the next:

```mermaid
flowchart LR
    SRC([".c source"]) --> DCC["dcc<br/>C89 -> Z80 asm"]
    DCC --> MAC([".MAC assembly"])
    MAC --> PEEP["dccpeep<br/>peephole optimizer"]
    PEEP --> MAC2([".MAC optimized"])
    MAC2 --> M80A["m80c / M80<br/>assemble"]
    RTL([" DCCRTL.MAC<br/>full runtime"]) --> STRIP["dccrtlstrip<br/>dead-block removal"]
    MAC2 -. references .-> STRIP
    STRIP --> RTLMIN([" RTLMIN.MAC<br/>used routines only"])
    RTLMIN --> M80B["m80c / M80<br/>assemble"]
    M80A --> REL([" app.REL"])
    M80B --> RRTL([" RTLMIN.REL"])
    REL --> L80["l80c / L80<br/>link"]
    RRTL --> L80
    L80 --> COM([".COM executable"])
```

| Stage | Tool | Input | Output | Role |
| --- | --- | --- | --- | --- |
| Compile | `dcc` | `.c` | `.MAC` | Translate C89 plus documented selected C99/C11 features to Z80/M80 assembly |
| Optimize | `dccpeep` | `.MAC` | `.MAC` | Local peephole rewriting of the asm |
| Reduce runtime | `dccrtlstrip` | `DCCRTL.MAC` + app `.MAC` | `RTLMIN.MAC` | Keep only the runtime routines the app references |
| Assemble | `m80c` or `M80` | `.MAC` | `.REL` | Object code (relocatable); `dccmake` uses native `m80c` by default |
| Link | `l80c` or `L80` | `.REL` files | `.COM` | Resolve symbols into a CP/M executable; `dccmake` uses native `l80c` by default |

The `dccpeep` stage is optional (`./scripts/ma.ps1 name -Mode nopeep` skips it
when run from PowerShell in the DCC C Compiler checkout). `dccrtlstrip` runs against the
*final* application assembly so it
sees the real set of runtime symbols the program calls.

## Compiler Shape

DCC C Compiler's implementation is AST-driven for function bodies: statements and
expressions are parsed into typed AST nodes, and the AST walker emits the Z80
assembly. Code generation is a **single AST path** — every expression and
statement, including local-declaration initializers, is lowered through the AST
emitter (initializers build into an isolated arena so they never disturb the
surrounding statement walk). The AST is "function-local" only in scope:
top-level declarations, the preprocessor, and the global type/symbol tables
remain direct table-driven front-end machinery rather than AST nodes.

```mermaid
flowchart LR
    SRC([".c source"]) --> PP["preprocess +<br/>#include splice"]
    PPX["dcc_pp_expr.c<br/>#if / #elif expressions"] -. conditional evaluation .-> PP
    PP --> LEX["lexer<br/>(next_token)"]
    LEX --> BUILD["dcc_ast_build.c<br/>build function-local AST"]
    BUILD --> GEN["dcc_ast_gen*.c<br/>emit from AST"]
    GEN --> ASM([".MAC assembly"])
```

The phases are:

| Classic phase | Conventional design | DCC C Compiler approach |
| --- | --- | --- |
| Lexical analysis | Separate tokenizer | `next_token` lexer in `dcc_preproc.c` (integrated with the preprocessor) |
| Parsing | Build an AST | Recursive-descent parse into a function-local AST |
| Semantic analysis | Walk the AST, annotate types | Done during AST construction against live symbol/type tables |
| Intermediate representation | One or more IRs (e.g. three-address code, SSA) | **None** — C maps straight to Z80 |
| Machine-independent optimization | Passes over the IR | Mostly absent by design; some peephole/idiom fast paths in codegen |
| Code generation | Lower IR to target | AST walker emits Z80/M80 assembly through shared emit helpers |
| Machine-dependent optimization | Target peephole pass | Separate program `dccpeep` over the emitted text |

### Typed expression lowering

The AST carries expression result types, so codegen can choose 16-bit, 32-bit,
pointer, struct, or float lowering from the tree it is emitting — the full
typed operand is always in hand before any code is emitted.

### State ownership and speculative generation

The compiler remains a single-process, single-translation-unit-at-a-time tool,
but related mutable state is grouped by lifecycle rather than exposed as loose
globals:

- `LexState` holds the live token/cursor fields and is copied by
  `lex_save()` / `lex_restore()` for parser lookahead.
- `FrameState` owns local-count, frame-size, and parameter-offset state.
- `ExprState` describes the value most recently left in registers.
- `FunctionPassState` owns counters restarted for function scan/codegen passes.
- `DeclState` owns the storage-class and qualifier flags for the declaration
  currently being parsed.

Some optimizations generate a function or loop into a temporary stream, verify
the emitted assembly, then either commit it or rewind parser/frame state and use
the ordinary fallback. `EmitSink` names the destination role (`FINAL`,
`DISCARD`, `VERIFY`, or `DEFERRED`) and scoped push/restore operations make
nested redirection explicit. Sink role does **not** imply suppression:
`scan_mode` remains separate, and verification streams may require raw formatted
writes so their text can be read back before the commit/decline decision.

Proof-based optimizations such as byte-array narrowing are deliberately
conservative. Unknown shapes, recursive captured calls, or exhausted proof
limits decline the optimization and retain ordinary 16-bit codegen.

### Refactor validation

For behavior-preserving parser/codegen changes, runtime output alone is not the
strongest oracle. The project builds before/after compilers and requires
byte-identical `.MAC` output across the application corpus plus identical stderr
across the compile-fail diagnostic corpus, then runs the CP/M regression and
performance suite. This catches label, speculative-pass, and instruction-shape
changes that may not alter the current runtime baselines.

## Compiler Features

The DCC C Compiler is intentionally small, but the compiler front end still provides a
user-facing feature set around diagnostics, C89/C99 compatibility extensions,
target-model checks, and size-oriented dead-code elimination in the generated
program.

### Error messages by category

Compiler diagnostics are emitted through `dcc_diag_emit.c`. Each diagnostic is
assigned a stable `DCC-E####` code, reported with the source file and line, and
prints the original source line with a caret when the compiler has an exact
token position. The compile-fail diagnostic tests under `tests/diagnostics/`
lock these messages and carets against exact baselines.

| Category | Code range | Examples |
| --- | --- | --- |
| Name lookup | `DCC-E0201` | undeclared identifiers |
| Preprocessor and include handling | `DCC-E0301`-`DCC-E0321` | malformed `#include`, unknown directives, unmatched `#elif`/`#else`/`#endif`, bad macro arity |
| Constant expressions | `DCC-E0401`-`DCC-E0403` | non-constant expressions, division by zero, missing expression operands |
| Struct/union/enum/type semantics | `DCC-E0501`-`DCC-E0540` | field designators, `offsetof`, bit-fields, duplicate enum constants, missing type names, multiple storage classes |
| Array and pointer constraints | `DCC-E0601`-`DCC-E0605` | unsupported variable inner dimensions, invalid bounds/object sizes, scalar subscripting |
| Statement control flow | `DCC-E0701`-`DCC-E0706` | `break`/`continue` outside valid contexts, stray `case`/`default`, duplicate or undefined labels |
| Functions and declarations | `DCC-E0801`-`DCC-E0806` | parameter declaration errors, redefinitions, too few or too many function-call arguments |
| Initializers and assignment compatibility | `DCC-E0901`-`DCC-E0920` | invalid address initializers, non-constant initializers, string/array/struct initializer errors, integer-to-pointer assignment |
| Unsupported or malformed constructs | `DCC-E1001`-`DCC-E1005` | unsupported AST forms, malformed syntax, oversized string literals, unsupported `sizeof` expressions |
| General syntax and top-level parsing | `DCC-E1101`-`DCC-E1107` | expected tokens such as `;`, `)`, `]`, `=`, or an external declaration |
| CP/M/Z80 target-model limits | `DCC-E1201`-`DCC-E1203` | unsupported `double`, `long long`, and 64-bit integer typedef names |

The categories are deliberately broad: the numeric code says where the problem
was detected, while the message text names the exact source-level issue.

### Dead code detection and elimination

The DCC C Compiler does not run a whole-program control-flow analysis pass, and it does not
warn about arbitrary unreachable user statements. Instead, dead-code handling is
pragmatic and size-focused at the points where the toolchain has reliable local
knowledge:

- **Dead expression results.** During AST lowering, expression statements and
  condition-only contexts set internal "result is dead" state so the emitter can
  choose forms that perform side effects without preserving an unused value.
- **Dead stores and labels in assembly.** `dccpeep` runs to a fixpoint over the
  emitted `.MAC` text. Among its cleanups are jump/label threading, jump-to-next
  removal, redundant load/store removal, and conservative dead IX-frame store
  elimination when a stack slot is overwritten before it can be read.
- **Dead static inline bodies.** Simple `static inline` functions can be
  buffered and emitted only when a real out-of-line body is needed, avoiding
  unused helper text in the generated assembly.
- **Dead runtime blocks.** `dccrtlstrip` performs conservative mark-and-sweep
  dead-block elimination over `DCCRTL.MAC`: it roots symbols referenced by the
  app, follows runtime-to-runtime references to a fixpoint, and writes
  `RTLMIN.MAC` containing only reachable runtime blocks.

This split keeps the compiler simple while still attacking the biggest sources
of wasted code: unused expression values, local assembly redundancies, unused
inline bodies, and unreferenced runtime support.

## Inside DCC C Compiler: module architecture

The compiler is one binary built from focused modules. Foundational target
types, shared data structures, and broadly used APIs live in `dcc.h`; narrower
contracts live in `dcc_ast_gen_internal.h`, `dcc_preproc_internal.h`, and
`dcc_regalloc_internal.h`. Shared mutable state is defined once in
`dcc_state.c`, with related fields grouped into the lifecycle structures
described above.

```mermaid
graph TB
    subgraph SHARED["Shared contracts and state"]
      H["dcc.h<br/>foundational types + broad API"]
      IH["*_internal.h<br/>focused subsystem contracts"]
      STATE["dcc_state.c<br/>shared state definitions"]
    end

    subgraph FE["1 - Front end"]
        DRV["dcc.c<br/>driver, CLI, main()"]
      PP["dcc_preproc.c<br/>macro engine + lexer"]
      PPX["dcc_pp_expr.c<br/>#if expression evaluator"]
        DIAG["dcc_diag_emit.c<br/>diagnostics + emit"]
        ASM["dcc_asmname.c<br/>C name -> asm symbol"]
    end

    subgraph TYP["2 - Types, symbols, constants"]
        TYPES["dcc_types.c"]
        SYM["dcc_symbols.c"]
        CONST["dcc_constexpr.c"]
        FOLD["dcc_fold.c"]
    end

    subgraph AST["3 - Function-local AST"]
      ASTN["dcc_ast.c / dcc_ast.h"]
      ASTB["dcc_ast_build.c"]
      ASTG["dcc_ast_gen*.c<br/>(5 TUs)"]
    end

    subgraph CG["4 - Code generation helpers"]
        EXPR["dcc_expr.c<br/>expressions, calls"]
        OPS["dcc_ops.c<br/>arithmetic, bitwise"]
        CMP["dcc_cmp.c<br/>compare, branch"]
        ASSIGN["dcc_assign.c"]
        STMT["dcc_stmt.c<br/>compound + switch helpers"]
        DECL["dcc_decl.c<br/>local decls, initializers"]
        NARROW["dcc_array_narrow.c<br/>byte-narrowing proof"]
    end

      subgraph TOP["5 - Top level, speculation + output"]
        FUNC["dcc_func.c<br/>functions, frame layout"]
        GINIT["dcc_global_init.c<br/>file-scope initializers"]
        REG["dcc_regalloc.c<br/>whole-function speculation"]
        LREG["dcc_loop_regalloc.c<br/>loop-scoped BC allocation"]
        DATA["dcc_data.c<br/>data-section emission"]
    end

      SHARED -.contracts.-> FE
    FE ==> TYP ==> AST ==> CG ==> TOP
```

The thick arrows are the dominant translation pipeline (front end → types →
    code generation → output). Within a stage the files cooperate through shared
    types and focused internal contracts; the arrows show the usual direction, not
    a hard layering rule.

| Group | Modules | Responsibility |
| --- | --- | --- |
| Shared | `dcc.h`, `dcc_state.c`, `dcc_ast_gen_internal.h`, `dcc_preproc_internal.h`, `dcc_regalloc_internal.h` | Foundational contract, focused internal contracts, and shared state definitions |
| Front end | `dcc.c`, `dcc_preproc.c`, `dcc_pp_expr.c`, `dcc_diag_emit.c`, `dcc_asmname.c` | Driver/CLI, macro engine + lexer, conditional-expression evaluation, diagnostics + emit primitives, C-name-to-asm-symbol mapping |
| Types / symbols | `dcc_types.c`, `dcc_symbols.c`, `dcc_constexpr.c`, `dcc_fold.c` | Type system, symbol tables, constant-expression evaluation, constant folding |
| Function-local AST | `dcc_ast.h`, `dcc_ast.c`, `dcc_ast_build.c`, `dcc_ast_gen.c` + `dcc_ast_gen_support.c` / `_expr.c` / `_cond.c` / `_stmt.c` (behind `dcc_ast_gen_internal.h`) | AST node storage, typed statement/expression building, and the AST-driven Z80 emitter — split into classifiers/type resolvers (`dcc_ast_gen.c`), the `ast_gen_supported` dispatch and folds (`_support.c`), expression emitters (`_expr.c`), condition/branch emitters (`_cond.c`), and switch/for/statement emitters (`_stmt.c`) |
| Code generation helpers | `dcc_expr.c`, `dcc_ops.c`, `dcc_cmp.c`, `dcc_assign.c`, `dcc_stmt.c`, `dcc_decl.c`, `dcc_stmt_fast.c`, `dcc_array_narrow.c` | Low-level expression/operator/statement emit helpers and conservative byte-narrowing proof, all feeding the AST-driven path |
| Top level / speculation / output | `dcc_func.c`, `dcc_global_init.c`, `dcc_regalloc.c`, `dcc_loop_regalloc.c`, `dcc_data.c` | Function/frame parsing, global initializer recording, speculative no-IX and BC/E allocation, loop-scoped BC allocation, and data-section emission |

## Inside dccpeep: a fixpoint peephole optimizer

`dccpeep` is DCC C Compiler's **machine-dependent optimizer**. It reads the emitted `.MAC`
as an array of text lines and applies dozens of small *peephole* rewrites —
each one matches a short local instruction pattern and replaces it with a
cheaper equivalent (for example folding a redundant store/reload, threading a
jump-to-jump, turning an `ld`/`cp` against zero into `or a`, or replacing an
absolute `jp` in range with a relative `jr`).

The optimizer is split by responsibility. `peep_lines.c` owns storage,
mutation, physical-line I/O, and opaque barriers for `; dcc user asm` regions;
passes never rewrite or delete those user-authored lines. `peep_parse.c`
contains stateless assembly parsers, `peep_effects.c` caches structured line,
opcode, register, and flag metadata, and `peep_analyze.c` contains shared
conservative register analysis. `peep_control_flow.c` owns versioned
label/function indexes and bounded reachability queries. The main `dccpeep.c`
file owns the descriptor-driven, order-sensitive fixed-point catalogue and
remaining general passes. `PeepContext` groups program ownership, options,
statistics, mutation versions, and cached indexes; compatibility globals keep
legacy pass signatures stable during migration. `PeepEditTransaction` provides
opt-in atomic commit/rollback for coupled rewrites.
The high-volume local dispatcher, board/game idioms, loop registerization, and
compiler-tagged temporary handling live in `peep_pass_once.c`,
`peep_pass_minmax.c`, `peep_pass_loops.c`, and
`peep_pass_inline_temp.c`. Label and branch rewrites live in
`peep_pass_control_flow.c`. Post-convergence shared-helper rewrites and
terminal cleanup live in `peep_pass_stubs.c` and `peep_pass_final.c`
respectively, behind `dccpeep_internal.h`.

dccpeep runs its rewrite catalogue to a **fixpoint** so one rewrite can expose
the pattern another rewrite needs:

```mermaid
flowchart TB
    READ["read .MAC into line array"] --> SWEEP["run all peephole passes once"]
    SWEEP --> CHK{"any pass<br/>changed a line?"}
    CHK -->|yes, and under 30 passes| SWEEP
    CHK -->|no change, or 30 passes| POST["post-convergence passes"]
    POST --> FRAME["IX-frame elimination<br/>+ optional shared stubs (-Os)"]
    FRAME --> FINAL["final tidy:<br/>dead-load removal, jp -> jr"]
    FINAL --> WRITE["write optimized .MAC"]
```

Key design points:

- **Driven to convergence.** The main loop re-runs every pass until a full
  sweep makes no change (capped at 30 iterations), because passes feed each
  other — e.g. a store/reload elimination can create a dead label that a later
  pass then removes.
- **Order-sensitive post-passes.** A few transforms must run *after*
  convergence: the signed-compare constant-bias fold, IX-frame elimination,
  and the shared-stub passes all depend on the instruction stream having
  settled, because earlier structural passes recognise loops by their
  canonical (un-folded) shape.
- **Two optimization goals.** `-Ot` (default, "time") inlines helper sequences
  for speed; `-Os` ("size") factors recurring sequences into shared `call`
  stubs to shrink the binary. Each helper family counts complete matches first
  and fires only at its whole-program linked-stub break-even. The mode is
  chosen on the command line and changes which post-convergence passes fire.
- **Text safety is explicit.** Physical input lines are read without a fixed
  length limit. Marked user-assembly regions become opaque label barriers while
  optimization runs, then are restored byte-for-byte; address relaxation also
  refuses to estimate across them.
- **Direct observability.** `scripts/run-dccpeep-tests.ps1` checks focused
  default, `-Os`, and undocumented-opcode fixtures plus idempotence. Optional
  `-fstats` output reports convergence iterations, per-pass calls and changes,
  and inserted/deleted line totals to stderr without changing normal output.

!!! tip "Why a separate program instead of an in-compiler pass"
    Keeping the peephole optimizer as a standalone text-to-text filter keeps
    the compiler simple and allows inspection, diffing, and skipping
    optimization (`nopeep`). The assembly is the contract between the two tools.

## The runtime: a block-structured library sized for stripping

The runtime `DCCRTL.MAC` is a single ~19,000-line assembly source, but its
*architecture* is what makes the toolchain's "pay only for what you use"
property possible. Rather than one monolithic blob, the runtime is written as
**~280 parsed blocks** around `public` entry points and shared preludes. A
program never links the whole library — `dccrtlstrip` keeps only the blocks the
application actually references (the mark-and-sweep details are in the companion
appendix [*Runtime optimization*](01-dccrtlstrip.md)). The architectural
consequence is that **every routine has a well-defined, measurable size cost**.

```mermaid
flowchart TB
    subgraph RT["DCCRTL.MAC (~19,000 lines, ~280 parsed blocks)"]
      BASE["always-present baseline<br/>~297 lines, 7 blocks<br/>(start, argv/console, heap, exit)"]
        IO["stdio blocks<br/>printf, file I/O core"]
        MEM["memory blocks<br/>malloc/free/realloc"]
        LONG["32-bit long blocks"]
        FLT["float core + math.h"]
        STR["string / ctype blocks"]
    end
    BASE --> APP(["linked .COM<br/>baseline + referenced blocks only"])
    IO -. if referenced .-> APP
    MEM -. if referenced .-> APP
    LONG -. if referenced .-> APP
    FLT -. if referenced .-> APP
    STR -. if referenced .-> APP
```

### Two numbers describe every routine

Because the runtime is block-structured, each public routine has two costs that
the build-time size hook (`docs/docs/hooks/runtime_sizes.py`) measures directly:

- **self** — the source lines in the routine's own block.
- **marginal** — self *plus* every additional block it transitively links
  beyond the always-present baseline. This is the real incremental cost of
  using a routine in a program that otherwise wouldn't need it.

The gap between the two is the whole story of the runtime's size architecture: a
small `self` with a large `marginal` means the routine sits on top of a big
shared substrate (the file-I/O core, or the float arithmetic core).

### The always-present baseline (~297 lines)

Seven blocks are always linked because they are reachable from the forced
`start` root: program entry and heap/BSS setup, the command-tail `argv` builder
(which also contains the console writer `__conout`), the heap-state words, and
`exit`. Console output therefore costs essentially nothing extra — `putchar`
and `puts` call into code that is already present.

### What the feature groups cost

The runtime's size is dominated by a few shared cores. Routines that sit on a
core are cheap individually but expensive to introduce, because the first one
links the whole core:

| Feature group | Shared core it tends to link |
| --- | --- |
| Console output (`putchar`, `puts`, integer `printf`) | baseline console writer or self-contained formatter |
| File-stream + low-level I/O (`fopen`, `fread`, `fputs`, `fprintf`) | FCB/DMA file core |
| `scanf` / `sscanf` / `fscanf` | shared scan core |
| Memory (`malloc`/`free`/`realloc`/`calloc`) | heap helpers and size arithmetic |
| 32-bit `long` arithmetic | long multiply/divide/modulo helpers |
| `float` operators | float normalise/round core |
| `math.h` (`sinf`, `expf`, `powf`, …) | float core plus conversions and chained math helpers |
| `string.h` / `ctype.h` | usually self-contained routines |

The exact per-routine `self`/`marginal` numbers — and the transitive
dependencies behind each one — are tabulated on the auto-generated
[*Runtime function sizes*](02-runtime-sizes.md) page, with the optimisation
takeaways in [*Runtime optimization*](01-dccrtlstrip.md). The takeaways for
sizing a program are:

- **Console-only output and string/ctype routines are cheap** — they link
  little or nothing beyond the baseline.
- **The first file, `scanf`, or `float` operation is the expensive one**; it
  links a shared core. Additional routines in the same family are then nearly
  free.
- **`math.h` is the single biggest lever** — the `exp`/`log`/`pow` and
  hyperbolic group are expensive because each chains other math routines on top
  of the float core.

Those numbers are recomputed from `DCCRTL.MAC` on every docs build, so editing
the runtime and rebuilding the docs is all that is needed to refresh them.

## Architecture summary

- the DCC C Compiler is an **AST-driven** C89-base compiler with selected
  C99/C11 additions, with direct lowering from typed function-body AST nodes to
  Z80/M80 assembly.
- Typed AST expression nodes drive mixed-width (16/32-bit, pointer, float)
  codegen decisions from the tree being emitted.
- Machine-dependent optimization is split out into **`dccpeep`**, a
  fixpoint peephole optimizer over the assembly text, with separate time (`-Ot`)
  and size (`-Os`) strategies.
- The runtime `DCCRTL.MAC` is **block-structured** (~280 parsed blocks over a
  ~297-line baseline), so every routine has a measurable
  `self`/`marginal` size cost and `dccrtlstrip` can link only the blocks a
  program references.
- The back half of the pipeline uses native **`m80c`**/**`l80c`** (or
  Microsoft **`M80`**/**`L80`** under `ntvcm`) for assembly and linking - a
  shared LINK-80-compatible `.REL` object format that DCC C Compiler consumes
  as a fixed target rather than needing to invent its own.
