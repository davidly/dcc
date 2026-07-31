# MIR migration: forward plan from the `e4cea99` checkpoint

## Where this picks up

Coverage: **204/2378 (8.58%)** at commit `e4cea99` on `perf/unified-regalloc`.
Two threads are open, both scoped as genuinely multi-session structural work
in `mir-migration-plan-to-100pct.md` (do not re-attempt either as a quick
one-shot fix - that document already records why small guesses were
correctly deferred):

1. **A real, reproduced-but-not-yet-isolated correctness bug** in
   `mir_emit_fused_comparison_branch`'s Item-27 (signed-zero-sign-test)
   path, affecting `forint.assign_pre`/`bump_sym_val` (and very likely
   `cobint`/`tinline`/`tinlinfb`). Currently harmless in production - these
   functions remain on the fallback path by construction of existing
   acceptance gates - but it blocks trusting this fusion for any future
   gate widening.
2. **The single largest remaining coverage lever**: `mir_try_emit_spilled_
   scalar_cfg` (2230/2378 functions' selector) never reads
   `mir.allocation_colors`/`allocation_spills` at all, so every value is
   always memory-resident. `mir_try_emit_homed_scalar_cfg` proves the
   register-aware approach works, but is gated to zero-spill, (now)
   defined-callee-only functions (~119/2378).

This plan sequences both into small, independently valid, revertible items
- exactly the loop this session's checkpoints (`72b3754`, `a7638c3`,
`32efbfb`, `e4cea99`) have been running - rather than proposing a big-bang
rewrite of either the fused-branch emitter or the spilled selector.

## Item A: instruction-level root-cause of the Item-27 bug (do this first)

**Why first**: it's a correctness bug, not a missing feature; SKILL.md's
philosophy ("fixing real bugs behind existing gates is lower-risk and
higher-value than widening gates further") and this plan's own rule 1
both apply. It also directly gates whether Item-27's fusion can ever be
trusted for wider use, which affects Item C below.

**Concrete steps** (session-sized, ~1-2 hours):

1. Build a debug/instrumented `ntvcm`-runnable single-step trace, or use
   `DCC_MIR_REPORT=1`/`DCC_MIR_SELECT_REPORT=1` plus manual disassembly
   annotation, to capture the exact SP and return-address value
   immediately before and after `assign_pre`'s **first** call site in
   `forint`'s `e.for` run (the call that already prints correctly, right
   before all subsequent output vanishes). Confirm or rule out a
   stack-imbalance/corrupted-return hypothesis directly, rather than by
   inference.
2. As a **diagnostic-only** change (never commit this form), temporarily
   force `mir_fused_compare_is_signed_zero_sign_test` to always return `0`
   for just this one function (e.g. gate it behind
   `DCC_MIR_DISABLE_ITEM27_FUNCTION=name` or a hardcoded string compare you
   delete afterward) and re-run the `forint` `e.for` scenario under
   `DCC_MIR_FORCE_ACCEPT_FUNCTION=assign_pre`. If the failure disappears,
   Item 27's fusion is conclusively implicated; if it persists, the bug is
   elsewhere in `spilled-scalar-cfg`'s call/frame handling for this
   function and Item 27 is exonerated - route the investigation there
   instead (check `mir_emit_home_push`/call-arg push-order interactions
   with `resolve_idx`/`eval_e` for this call shape specifically).
3. Once isolated to a specific emission choice, write the smallest
   reproducer that exercises only that choice (a same-shape but far
   simpler standalone `.c` file, verified to actually reproduce - do not
   reuse this session's inconclusive `fprintf`-instrumented reproducer
   attempt, which changed the MIR shape enough to be untrustworthy;
   confirm any new reproducer fails *before* investigating it further).
4. Fix the root cause the same way Item 2 (the `mir_emit_homed_unary_
   instruction` HL/DE clobber) was fixed this session: identify the
   general principle (not a `forint`-specific patch), apply it, then
   re-validate with a full census + focused + wide-fast + full-extended
   run before considering it landable.

**Validation**: `scripts/mir-migration-bisect.sh forint
assign_pre,bump_sym_val` must report neither function implicated once
fixed (i.e., forcing both to MIR-accept, not just fallback, must pass).
Then the standard full tier: census `--fail-on-regression`, focused
`-Mode full` on any newly-unblocked apps, wide `-Mode fast`, full `-Mode
full -Extended`.

**Exit criteria for this item**: either (a) a landed fix, verified against
all of `forint`/`cobint`/`tinline`/`tinlinfb`, or (b) if root-causing proves
to need more than one session, a **specific, mechanism-level** deferral
(not "still broken, moving on" - identify at least which emission helper
or invariant is violated) written into `mir-migration-plan-to-100pct.md`'s
Execution Log, same rigor as this session's entry.

