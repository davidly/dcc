# AST Migration Handoff

This handoff captures the current AST migration checkpoint for continuing work on another machine.

## Current State

- Branch/workspace: `ast-implementation` in `/Users/dave/GitHub/dcc`.
- Acceptance target: keep C89-plus target compatibility, keep existing tests green, and prefer generated `.COM` size same or smaller. Byte-identical `.MAC` probes are used as focused safety checks, but size reduction with green tests is acceptable.
- Latest fast regression checkpoint passed with AST enabled:
  - Command: `DCC_AST_GEN=1 pwsh ./scripts/runall.ps1 -Mode fast`
  - Result: `Passed 189 / Failed 0 / Skipped 7`
  - Total time on current machine: about 32 seconds.
- Latest fallback scan:
  - Command: `./scripts/ast-fallback-report.sh | tee /tmp/ast-fallback-summary-after-callptrarg.txt | sed -n '1,60p'`
  - Total fallback sites: `18237`
  - Largest current buckets: `expr-stmt 5870`, `assign 2770`, `call 2465`, `if 1990`, `compound 1244`.
  - Top apps: `pint 790`, `cint 789`, `forint 671`, `a1 657`, `adaint 650`, `fint 604`.
  - Pointer tests are no longer the biggest surface: `tptrrhs 328`, `tptrlhs 341`.

## Files And Helpers

- Main active implementation file: `src/dcc/dcc_ast_gen.c`.
- Current focused diff in that file was about `287` changed lines: `262 insertions`, `25 deletions`.
- Helper scripts created and staged:
  - `scripts/ast-corpus-diff.sh`
  - `scripts/ast-fallback-report.sh`
- Other AST infrastructure already exists in the branch, including `src/dcc/dcc_ast.c`, `src/dcc/dcc_ast.h`, and `src/dcc/dcc_ast_build.c`.

## Recently Added AST Coverage

- Pointer-expression support across stores, reads, returns, and call arguments:
  - computed deref stores and reads
  - computed `->` member bases
  - dot-over-deref member stores
  - pointer-valued member fields
  - pointer-array elements
  - `*(&ptr)` member reads
  - pointer-returning calls used as pointer expressions
  - pointer-expression direct-call arguments
- Index/address support:
  - address-of 1-D and 2-D elements
  - 2-D int array and struct-field reads/stores
  - index-only `plain +/- integer-literal` subscripts
  - address-of pointer-array elements
- Direct-call argument widening:
  - long actuals use the streaming `push de; push hl` shape
  - float literal/identifier actuals use the same 4-byte push shape
  - pointer expressions route through `gen_pointer_expr_ast` before pushing
- Assignment support:
  - non-direct local `int`/`char` scalar assignment through streaming's address+store tail
  - simple direct local long assignment using existing long load/promote/store helpers

## Validated Probes And Targets

Focused byte-identical probes passed:

- `tptrreturnarith`
- `tnondirectlhs`
- `tnondirectcharlhs`
- `taddrderefmember`
- `tcallptrread`
- `tcalllongarg`
- `tlongassign`
- `tcallfloatarg`
- `tcallptrarg`

Targeted baselines passed:

- `tptrrhs`
- `tptrcnd`
- `tlong`
- `tfloat4`
- `tmathf`
- `ttrig`

Some manual target diffs needed CR normalization before comparison.

## Useful Commands

Build host tools:

```sh
pwsh ./scripts/build-dcc.ps1
```

Focused AST-on/off probe pattern:

```sh
DCC_AST_GEN=2 DCC_AST_REPORT=1 ./dcc /tmp/probe.c -o /tmp/probe.on.mac >/dev/null 2>/tmp/probe.report
./dcc /tmp/probe.c -o /tmp/probe.off.mac >/dev/null 2>/dev/null
cat /tmp/probe.report
diff -u /tmp/probe.off.mac /tmp/probe.on.mac
```

Targeted app build/run pattern:

```sh
DCC_AST_GEN=1 pwsh ./scripts/ma.ps1 tptrrhs fast
cd build
ntvcm -s:0 TPTRRHS.COM > /tmp/tptrrhs.out 2>&1
cd ..
tr -d '\r' < /tmp/tptrrhs.out > /tmp/tptrrhs.norm
diff -u tests/baselines/tptrrhs.txt /tmp/tptrrhs.norm
```

Fallback summary:

```sh
./scripts/ast-fallback-report.sh | tee /tmp/ast-fallback-summary.txt | sed -n '1,60p'
```

Fast suite checkpoint:

```sh
DCC_AST_GEN=1 pwsh ./scripts/runall.ps1 -Mode fast
```

## Suggested Next Work

- Stop focusing primarily on `tptrrhs`; the highest payoff is now broad app patterns in `pint`, `cint`, `forint`, `a1`, `adaint`, and `fint`.
- Sample concrete fallback lines from those apps, then look for repeated assignment/call shapes.
- Good acceleration candidates:
  - more direct-call argument forms
  - more simple scalar assignment/read shapes
  - common interpreter assignment patterns in `pint` and `cint`
  - long/float scalar read and assignment cases where existing helpers already match streaming
- Keep probe-first discipline for long/float member/index reads.
- Keep broad `AST_CAST` deferred until target-type information is available.
- Keep char 2-D stores deferred unless byte-store drift is understood.

## Hazards

- After chained `AST_INDEX` and `AST_MEMBER` support, never assume bases are bare identifiers. Check node kinds before reading `sval`.
- Byte/char stores often have streaming fast paths or byte-store artifacts. Probe before widening.
- Some editor diagnostics missed clang errors from static declaration ordering; rebuild after edits.
- Do not revert unrelated dirty files. Current branch has unrelated/user changes such as `.vscode/settings.json` and `tests/t.c`.