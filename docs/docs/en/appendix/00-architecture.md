# Appendix: compiler architecture

This appendix describes the DCC C Compiler toolchain: the compiler `dcc`, the peephole
optimizer `dccpeep`, the runtime size reducer `dccrtlstrip`, and the assembler
and linker - either the host-native
[`m80c`](03-utilities.md#native-assembler-m80c)/[`l80c`](03-utilities.md#native-linker-l80c),
or the Microsoft `M80`/`L80` originals running under `ntvcm`.

!!! note "Host-resident tools"
  `dcc`, `dccpeep`, `dccrtlstrip`,
  [`m80c`](03-utilities.md#native-assembler-m80c), and
  [`l80c`](03-utilities.md#native-linker-l80c) run on Windows, macOS, and
  Linux. They never run on a Z80. They emit Z80 assembly text and CP/M
  `.COM` files that run under CP/M-80, for example via the `ntvcm` emulator.
  [`m80c`](03-utilities.md#native-assembler-m80c)/
  [`l80c`](03-utilities.md#native-linker-l80c) are clean-room,
  LINK-80-object-format-compatible
  reimplementations of `M80`/`L80` with unbounded host memory instead of
  CP/M's own 64K tables - large `nopeep` builds can exhaust real `L80`'s own
  in-emulator linking workspace well before the target program itself would
  not fit; [`l80c`](03-utilities.md#native-linker-l80c) has no such ceiling.
  They are the default; pass
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
flowchart TB
  SRC["C source"] --> DCC["dcc<br/>AST → MIR → Z80 assembly"]
  DCC --> PEEP["dccpeep<br/>optional assembly optimization"]
  PEEP --> APPSTRIP["dccrtlstrip<br/>remove unreachable app blocks"]
  APPSTRIP --> APPASM["m80c<br/>assemble application"]
  APPASM --> APPREL["app.REL"]

  RTL["DCCRTL.MAC<br/>full runtime"] --> STRIP["dccrtlstrip<br/>keep referenced routines"]
  APPSTRIP -. runtime references .-> STRIP
  STRIP --> RTLASM["m80c<br/>assemble reduced runtime"]
  RTLASM --> RTLREL["RTLMIN.REL"]

  APPREL --> LINK["l80c<br/>link application + runtime"]
  RTLREL --> LINK
  LINK --> COM["CP/M .COM executable"]
```

| Stage | Tool | Input | Output | Role |
| --- | --- | --- | --- | --- |
| Compile | `dcc` | `.c` | `.MAC` | Parse typed AST, lower and verify MIR, then select Z80/M80 assembly |
| Optimize | `dccpeep` | `.MAC` | `.MAC` | Local peephole rewriting of the asm |
| Reduce application | `dccrtlstrip` | All app `.MAC` files | Rewritten app `.MAC` files | Remove functions, initialized objects, and module BSS objects unreachable from the final program entry |
| Reduce runtime | `dccrtlstrip` | `DCCRTL.MAC` + app `.MAC` | `RTLMIN.MAC` | Keep only the runtime routines the app references |
| Assemble | [`m80c`](03-utilities.md#native-assembler-m80c) | `.MAC` | `.REL` | Object code (relocatable); `dccmake` uses native `m80c` by default |
| Link | [`l80c`](03-utilities.md#native-linker-l80c) | `.REL` files | `.COM` | Resolve symbols into a CP/M executable; `dccmake` uses native `l80c` by default |

The `dccpeep` stage is optional (`./scripts/ma.ps1 name -Mode nopeep` skips it
when run from PowerShell in the DCC C Compiler checkout). `dccrtlstrip` first
computes whole-program reachability across all final application assembly
modules, then uses the reduced application to select runtime blocks.

## Compiler shape: front end, AST, and MIR

DCC C Compiler is an **AST/MIR compiler**. The recursive-descent front end
builds typed AST nodes for function statements and expressions. Those transient
nodes lower into one persistent MIR function before the AST arena is reused.
Production function assembly is emitted from generated MIR candidates.

Top-level declarations, global initializers, strings, and data/BSS placement
remain table-driven because they are not function-body instructions.

```mermaid
flowchart TB
  SRC["C source"] --> FRONT["Front end<br/>preprocess, parse, build typed AST"]
  FRONT --> MIR["Persistent MIR<br/>lower statements and record metadata"]
  MIR --> ANALYZE["Prepare MIR<br/>repair metadata, check structure,<br/>build CFG, promote objects, transform values"]
  ANALYZE --> VERIFY["Verify dominance<br/>independent reachable CFG + PHI-edge checks"]
  VERIFY --> ALLOCATE["Plan baseline allocation<br/>solve liveness, assign homes and spills"]
  ALLOCATE --> SELECT["Select generated Z80 candidate<br/>try exact structural schedules first"]

  SELECT -- exact match --> ASM["Selected .MAC function body"]
  SELECT -- exact declines --> GENERAL["Build generated alternatives<br/>rollout, homed, regional, spilled"]
  GENERAL --> COST["Choose alternative<br/>candidate-specific allocation + mir-v1"]
  COST --> ASM

  ALLOCATE -. optional reports .-> SHADOW["Diagnostic shadow models<br/>target constraints + sparse schedule"]
```

| Classic phase | DCC C Compiler implementation |
| --- | --- |
| Preprocessing / lexical analysis | Integrated macro engine and `next_token` lexer in `dcc_preproc.c`; `dcc_pp_expr.c` evaluates conditional directives |
| Parsing | Recursive descent builds typed AST nodes against live symbol/type tables |
| Intermediate representation | Persistent per-function MIR with virtual values, typed memory, calls, labels, branches, PHIs, VLA operations, and aggregate copies |
| MIR analysis | Deferred metadata repair, structural checks, CFG construction, object promotion, independent dominance verification, liveness, baseline register homes, and spill-slot planning |
| Instruction selection | Priority exact machine schedules, followed by general rollout, homed, hybrid/regional, and spilled CFG candidates |
| Profitability | After exact scheduling declines, `mir-v1` compares generated alternatives using machine instructions/bytes, helper/frame/spill costs, moves, branches, and loop weighting |
| Machine-dependent cleanup | Standalone `dccpeep` fixpoint optimization over the selected assembly |

### Transient AST, persistent MIR

AST nodes retain C result type and effective operand type, so integer
promotions, pointer scaling, long/float operations, bit-fields, and lvalue
widths are decided before lowering. The MIR keeps those decisions after the AST
arena is reset.

MIR uses virtual values rather than source-register claims. Its instruction
set represents constants, parameters, loads/stores, address/member/index
formation, unary/binary operations, call-site-tagged arguments and calls,
aggregate copies, labels, branches, jumps, edge-specific PHIs, returns, and VLA
stack save/allocate/restore operations.

### One production body walk

`dcc_func.c` performs one production metadata/MIR walk for each function.
Declarations are represented by stable placeholders because parser replay may
discover initializers, renamed C99 loop variables, strings, inline temporaries,
or VLA scope events after surrounding statements have already lowered.
`dcc_ast_metadata.c` and `dcc_ast_stmt_meta.c` fill and position those events
without generating a second assembly body.

This separation is load-bearing:

- declarations, scopes, VLA exits, labels, diagnostics, and debug events have
  explicit non-emitting owners;
- statement evaluation occurs once;
- initializer side effects cannot be duplicated by metadata replay;
- every production function body reaches instruction selection as verified MIR.

### Verification, promotion, and allocation

Structural verification checks operand/object bounds, labels, definitions,
PHI references, call identities, and known argument ABI types. After object
promotion and semantic transformations, `dcc_mir_verify.c` independently
reconstructs the instruction CFG and computes an immediate-dominator tree in
reverse postorder, using storage linear in the MIR size.

Every ordinary value use on a reachable path must be dominated by its
definition: execution cannot reach the use without first passing the
definition. PHIs define their results at the logical block entry, even when
they appear later in the instruction array; each input must dominate its
corresponding incoming edge. Argument records and their values must dominate
the matching call. Unreachable paths impose no dominance requirement, but
their IDs and references still undergo structural checks. This non-mutating
check runs before allocation and candidate emission and cannot be disabled
through an environment setting.

Object promotion reuses agreeing reaching definitions or constructs a valid
two-predecessor PHI. It distinguishes an **undefined entry value** from an
**unreached dataflow state**: a definition discovered only on a loop backedge
cannot supply the function-entry path. Undefined values and unresolved joins
remain memory operations. This does not make reading an uninitialized C local
defined behavior; it prevents that read from being represented by an invalid
SSA value. Exact schedules account for the corrected NOP positions where
unnecessary loop merges no longer produce PHIs.

After verification, backwards liveness treats PHI operands as incoming-edge
uses and keeps call arguments live through the matching call-site ID.

Allocation assigns lifetime homes in HL, DE, BC, or callee-saved IY, with
deterministic spill slots when pressure or ABI constraints require them.
Call-crossing ordinary values may use only IY. Fixed Z80 operand/result
registers are boundary constraints, so the emitter inserts moves rather than
precoloring a value for its whole lifetime.

The MIR analysis pipeline computes a baseline allocation and retains its liveness matrices
for selection. Candidate construction may then derive and measure a different
allocation plan: lazy-parameter and regional candidates, for example, save the
baseline homes and spills, recompute them for that candidate, and restore the
baseline before the next attempt.

### Generated-only candidate selection

Every candidate writes to its own temporary stream. A declining selector cannot
leave partial text in the next candidate. Production first tries:

- exact `scheduled-machine-cfg` kernels with complete structural proofs;
- the compact `general-rollout` scalar DAG for eligible straight-line functions;
- `homed-scalar-cfg`, including hybrid and regional-home variants;
- the general `spilled-scalar-cfg` emitter.

An accepted exact schedule has priority. When exact scheduling declines, the
selector establishes a complete generated incumbent from the rollout or general
CFG candidates, then `mir-v1` may compare that incumbent with homed,
lazy-parameter, hybrid, regional, and spilled variants. The policy compares
only generated MIR candidates.
`DCC_MIR_REQUIRE_COMPLETE=1` and `DCC_MIR_REQUIRE_EMIT=1` are the strict
semantic and generated-output boundaries.

Proofs are deliberately conservative. Unknown, recursive, cyclic, volatile,
aliased, or unsupported shapes decline an optimization, not MIR emission.

### Refactor validation

For behavior-preserving front-end or metadata refactors, compare raw compiler
output and diagnostics before and after, then run the CP/M suite. For MIR
selection changes, compare stack and no-stack census snapshots, run every app
whose generated hash or metrics changed in peep and nopeep modes, and finish
with strict full+extended stack and no-stack runs.

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
| Initializers and assignment compatibility | `DCC-E0901`-`DCC-E0921` | invalid initializers, integer-to-pointer assignment, taking the address of a register-qualified object |
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

- **Dead MIR values and stores.** Lowering records side effects separately from
  virtual results. Liveness, object promotion, and selector-local proofs can
  omit unused values, dead stores, and rematerializable temporaries.
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
- **Dead application blocks.** Before runtime selection, `dccrtlstrip` follows
  reachability across the final application's marked assembly modules and
  removes unreferenced functions and eligible objects. This is link-time
  reachability, not a whole-program C statement analysis.

This split keeps the compiler simple while still attacking the biggest sources
of wasted code: unused expression values, local assembly redundancies, unused
inline bodies, and unreferenced runtime support.

## Inside DCC C Compiler: module architecture

The compiler is one binary built from focused modules. Foundational target
types, shared data structures, and broadly used APIs live in `dcc.h`; narrower
contracts live in subsystem `*_internal.h` headers. Shared compiler state is
defined once in `dcc_state.c`; machine-family modules do not add shared data.

```mermaid
graph TB
    subgraph FE["Front end"]
      DRIVER["dcc.c<br/>driver + CLI"]
      PP["dcc_preproc.c / dcc_pp_expr.c<br/>preprocessor + lexer"]
      PARSER["dcc_func.c / dcc_stmt.c<br/>declarations + statements"]
      SEM["dcc_types.c / dcc_symbols.c<br/>types + symbols"]
    end

    subgraph AST["Transient typed AST"]
      BUILD["dcc_ast_build.c"]
      NODES["dcc_ast.c / dcc_ast.h"]
      META["dcc_ast_metadata.c<br/>dcc_ast_stmt_meta.c"]
    end

    subgraph MIR["Persistent function MIR"]
      CORE["dcc_mir.c<br/>lowering + repair + promotion"]
      VERIFY["dcc_mir_verify.c<br/>independent dominance verification"]
      ALLOCATE["dcc_mir.c<br/>liveness + baseline allocation"]
      SHADOW["dcc_mir_target.c / dcc_mir_schedule.c<br/>diagnostic shadow models"]
    end

    subgraph BACK["Generated back ends"]
      SELECT["dcc_mir_select.c<br/>priority, rollout + mir-v1"]
      COMMON["dcc_mir_emit_common.c<br/>scalar DAG + shared emission"]
      HOMED["dcc_mir_homed_cfg.c<br/>homed / hybrid / regional"]
      SPILLED["dcc_mir_spilled_cfg.c<br/>general spilled CFG"]
      MACHINE["dcc_mir_machine_*.c<br/>exact structural schedules"]
    end

    subgraph OUT["Top level and data"]
      INIT["dcc_global_init.c<br/>file-scope initializers"]
      DATA["dcc_data.c<br/>strings, globals, BSS"]
      ASM["selected function streams<br/>+ top-level assembly"]
    end

    DRIVER --> PP --> PARSER --> BUILD --> CORE
    SEM --> BUILD
    NODES -.storage and typed nodes.-> BUILD
    META -.declarations / scopes / VLA events.-> CORE
    CORE --> VERIFY --> ALLOCATE --> SELECT
    ALLOCATE -. diagnostic reports .-> SHADOW
    SELECT --> COMMON
    SELECT --> HOMED
    SELECT --> SPILLED
    SELECT --> MACHINE
    HOMED -. shared operations .-> COMMON
    SPILLED -. shared operations .-> COMMON
    COMMON --> ASM
    HOMED --> ASM
    SPILLED --> ASM
    MACHINE --> ASM
    INIT --> DATA --> ASM
```

| Group | Modules | Responsibility |
| --- | --- | --- |
| Shared | `dcc.h`, `dcc_state.c`, subsystem `*_internal.h` files | Target model, shared contracts, and lifecycle-owned compiler state |
| Front end | `dcc.c`, `dcc_preproc.c`, `dcc_pp_expr.c`, `dcc_func.c`, `dcc_stmt.c`, `dcc_diag_emit.c`, `dcc_global_scan.c` | Driver, preprocessing/lexing, declarations/statements, conservative global-use prepass, frame scan, and diagnostics |
| Types / symbols | `dcc_types.c`, `dcc_symbols.c`, `dcc_constexpr.c`, `dcc_fold.c`, `dcc_asmname.c` | Type system, symbol tables, constant evaluation/folding, and M80-safe assembly-name mapping |
| Typed AST / metadata | `dcc_ast.c`, `dcc_ast_build.c`, `dcc_ast_gen*.c`, `dcc_ast_metadata.c`, `dcc_ast_stmt_meta.c`, `dcc_licm.c` | Transient typed trees, semantic classifiers, and non-emitting LICM/CSE planning and declaration/scope replay |
| Compatibility helpers | `dcc_expr.c`, `dcc_ops.c`, `dcc_cmp.c`, `dcc_assign.c`, `dcc_decl.c`, `dcc_stmt_fast.c`, `dcc_array_narrow.c` | Shared initializer/type behavior and conservative source proofs; not a production body emitter |
| MIR core | `dcc_mir.c`, `dcc_mir_stream.c` | Persistent IR, metadata repair, CFG/verifier, liveness, baseline allocation, and isolated candidate streams |
| MIR dominance verification | `dcc_mir_verify.c` | Independent reachable CFG, immediate dominators, ordinary-value and PHI-edge checks, and call-argument dominance; no IR rewriting or allocation |
| MIR emission / selection | `dcc_mir_select.c`, `dcc_mir_emit_common.c`, `dcc_mir_homed_cfg.c`, `dcc_mir_spilled_cfg.c` | Exact-schedule priority, scalar DAG rollout, transactional generated candidates, candidate-specific homes/spills, shared emission, and `mir-v1` selection |
| Diagnostic shadow models | `dcc_mir_target.c`, `dcc_mir_schedule.c` | Optional Z80 constraint and sparse-schedule reports; these modules do not select production candidates or emit Z80 |
| Machine schedules | `dcc_mir_machine_emit.c` (coordinator), `dcc_mir_machine_*.c` (families) | Exact structural matchers and specialized Z80 streams |
| Top level / output | `dcc_func.c`, `dcc_global_init.c`, `dcc_data.c` | Function/frame parsing, one production metadata/MIR body walk, global initializer recording, deferred static-body placement, and data-section emission |

### Exact machine-schedule families

`dcc_mir_machine_emit.c` owns common machine helpers and the order-sensitive
dispatch. Cohesive schedules live in separately compiled families:

| Family module | Responsibility |
| --- | --- |
| `dcc_mir_machine_attention.c` | Matrix, attention, and fixed-point kernels |
| `dcc_mir_machine_byte_scans.c` | Byte/row scans, fills, copies, hashes, records, and file-line kernels |
| `dcc_mir_machine_constant_folding.c` | Constant/result flows, result switches, and indexed-member schedules |
| `dcc_mir_machine_containers.c` | Array, container, stack, comparison, and reduction schedules |
| `dcc_mir_machine_float_recursion.c` | Floating-point, recursive, tree, and byte-status kernels |
| `dcc_mir_machine_numeric.c` | Integer, long, fixed-point, and math kernels |
| `dcc_mir_machine_float_reports.c` | Float reports and checks |
| `dcc_mir_machine_scanners.c` | Scan, parse, and traversal loops |
| `dcc_mir_machine_aggregate_checks.c` | Aggregate, array, and struct checks |
| `dcc_mir_machine_structural_checks.c` | Literal, bitset, sieve, string, structure, and bitfield validations |
| `dcc_mir_machine_runtime_runners.c` | Runtime, file, and system orchestration |
| `dcc_mir_machine_interpreter_runners.c` | Interpreter and parser runners |
| `dcc_mir_machine_call_runners.c` | Call/control orchestration |
| `dcc_mir_machine_validation_runners.c` | Scope, wide-value, and validation runners |
| `dcc_mir_machine_wide_records.c` | Wide arithmetic, aggregate updates, and record-oriented schedules |
| `dcc_mir_machine_endgame.c` | Large final exact schedule families |

Machine families follow a zero-shared-state rule:

- plans, candidates, and mutable matching state are automatic and attempt-local;
- each family exports one dispatcher and no data;
- module-local constant tables use internal linkage;
- `dcc_mir_machine_internal.h` contains function contracts, not shared plan
  storage.

Run `scripts/audit-c-module-exports.py` after changing a module boundary. The
audit must report only the allowed dispatcher and no exported data.

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
statistics, mutation versions, and cached indexes; compatibility globals
preserve established pass signatures. `PeepEditTransaction` provides opt-in
atomic commit/rollback for coupled rewrites.
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

The runtime `DCCRTL.MAC` is a single assembly source, but its
*architecture* is what makes the toolchain's "pay only for what you use"
property possible. Rather than one monolithic blob, the runtime is written as
hundreds of parsed blocks around `public` entry points and shared preludes. A
program never links the whole library — `dccrtlstrip` keeps only the blocks the
application actually references (the mark-and-sweep details are in the companion
appendix [*Runtime optimization*](01-dccrtlstrip.md)). The architectural
consequence is that routine dependencies can be inspected and their linked
cost measured for a particular application.

```mermaid
flowchart TB
    subgraph RT["DCCRTL.MAC (block-structured runtime)"]
      BASE["always-present baseline<br/>(start, argv/console, heap, exit)"]
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
  beyond the always-present baseline. This estimates source volume, not
  assembled bytes or execution cycles.

The gap between the two is the whole story of the runtime's size architecture: a
small `self` with a large `marginal` means the routine sits on top of a big
shared substrate (the file-I/O core, or the float arithmetic core).

### The always-present baseline

The forced `start` root retains entry, heap/BSS setup, heap-state words, exit,
and their dependencies. Command-tail construction and console output have
separate public blocks; the application main shim can retain additional
support. Use the generated table for the current baseline rather than a fixed
block count. `putchar` and `puts` are small wrappers, not zero-cost operations.

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

- DCC C Compiler is an **AST/MIR** C89-base compiler with selected C99/C11
  additions. Typed AST nodes lower into persistent verified function MIR.
- MIR owns CFG, PHIs, virtual values, object promotion, liveness, register
  homes, spills, target constraints, and generated candidate selection.
- Production function assembly comes only from MIR.
- Structurally proven exact machine schedules have priority. When they decline,
  general rollout and homed/spilled CFG candidates are selected from generated
  streams, with `mir-v1` arbitrating eligible alternatives.
- Machine-dependent optimization is split out into **`dccpeep`**, a
  fixpoint peephole optimizer over the assembly text, with separate time (`-Ot`)
  and size (`-Os`) strategies.
- The runtime `DCCRTL.MAC` is **block-structured** into hundreds of public
  blocks, so every routine has a measurable
  `self`/`marginal` size cost and `dccrtlstrip` can link only the blocks a
  program references. The exact current totals are generated on the
  [runtime size page](02-runtime-sizes.md).
- The back half of the pipeline uses native
  **[`m80c`](03-utilities.md#native-assembler-m80c)**/
  **[`l80c`](03-utilities.md#native-linker-l80c)** (or
  Microsoft **`M80`**/**`L80`** under `ntvcm`) for assembly and linking - a
  shared LINK-80-compatible `.REL` object format that DCC C Compiler consumes
  as a fixed target rather than needing to invent its own.