## Item B: prevalence check before any gate widening

Before touching acceptance gates based on Item A's fix, re-run the
`DCC_MIR_SELECT_REPORT=1` census-adjacent survey to count how many
fallback functions in the full corpus actually contain the Item-27 shape
(`<`/`>=` against a provably-signed operand and constant-0 RHS) - this
tells you the real yield of trusting the fusion again, separate from just
"the bug is fixed." Do this **after** Item A, not before, so the count
reflects a shape you can safely re-enable.

## Item C: resume Phase 1's narrowest next slice - calls with spilled cross-call values

Per `mir-migration-plan-to-100pct.md`'s Phase 1 dependency order, the
already-landed slice (`72b3754`) only admits `MIR_CALL` when *every*
argument is narrow/non-struct and the callee is defined - it does not yet
handle a value that is **live across the call and spilled** (as opposed to
zero-spill functions only). This is the next-narrowest widening before
attempting the full "register-aware general CFG emission" rewrite:

1. Audit `mir.allocation_colors`/`allocation_spills`'s existing `cross_call`
   tracking (referenced in `mir-migration-plan-to-100pct.md` Phase 1 step
   3) to confirm it already forces a spill (not a caller-saved register)
   for any value live across a call - if true, `homed-scalar-cfg` can admit
   functions with `allocation_spill_count > 0` **as long as every spilled
   value's load/store already goes through the correct `ix`-relative slot
   helpers** (reuse `mir_emit_virtual_load`/`_store`, the same helpers
   `spilled-scalar-cfg` uses, for exactly the spilled subset - do not write
   a new memory-access helper).
2. Implement as a new acceptance condition: allow `mir.allocation_spill_
   count > 0` in `homed-scalar-cfg`'s gate, and in the emission switch, for
   any value whose `allocation_colors[v] == MIR_COLOR_NONE` (or however
   spilled values are marked - confirm the exact sentinel in
   `mir_summarize_allocation` first), fall through to the existing
   `spilled-scalar-cfg` memory-access emission helpers instead of the
   homed register-move helpers, for that value only.
3. Falsifiable check before writing code: pick 5-10 fallback functions with
   exactly 1-2 spilled values and otherwise-homed-eligible shapes (forced-
   accept them today, expect rejection due to the spill-count gate), hand-
   verify what `spilled-scalar-cfg` already emits for those specific
   values matches what the widened homed path would produce.
4. Standard validation tier (census, focused, wide-fast, full-extended)
   before commit/push, exactly as every prior item this session.

**Do not** attempt the full "general CFG selector consults the allocator
for all 2230 spilled-path functions" rewrite in one item - per the plan's
own Phase 1 section, that requires cross-block PHI register-identity
reconciliation at merge points, which is a separate, harder-still item.
Item C is scoped to stay inside `homed-scalar-cfg`'s existing zero-cross-
block-register-identity-problem simplification (it still requires
zero-spill *at merge points*, only calls' spill-around-call values are
newly allowed) - re-derive the exact boundary from the allocator's data
before implementing, do not assume.

## Item D: PHI register-identity reconciliation (only after Item C lands and is measured)

This is Phase 1's actual "big lever" - a new selector mode that keeps
non-spilled values in registers **across block boundaries**, requiring:

- at each merge point, either all predecessors agree on a value's register,
  or an explicit reconciling move is inserted on the edge that disagrees;
- this is real work (a mini register-coalescing pass), is the highest-risk
  item in this entire plan, and should not be started until Items A-C have
  produced a clearer picture of real-world yield and a proven pattern for
  spill-around-instruction handling to reuse.

Treat Item D as "not yet scoped enough to implement" - the next session
that reaches this point should re-derive its own falsifiable hypothesis
and smallest-slice plan from a fresh census, per SKILL.md's guidance to
re-derive rather than continue stale numbering once a vein like this
starts running dry.

## Sequencing rule

Do Item A before Item C (a bug fix before a widening in the same file
family reduces risk of conflating a pre-existing bug with a new
regression). Do Item B between them (cheap, informs priority). Do not
start Item D until C is committed, measured, and this document is updated
with C's actual yield.

## Item B: resolved as not-applicable (surveyed, no action needed)

Re-checked the premise before surveying: Item 27
(`mir_fused_compare_is_signed_zero_sign_test`) is an **emission-quality**
fusion applied unconditionally inside the binary-op emitter for any
*already-accepted* function - it is not, and never was, an acceptance
gate that Item A's fix disabled or re-enabled. The only thing Item A
removed was a diagnostic-only per-function disable hook
(`DCC_MIR_DISABLE_ITEM27_FUNCTION`) used to bisect a different bug; it
never affected production behavior. So there is no "trust the fusion
again" yield to measure - the fusion has been continuously active in
production the whole time.

