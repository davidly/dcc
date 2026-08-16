---
name: mir-migration
description: "Maintain and optimize dcc's completed generated-only MIR backend. Use for MIR lowering, verification, selectors, cost policy, register homes, spills, exact machine schedules, coverage censuses, or MIR performance recovery."
---

# dcc generated-only MIR backend

The migration is complete:

- Every production function body is lowered to verified MIR.
- Production Z80 comes only from a selected MIR candidate.
- AST code after parsing is metadata-only: declarations, scopes/VLAs, inline
  temps, strings, labels, diagnostics, and debug events.
- There is no body-codegen fallback, capture/replay stream, discard emitter, or
  legacy register-allocation retry.
- Compatibility `captured_*` census columns are always `-1`.

Use `dcc-project` for build, test, runtime, and baseline conventions.

## Non-negotiable rules

1. Keep `DCC_MIR_REQUIRE_COMPLETE=1` and `DCC_MIR_REQUIRE_EMIT=1` clean.
2. Never use legacy output, app/function names, padding, or performance
   baselines as production selection gates.
3. Identify every changed function before widening a selector or cost gate.
4. Correct output that regresses peep or nopeep remains unfinished.
5. Emit each candidate to its own temporary stream; a declined selector must
   not contaminate the next candidate.
6. Preserve type, volatility, aliasing, call ABI, CFG edges, PHI edge uses,
   VLA restoration, and callee-save state explicitly.
7. Prefer reusable range/liveness/value proofs over one-off schedules; use an
   exact schedule only when a general emitter cannot retain the proven win.
8. Generated reports and snapshots belong under `build/`.

## Architecture

| Surface | Location |
| --- | --- |
| Lowering, metadata repair, verifier, CFG/liveness | `src/dcc/dcc_mir.c` |
| Public/internal MIR contracts | `dcc_mir.h`, `dcc_mir_internal.h` |
| Candidate selection and `mir-v1` policy | `dcc_mir_select.c` |
| Shared emission and homes | `dcc_mir_emit_common.c`, `dcc_mir_homed_cfg.c` |
| Spills and general CFG emission | `dcc_mir_spilled_cfg.c` |
| Z80 costs/constraints/scheduling | `dcc_mir_target.c`, `dcc_mir_schedule.c` |
| Exact schedule dispatch | `dcc_mir_machine_emit.c` |
| Exact schedule families | `dcc_mir_machine_*.c` |

AST and MIR C/header modules follow the Doxygen file-header convention defined
in the `dcc-project` skill: `@file`, `@brief`, explicit role, key entry points,
and module boundaries. Keep the module maps in `dcc_ast.h` and
`dcc_mir_internal.h` current whenever files move or responsibilities change.
Never document `dcc_ast_gen*` as a production body-codegen fallback.

Place new schedules in the closest family module. Keep plan/matcher state
automatic and expose only that module's dispatch function:

```sh
python3 scripts/audit-c-module-exports.py src/dcc/module.c \
  --allow-function module_dispatch
```

## Diagnostics

| Control | Purpose |
| --- | --- |
| `DCC_MIR_SELECT_REPORT=1` | Selected emitter, reason, bytes, instructions, blocks, hash |
| `DCC_MIR_REPORT=1` | Dump every MIR function, liveness, and allocation |
| `DCC_MIR_FUNCTION=name` | Enable named MIR diagnostics; filter stderr by function |
| `DCC_MIR_COVERAGE=1` | Report remaining opaque lowering |
| `DCC_MIR_REQUIRE_COMPLETE=1` | Reject incomplete semantic MIR |
| `DCC_MIR_REQUIRE_EMIT=1` | Reject any non-generated body |
| `DCC_MIR_SELECT_FUNCTION=name` | Restrict a candidate comparison |
| `DCC_MIR_SELECT_CANDIDATE=name` | Select a named generated candidate |
| `DCC_MIR_EMIT_FUNCTION=name` | Diagnostic specialized-emitter isolation |
| `DCC_MIR_GENERAL_FUNCTION=name` | Diagnostic general-emitter isolation |
| `DCC_MIR_COST_POLICY=mir-v1-report` | Report alternatives without selecting them |

Do not document or revive removed forced-legacy controls.

## Snapshot and compare

Take both snapshots before changing selection:

