# AST Work Complete

This document summarizes the AST migration checkpoint that reached full forced-AST coverage across the current `tests/*.c` corpus.

## Result

Forced AST code generation now covers every statement/expression site in the current fast test corpus.

Final coverage scan:

```text
TOTAL emit=12094 fallback=0 coverage=100.00%
```

Final forced-AST fast regression suite:

```text
Passed:  188
Failed:  0
Skipped: 7
```

Validation command used:

```sh
set +H; cd /home/dave/GitHub/dcc; \
pkill -9 ntvcm 2>/dev/null || true; \
timeout 300 env DCC_AST_GEN=2 pwsh ./scripts/runall.ps1 -Mode fast \
  2>&1 | rg -n "FAIL  |Failed apps|OUTPUT MISMATCH|Passed:|Failed:|Skipped:"
```

## Main Files

- `src/dcc/dcc_ast_gen.c`
- `src/dcc/dcc_ast_build.c`

`src/dccpeep/dccpeep.c` was investigated during the final `ttt` optimizer issue, but no peephole optimizer source change was kept. The final fix was in AST emission shape, not in the optimizer.

## Major Coverage Added

- Sparse long `switch` support:
  - Long-valued switch controls are accepted for sparse switch lowering.
  - Long case labels are accepted by low-16-bit uniqueness.
  - Jump-table lowering remains conservative and rejects wide/large long ranges.

- Nested `case` / `default` labels inside switch bodies:
  - Duff's-device style `switch { case 0: do { ... case 7: ... } while (...); }` is now represented and emitted by the AST path.
  - The AST path has its own local switch-label context for nested labels rather than depending on the streaming parser's private switch context.

- Scoped declaration and C99-style `for` handling:
  - Declaration-bearing compounds are gated by replaying declarations with emission suppressed so block locals resolve during AST support checks.
  - `for (int i = ...; ...; ...)` scopes now preserve the same rename sequence as streaming codegen.
  - The support gate now consumes `g_for_seq` in source order while checking sibling loops, then the top-level AST probe restores the counter before real emission.

- Indexed and nested lvalue support:
  - Nested indexed struct object reads, including shapes such as `b[r][c].field`.
  - Deep N-dimensional indexed lvalues for dead-result compound operations.
  - Indexed postfix lvalues such as `movecnt[ply]++` inside address calculations.
  - Long-valued constant subscript expressions where address math intentionally uses the low word.

- Long, float, pointer, and compound assignment coverage:
  - Long dereference compound shifts preserve pointer/address registers correctly.
  - Non-direct plain-int bitwise compounds are supported when the result is dead.
  - Pointer `+=` / `-=` with constant-binary RHS scaling is supported.
  - Long/float/index/member assignment-valued expressions were widened where their existing emitters already matched streaming semantics.

- Constant controlling expressions:
  - Constant `if`/loop controlling expressions are folded only in the condition branch path.
  - Integer casts in constant conditions apply DCC-width truncation/sign-extension for 8-bit, 16-bit, and 32-bit integer types.
  - Constant-left short-circuit forms, such as macro-expanded `1 && expr`, lower as branches rather than as value-producing logical expressions.

## Peephole Optimizer Workaround

The final difficult issue was `ttt` under forced AST with peephole optimization enabled.

Symptom:

```text
Expected: 64930 moves
Actual:   411240 moves
```

For a single direct run, the optimized forced-AST binary printed:

```text
41124 moves
1 iterations
```

while the unoptimized forced-AST binary printed the correct:

```text
6493 moves
1 iterations
```

This showed that the AST-generated code was semantically correct before `dccpeep`, but had a shape that interacted badly with MinMax-specific peephole passes.

The unsafe intermediate shape came from AST `for`-increment emission. For dead-result loop increments like `p++`, the AST path was using the value-producing postfix expression emitter. That preserved the old value with a `push hl` / `pop hl` style sequence. As a result, the optimized `MinMax` function no longer matched the streaming loop-counter pattern exactly. The peephole optimizer then applied related MinMax reductions in a combination that produced incorrect optimized behavior.

The workaround/fix is deliberately on the AST side:

- Do not change `dccpeep` for this case.
- Emit dead-result `for` increments with the same statement-style update path used for dead expression statements.
- For identifier increments, call `emit_incdec_sym_direct`.
- For addressable lvalues, emit the lvalue address and call `emit_incdec_addr`.
- Avoid using the value-producing postfix emitter when the increment result is dead.

This restores the streaming-like loop increment shape. The existing peephole optimizer can then safely recognize its normal MinMax patterns, including loop-counter and frame reductions, without needing an optimizer-side exception.

The important lesson is that peephole safety here depends on AST preserving the streaming dead-expression shape, not merely preserving source-level semantics. If a future AST change makes a hot optimized function semantically correct in `nopeep` but wrong in `fast`, compare optimized and unoptimized `.MAC` output and look for changed peephole pattern eligibility before editing `dccpeep`.

## Final Verification

Focused target checks were used while closing the last slices:

- `tctxops`: 0 fallbacks, target-green.
- `tctxflt`: 0 fallbacks, target-green.
- `tchess`: 0 fallbacks, target-green.
- `tarray`: 0 fallbacks, target-green.
- `tforsco`: 0 fallbacks, target-green.
- `tlimits`: 0 fallbacks, target-green, runtime output confirmed.
- `ttt`: 0 fallbacks, optimized output restored.
- `tttu`: 0 fallbacks, target-green.
- `tgnarly`: 0 fallbacks, target-green.

Final full-suite invariant is preserved:

```text
188 passed / 0 failed / 7 skipped
```

## Notes For Future Work

- C99 implementation conveniences are acceptable in the compiler project direction where they make sense; the long-term compiler goal is C11 support.
- Keep target-code behavior conservative and validated against CP/M test outputs.
- For constant expressions, avoid broadly enabling generic constant value emission just to reduce fallbacks. The successful approach was to limit folding to controlling-expression branch lowering.
- For switch support, keep jump-table eligibility conservative. Sparse long switches should use low-16-bit comparisons; dense jump tables should stay in the old safe range.
- For declaration/scoped-for handling, preserve the `g_for_seq` pre-order relationship with the frame-sizing scan and streaming codegen.