Ran the survey anyway to have a concrete number: of the 2174 functions
currently on fallback (post-Item-A census), 628 contain at least one
`<`/`>=` binary comparison anywhere in their MIR (a deliberately loose
upper bound - not filtered for constant-0 RHS or signed operand type, so
the true Item-27-shape count is smaller). This is irrelevant to Item 27
specifically, since none of these functions are being kept on fallback
*because of* Item 27 - their `fallback_reason` values are the usual
`text-size`/`instruction-count`/`cfg-backedge`/etc. Whether any of them
move to MIR is governed entirely by those other gates, independent of
this fusion.

**Conclusion**: Item B is complete with a "no action" result. Proceeding
directly to Item C, which is the actual lever for additional coverage.

## Item C: audited, deferred with rationale (Item-6-style)

**Audit finding (step 1 from the plan above)**: the allocator's cross-call
handling is more nuanced than assumed when Item C was scoped. In
`mir_summarize_allocation`, a value live across a call is *not* always
spilled - it is preferentially colored into `MIR_COLOR_IY` (the one
register available across calls) when no other cross-call value
conflicts with it; it only spills when `cross_opaque` is also set, or
when IY is already claimed by a higher-priority cross-call value (the
sort by `cross_call[]` then `degree[]` picks a winner). So
`allocation_spill_count != 0` in `mir_try_emit_homed_scalar_cfg`'s entry
gate is not purely a "calls are present" signal - it also fires for
plain register-pressure spills unrelated to any call, and for the
"second or later" cross-call value once IY is taken.

Widening this gate is not a single-line change: `mir_try_emit_homed_scalar_cfg`
(272 lines) has a **second**, independent rejection inside its main
instruction loop - `if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0)
return 0;` - which fires for every spilled value's *definition*, and the
rest of the function's ~15 opcode cases (`MIR_BINARY`, `MIR_UNARY`,
`MIR_ARG`, `MIR_CALL`, `MIR_RETURN`, comparisons, etc.) all assume every
operand already has a register color and use register-move helpers
directly - none of them fall through to `spilled-scalar-cfg`'s
`ix`-relative memory helpers. Safely mixing "homed value in a register"
and "spilled value via memory" within the same emission pass, per
operand, across every opcode case, is a correctness-sensitive rewrite of
comparable risk to the bug just fixed in Item A (a wrong fallthrough for
even one opcode case would silently corrupt a spilled value's load/store,
with the same kind of "runs fine until it doesn't" failure signature) -
this is exactly the class of change SKILL.md's risk ordering places above
"calls/PHIs" and below "large CFG/inline substitution."

**Decision**: defer Item C rather than implement it under this session's
remaining scope. Implementing and *validating* a mixed homed/spilled
emission path correctly requires the same falsifiable, per-opcode
hand-verification discipline Item A needed (structural audit already done
above; next is hand-verifying `spilled-scalar-cfg`'s existing memory
helpers against each of homed-scalar-cfg's ~15 opcode cases one at a time,
which is genuinely a full session's work on its own, not a continuation
of this one).

**Concrete starting point for the next session** (per the plan's own
guidance to re-derive rather than continue stale numbering, but this
audit is still directly reusable):

1. Confirm the exact "spilled-value" sentinel: `mir.allocation_colors[v] < 0`
   marks a spill (confirmed above); `mir.allocation_spills[v]` holds its
   assigned slot index once spilled (see `mir_summarize_allocation`,
   `mir.allocation_spills[value] = mir.allocation_spill_count++`).
2. For each opcode case in `mir_try_emit_homed_scalar_cfg`, decide per
   operand: if its color is a real register, keep the existing homed
   register-move code path; if its color is `< 0` (spilled), redirect to
   `spilled-scalar-cfg`'s existing `ix`-relative load/store helpers for
   that one value only. Do this one opcode at a time (e.g., start with
   `MIR_CALL`'s `MIR_ARG` operands only, since that is the concrete case
   this item was originally motivated by), hand-verifying against
   `spilled-scalar-cfg`'s output for 5-10 forced-accept candidates before
   moving to the next opcode.
3. Re-run the standard validation ladder (census --fail-on-regression,
   focused full, wide fast, full extended) after every single opcode is
   widened, not just once at the end - each opcode case is its own
   falsifiable unit and should be its own commit, matching this session's
   Item A discipline.

Item D remains not-yet-scoped, unchanged, and still gated on Item C
landing first.