```sh
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  python3 scripts/mir-migration-census.py \
  --output build/mir-before.tsv --jobs 24 --timeout 60
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  python3 scripts/mir-migration-census.py \
  --output build/mir-before-stack.tsv --extra-args=-fstack-check \
  --jobs 24 --timeout 60
```

Compare after rebuilding:

```sh
python3 scripts/mir-migration-census.py \
  --output build/mir-after.tsv --compare build/mir-before.tsv \
  --fail-on-regression
python3 scripts/mir-migration-census.py \
  --output build/mir-after-stack.tsv --compare build/mir-before-stack.tsv \
  --fail-on-regression --extra-args=-fstack-check
```

The census prints the exact focused `runall.ps1` app list. Validate that entire
list in stack and no-stack full mode.

For current-vs-parent generated candidates:

```sh
python3 scripts/mir-current-vs-parent.py \
  --parent-compiler build/dcc-parent --apps app1,app2 \
  --output-dir build/mir-parent-compare
```

`scripts/mir-migration-validate.sh <label> <baseline.tsv> <apps>` is a useful
fail-fast iteration ladder. It does not replace the final strict stack and
no-stack release gates.

## Efficient change loop

1. Start from a committed checkpoint and rebuild `dcc`.
2. Snapshot stack and no-stack censuses.
3. Form one falsifiable hypothesis and name the affected app/function.
4. Inspect MIR, candidate output, and linked runtime profile.
5. Make the smallest reusable change.
6. Rebuild and run the cheapest focused `-Mode full` discriminator.
7. Compare censuses with `--fail-on-regression`.
8. Run every changed app in strict stack and no-stack full mode.
9. Use ASan/UBSan for CFG, liveness, allocation, recursive proof, or ownership
   changes.
10. Run both strict full+extended release gates before commit/push.

## Selector and cost policy

- `mir-v1` compares generated candidates only. Exact schedules retain priority
  when their complete structural proof succeeds.
- A semantic matcher proves legality; the cost policy proves profitability.
  Do not merge those responsibilities.
- Static assembly bytes and instruction counts are proxies. Check linked bytes
  and dynamic cycles.
- Account for helper retention, frame bytes, spills, register moves, branches,
  calls, and loop frequency.
- A helper with a fallback may be profitable only in a real CFG cycle or a
  separately proven hot exact kernel.
- Preserve straight-line compact code when linking a new helper would cost more
  than it saves.

## Register and liveness rules

- HL/DE/BC/IY homes are allocation results, not source-symbol claims.
- BC/DE are caller-saved. Only IY may hold an ordinary value across calls.
- IY users save/restore it and account for shifted parameter offsets.
- `_setjmp`/`_longjmp` restore IY because non-local control bypasses epilogues.
- PHI operands are edge uses, and call arguments remain live through their
  matching call-site ID.
- Backedges extend live intervals; do not infer loop safety from linear text
  when CFG successors are available.
- Unknown aliases, volatile access, opaque asm, indirect writes, and calls
  conservatively kill proofs.
- Mark graph nodes visited when enqueuing so bounded worklists cannot overflow
  through duplicate predecessors.

## Exact schedules

Exact schedules must prove the complete relevant semantics:

- opcode/CFG shape and unique labels;
- value and PHI relationships;
- types, signedness, widths, storage, and volatility;
- parameter offsets, array extents, strides, and aggregate sizes;
- direct call prototypes, argument identities/order, and cleanup;
- stack-check placement, frame size, spills, and callee saves;
- all mutations, exits, and observable calls.

Support source spelling variants through semantic normalization or separately
proven structural fingerprints, never source/function-name tests. Add a
permanent near-match or alternate-form test when a production variant caused
the fix.

## Tooling and release checks

```sh
pwsh ./scripts/test-mir-require-emit.ps1
DCC_MIR_REQUIRE_EMIT=1 python3 scripts/mir-extended-census.py \
  --mode both --require-complete --output build/mir-extended.tsv
python3 scripts/rtl-iy-safety.py
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
git diff --check
```

Final publication requires:

```sh
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh ./scripts/runall.ps1 -Mode full -Extended \
  -RunTimeout 30 -FailuresOnly
DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
  pwsh ./scripts/runall.ps1 -Mode full -Extended -NoStackCheck \
  -RunTimeout 30 -FailuresOnly
```

Keep `plan.md` as the concise handoff and `mir-text-size-plan.md` as the
chronological experiment/rejection log. Re-derive priorities from current
census/profile data rather than continuing stale migration plans.
