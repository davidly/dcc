# Compiler source coverage

Run `sh scripts/compiler-coverage.sh` from the repository root to build a
separate Clang-instrumented compiler and exercise it with:

- all main applications in both peephole and no-peephole modes;
- the diagnostics suite;
- the dccpeep fixtures; and
- all applicable C89, C99, and C11 extended single-exec tests in both modes.

The generated text and HTML reports are kept under
`build/compiler-coverage/report/` and are intentionally not committed.

## Initial report

The first full run on 2026-08-28 produced:

| Metric | Covered |
| --- | ---: |
| Functions | 88.48% |
| Lines | 83.91% |
| Branches | 56.77% |
| Regions | 84.60% |

All 473 runnable main applications and all 196 applicable extended tests
passed in both optimization modes during this run. The 12 main-suite and 23
extended-suite skips remained the documented target or dialect exclusions.

## Interpretation

The aggregate includes source retained for fallback, migration, or shadow
analysis rather than selected production output. In particular,
`dcc_assign.c`, `dcc_stmt_fast.c`, `dcc_cmp.c`, and most of
`dcc_ast_gen_expr.c` are legacy direct-emission paths; `dcc_mir_schedule.c` and
`dcc_mir_target.c` are shadow scheduler/target paths. Their near-zero coverage
does not mean ordinary accepted programs bypass testing, but it does make the
unfiltered total unsuitable as a pass/fail threshold.

The first actionable areas to inspect are:

1. `dcc_diag_emit.c` (54.03% lines, 29.86% branches): uncommon diagnostic
   formatting, recovery, and reporting combinations.
2. `dcc_symbols.c` (49.87% lines, 39.14% branches): older symbol load/store
   helpers mixed with still-active symbol-table and declaration behavior.
3. `dcc_mir_select.c` (64.34% lines, 53.53% branches): candidate rejection,
   fallback, and reporting paths that normal successful selections do not
   often reach.
4. `dcc_preproc.c` (78.30% lines, 60.37% branches): malformed directives,
   macro-limit handling, include failures, and less-common token forms.
5. Active AST condition/support code, especially `dcc_ast_gen_cond.c` and
   `dcc_ast_gen_support.c`, after separating current MIR metadata/lowering
   paths from retained direct emitters.

Before setting coverage targets, the next step should classify compiled
functions as production, diagnostic/error-only, optional instrumentation, or
retained legacy code. Reports can then show a production-only total and track
changes without encouraging tests that merely execute obsolete paths.
