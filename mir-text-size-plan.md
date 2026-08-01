# dcc MIR text-size fallback plan

Scope: this document is solely about closing the `text-size` fallback
reason in `mir_try_emit_spilled_scalar_cfg` (`src/dcc/dcc_mir.c`), which
SKILL.md's "Known root cause" section already identified as **systemic,
not near-miss**: at the checkpoint measured 2026-07-30, `text-size`
accounted for 2,109/2,319 (91%) of all fallback functions, and re-measured
after Items 21-25 (this session) it is 1,756/1,833 (95.8%) of all
remaining fallback - unchanged in proportion by any opcode-admission work,
because it is not an opcode-recognition problem at all. It is a **code
quality problem in the one selector every non-MIR function goes through**.

It supersedes nothing else in `mir-migration-plan-to-100pct.md` (that
document's single-opcode-admission vein for `homed-scalar-cfg` is
separately closed out as of Item 25) - this is a new, narrower, deeper
document for one specific, much higher-leverage body of work.

## Method: re-derive root causes from evidence, do not assume the old finding still fully explains it

Re-ran the full census and re-bucketed the `text-size` population's
generated/captured byte ratio, fresh, before designing anything:

```sh
python3 scripts/mir-migration-census.py --output /tmp/census-full.tsv
```

```text
near (<1.15x):   36 /1756  (2.1%)
close (1.15-1.5x): 191/1756 (10.9%)
mid (1.5-2x):     653/1756 (37.2%)
far (>2x):        876/1756 (49.9%)
```

**This confirms and sharpens SKILL.md's 2026-07-30 finding**: the
population is not a handful of near-miss functions waiting for a small
polish - it is dominated (87% of the population) by functions generating
1.5x-5x+ as many bytes as legacy for equivalent logic. Two independent,
compounding root causes were found by direct assembly inspection of
representative functions across the ratio spectrum, described below.
Both are structural, both are already-partially-built machinery in
`dcc_mir.c` that is either incomplete or was landed switched off - this
is a "finish and correctly enable existing design," not a "build a new
mechanism from scratch," which was explicitly preferred per user
direction to favor a clean rewrite of the addressing/cost-quality
subsystem over further incremental opcode migration.

### Root cause A (near/close/mid population, ~34-115 bytes/function): unconditional boolean materialization + a dead-store class dccpeep doesn't remove

Already root-caused in SKILL.md (2026-07-30). Confirmed still present via
a fresh forced-accept diff of `check_s` (`tests/tesc.c`) and `dccpeep -Ot`
inspection:

```
call __scmp
pop bc
pop bc
ld (ix-2),l      ; <-- dead store: (ix-2)/(ix-1) is never read anywhere
ld (ix-1),h      ;     else in the function
ld a,h
or l
jp nz,L126
```

`dccpeep`'s existing same-basic-block redundant-reload pass already
proves the immediately-following `ld l,(ix-2)/ld h,(ix-1)` reload is
redundant (hl is untouched since the store) and removes it - but the
*store* itself survives, because proving it dead requires whole-function
scope (no other instruction anywhere else reads that exact slot), which
is a different, larger analysis than same-basic-block reload elimination.
`mir_emit_scalar_compare` (`dcc_mir.c` ~line 4939) unconditionally
materializes an explicit 0/1 boolean into a backend slot for every
comparison, even when the only consumer is the very next
`MIR_BRANCH_FALSE`/`MIR_BRANCH_TRUE`, which never needs a materialized
0/1 at all - it only needs the flag state (or a moved-but-untested HL/DE
value) at the point of the jump.

This affects **every comparison-then-branch shape reached in the general
CFG walker** (`if`, `while`, `for`, ternary conditions, `&&`/`||` chains) -
i.e., most functions with any control flow at all, which is why it
recurs at ~2-6% of a function's total size across the whole `near`
through `mid` population, not just a handful of functions.

### Root cause B (mid/far population, the dominant byte-count driver on large functions): no address caching for frame offsets beyond the Z80's signed 8-bit `(ix+d)` range

Found by direct inspection of `ti32`/`tui32` (~4.8x ratio, the single
worst gap in the corpus) and `tptrcnd`/`tptrlhs` (the two largest
absolute gaps, ~150-230KB). Any local/parameter/backend-spill-slot whose
frame offset falls outside `-128..+127` relative to `ix` cannot use the
2-instruction `ld l,(ix+d)/ld h,(ix+d+1)` direct form at all - `dcc_mir.c`
falls back, at **every single read or write**, to:

```
push ix
pop hl
ld de,<offset>
add hl,de
... ld/st through hl ...
```

6 instructions (9 bytes) of pure address recomputation, repeated
identically for every access to the same slot, versus 2 instructions (4
bytes) for an in-range direct access. For any function whose combined
local+spill-slot frame exceeds 127 bytes (common for anything with
several local arrays, many locals, or heavy spilling), essentially every
local access anywhere in the function pays this tax, which is exactly
why the byte-count ratio compounds to 3-5x on large functions - it is
not one bad instruction sequence, it's a per-access multiplier applied
across the whole function body.

**Legacy's own backend has a narrow, heuristic answer to a related but
different problem**: `dcc_symbols.c`'s `has_addr_cache`/
`maybe_reserve_addr_cache_for_array` caches a LOCAL ARRAY's address once
in a dedicated frame slot, but only for arrays whose bare-identifier
token count in the source clears an arbitrary threshold
(`ADDR_CACHE_MIN_COUNT`), and only if the function doesn't call
`exec()`/`execv()` (a frame-growth safety escape hatch for a real M80
"out of range" assembly failure hit once on `tarray6.c`). This is
exactly the kind of ad-hoc, name/token-count-driven, global-variable-based
logic explained by this codebase's history (the legacy backend was
migrated from an older streaming emitter to an AST-based one, which is
why it accumulated this style of per-symbol flag and global scratch
state rather than a clean, structurally-derived predicate) - it is not a
design to imitate.

**`dcc_mir.c` already has something better, half-finished and dormant.**
A complete, general "virtual IY frame base" mechanism already exists,
wired through ~10 call sites across `mir_try_emit_spilled_scalar_cfg`
(`mir_virtual_iy_base`, `mir_virtual_iy_offset()`,
`mir_emit_restore_virtual_iy()`, `mir_emit_virtual_iy_epilogue()`), all
introduced in one commit (`938c45b`, "make full rollout transactional and
regression-free"). Every read/write site already checks
`mir_virtual_iy_base` first and uses a small IY-relative offset when in
range, falling back to the existing ix-direct/full-recompute forms
otherwise - **but `mir_virtual_iy_base` is hard-set to `0` unconditionally
at both places it's ever assigned, and there is no third assignment
anywhere in the file that ever sets it to 1.** The mechanism was fully
designed (including a correct, `exx`-protected epilogue that restores the
caller's `iy` without clobbering the return value in `hl`, and a
defensive `mir_emit_restore_virtual_iy()` re-establishment after any call
whose callee isn't proven to be dcc-defined in this TU, mirroring the
exact same conservative "not proven safe" predicate already used
elsewhere for IY safety in `homed-scalar-cfg`) but never actually
switched on. This is the highest-leverage, lowest-new-code fix available:
finish wiring the missing "establish IY at function entry" prologue step
and the enabling condition, rather than design a parallel mechanism.

## Execution Log

### Item T1: enable the dormant virtual-IY frame base for large-frame functions

**Design** (deliberately a from-scratch, structural predicate - not a
port of legacy's array/token-count heuristic):

- Enable whenever `frame_bytes` (`local_bytes + aggregate_temp_bytes +
  2*backend_slot_count`, already computed before prologue emission) is
  greater than 150 (see threshold-refinement note below). This is both
  *necessary* (if the whole frame fits in 127 bytes, no offset can ever
  be out of `(ix+d)` range, so enabling would only add fixed overhead for
  zero benefit) and, with the 150 margin, *sufficient in practice* for
  the frame to contain at least one object whose access genuinely
  benefits enough to outweigh the new fixed prologue/epilogue cost.
- At the prologue, when enabled: `push iy` (saves the caller's `iy` in
  the exact stack position `mir_emit_virtual_iy_epilogue`'s existing math
  already expects - immediately below the just-reserved frame, matching
  its own `frame_bytes + 2` computation), then call the already-existing
  `mir_emit_restore_virtual_iy()` to set `iy = ix - (local_bytes +
  aggregate_temp_bytes)` - the boundary between the locals region and the
  backend-spill-slot region, so backend slots become small,
  bounded-by-slot-count positive `iy` offsets regardless of how deep
  locals push them relative to `ix`.
- Every read/write call site, the defensive post-call restore, and every
  epilogue/return path were already wired to `mir_virtual_iy_base` before
  this change and required no further edits - confirmed by full call-site
  audit (`grep -n "mir_virtual_iy_base"`) before implementing.

**Why this can only help or be neutral, never regress a function's
generated size below what it already produced**: every access site
checks `mir_virtual_iy_base && iy_offset in range` FIRST, falls back to
the pre-existing `ix_offset in range` check SECOND, and the original
6-instruction full recompute THIRD - so an access that was already
in-range for `ix` is unaffected, and one that wasn't stays exactly as
expensive as before if `iy` doesn't reach it either. The true cost this
item introduces is the new fixed prologue/epilogue overhead (`push iy` +
`mir_emit_restore_virtual_iy`'s 2-4 instructions at entry; the
`exx`-protected iy-restore sequence in `mir_emit_virtual_iy_epilogue`,
paid once per `return`/fall-off-the-end path) for every function crossing
the threshold - whether or not any of its individual accesses actually
land in `iy` range, and multiplied by however many return points the
function has. This means "frame_bytes > 127" alone is *not* sufficient
to guarantee never regressing an individual function - see below.

**Threshold-refinement finding (`tsprintf::main` regression)**: the
initial `frame_bytes > 127` threshold was validated with a full
whole-corpus raw byte-sum diff (not just the migration census's
acceptance-status-only pass/fail, which is blind to fallback-to-fallback
byte churn) and found exactly one regression: `tsprintf::main`
(`locals=128, slots=1, frame_bytes=130` - only 3 bytes over the
threshold) grew by +1095 bytes (11835 -> 12930). Root cause: with
`local_bytes=128`, every local's `ix` offset is already in the -1..-128
range (the boundary is inclusive, so 128 bytes of locals exactly fits),
so nothing about the locals region benefits from `iy`; only the single
2-byte backend slot (`iy` offset -2) newly benefits, saving a handful of
bytes per access - but `tsprintf::main` has multiple return points, and
each one pays the full `mir_emit_virtual_iy_epilogue` fixed cost, which
outweighs the one slot's savings. Raising the threshold margin to `> 150`
(instead of `> 127`) requires the frame to exceed the raw `(ix+d)` range
by a wider margin before enabling, which in practice ensures enough of
the frame (not just a single 2-byte slot) lies in `iy`-benefiting
territory to outweigh the fixed per-return-point cost. This is a pragmatic
safety-margin choice, not an exact pre-scan of per-access benefit vs. a
return-count-weighted fixed-cost estimate (which would be more precise
but requires a two-pass scan); re-running the full corpus byte-sum diff
after raising the threshold confirmed **zero regressed functions**
(previously 1) while 28 functions still improved and the corpus-wide
`generated_bytes` total still dropped by 761,245 bytes (a smaller total
than the `>127` variant's 790,597, trading a small amount of yield for
the correctness-invariant guarantee of never regressing a single
function - the appropriate trade per SKILL.md's non-negotiable rules).

**Validation performed**:

- Rebuild clean (only the pre-existing benign unused-parameter warning).
- Whole-corpus (2018 functions) raw byte-sum diff before/after: total
  `generated_bytes` 8,389,143 -> 7,627,898 (-761,245 bytes, -9.1%), 28
  functions improved, **0 functions regressed**.
- `scripts/mir-migration-census.py --compare ... --fail-on-regression`:
  0 newly MIR-emitted, 0 no-longer-emitted, 22 apps with census changes,
  0 apps requiring runtime validation - confirmed by direct inspection
  that no `result=mir` (actually-accepted) function's output changed at
  all; this item's effect today is entirely confined to still-fallback
  functions' captured diagnostic metrics, so it carries zero runtime risk
  to the current production build.
- Correctness of the new `iy`-relative addressing itself was independently
  verified by force-accepting a large representative function
  (`DCC_MIR_FORCE_ACCEPT_FUNCTION=ti32`, whose frame is well over the
  threshold) and running `scripts/runall.ps1 -Apps t -Mode full`: test
  passed (functional correctness intact); the reported "performance
  regressions" are expected artifacts of comparing forced-MIR output
  against the legacy perf baseline (unrelated to this fix, since `ti32`
  is not actually production-accepted).
- Wide safety net: `scripts/runall.ps1 -Mode fast` across all 323 apps:
  314 passed, 0 failed, 9 skipped, diagnostics/dccpeep/performance all
  passed.

**Outcome**: this item reduces the systemic frame-addressing overhead
(Root Cause B) across the `text-size` fallback population without
regressing any function or changing any currently-accepted function's
output. It does not by itself flip any function's accept/reject status
(no function crossed the acceptance threshold from this byte reduction
alone) - it narrows the gap for a future combined pass (with Root Cause A's
dead-store fix) to push more functions under the acceptance cost limits.
Committed and pushed to `origin/perf/unified-regalloc`.

**Next**: proceed to Root Cause A (the dead-store/boolean-materialization
fix in `mir_emit_scalar_compare` / the comparison-branch fusion path),
the other systemic multiplier identified in this investigation.

### Item T2: extend compare+branch fusion (Root Cause A) to 32-bit ("wide"/`long`) comparisons

**Finding**: a fresh full census after Item T1 (2018 functions, `text-size`
population still 1756/1673-non-inline) showed the same bucketed shape as
before (37 near, 194 close, 670 mid, 855 far), so Root Cause B's fix alone
didn't shift the population much. Investigating the worst outliers (after
excluding `static inline` functions, whose "captured" comparison is
apples-to-oranges since legacy inlines them at call sites) found
`tlimits::main` (6.07x ratio, 8541 vs 1408 bytes, frame only 22 bytes -
not a Root Cause B case) is dominated by `LONG_MIN`/`LONG_MAX`/`ULONG_MAX`
comparisons inside `if` conditions.

Root cause: `mir_binary_is_fusable_comparison` (Item 1/4's compare+branch
fusion, the actual fix for Root Cause A) unconditionally excluded any
comparison with `type_size(insn->secondary_offset) == 4` (32-bit operands,
i.e. `long`/`unsigned long`). Every wide comparison therefore paid the same
materialize-0/1-into-HL, spill-to-backend-slot, reload, retest-with-`ld
a,h/or l` round trip Item 1 eliminated for 16-bit operands - never fused,
regardless of feeding a branch directly.

**Fix**: removed the `type_size == 4` exclusion (keeping a `type_is_float`
exclusion, added defensively since the float 4-byte comparison helpers'
exact HL-return convention wasn't independently verified this session -
narrower scope than necessary but zero-risk). Added
`mir_emit_fused_wide_comparison_branch`: every wide comparison shape
(inline xor-compare for `==`/`!=`, and the `__ltu`/`__lts`/`__leu`/`__les`/
`__lgu`/`__lgs`/`__lku`/`__lks` runtime helpers for relational operators)
already leaves a concrete 0/1 boolean in HL by construction - there's no
flag-based shortcut available (unlike the 16-bit path's `sbc hl,de`), but
skipping the store/reload/retest is still a direct, safe win. Unlike the
16-bit fusion's `mir_negate_comparison_operator` (which re-derives which
CPU flag corresponds to "true" for the negated operator), the wide path
tests the *already-computed* boolean of the original (non-negated)
operator directly: true-branch condition is `nz` when there's no
intervening `!`, or `z` when there is (since `!x` inverts which boolean
value means "branch taken").

**Validation**:
- Whole-corpus byte-sum diff: total `generated_bytes` 7,627,898 ->
  7,611,255 (-16,643 bytes), 153 functions improved, 7 functions apparently
  "regressed" by 1-53 bytes each.
- Investigated the largest apparent regression (`tlongopt::test_shift_edges`,
  +53 bytes) directly: `mir_stream_size()` (the source of the
  `generated_bytes` metric) returns the raw *text* byte length of the
  emitted `.mac` stream (`ftell`), not an assembled machine-code byte
  count - this matches SKILL.md's caution that text/instruction-count size
  is not proof of real code size. A label-number-normalized diff
  (`sed 's/L[0-9]*/LBL/g'`) of the function's own emitted instructions
  showed **zero actual differences** - the reported delta is solely from
  this item's new `fallthrough_label` allocations shifting later label
  numbers across a decimal-digit-width boundary (e.g. `L999` -> `L1000`)
  elsewhere in the same translation unit, inflating raw text size by a
  few characters with no real instruction change. Not a genuine
  regression, and since none of the affected functions are
  currently-accepted (`result=mir`), this proxy-metric noise has zero
  effect on any actual compiled `.COM` output.
- `mir-migration-census.py --compare --fail-on-regression`: 0 newly
  MIR-emitted, 0 no-longer-emitted, 87 apps with census changes, 0 apps
  requiring runtime validation - confirmed no `result=mir` function's
  output changed.
- Correctness of the new wide-fusion emission verified via
  `DCC_MIR_FORCE_ACCEPT_FUNCTION=main` on `tlimits` (heaviest wide-compare
  user) with `runall.ps1 -Mode full`: test passed; reported perf deltas
  are the same expected force-accept-vs-legacy-baseline artifact seen in
  Item T1's validation.
- Wide safety net: `runall.ps1 -Mode fast`, 323 apps: 314 passed, 0
  failed, 9 skipped, diagnostics/dccpeep/performance all passed.

**Outcome**: closes the remaining `type_size == 4` gap in Root Cause A's
fusion for integer comparisons; further narrows the `text-size` gap
without touching any currently-accepted function. Committed and pushed to
`origin/perf/unified-regalloc`.

**Next**: float wide comparisons (`__feqf`/`__fnef`/etc.) were
conservatively excluded from this fusion pending independent verification
of their HL-return convention; a small follow-up could extend Item T2 to
them once verified. Otherwise, continue re-bucketing the `text-size`
population after T1+T2 to find the next dominant repeated pattern (the
distribution is still mid/far-dominated, suggesting more per-access or
per-call overhead classes remain to be found, not a single remaining
outlier).

## Root Cause C (discovered post-T2): general single-use immediate store/reload is not elided

Investigating the next round of worst `text-size` ratio outliers (post
T1+T2, non-`static inline` functions) surfaced a pattern much larger in
scope than Root Cause A's comparison-specific fix: `tfarrsub::set_direct`
(a single `ds.arr[ds.n] = (unsigned char)val; ds.n++;` statement) emits a
raw MIR body dominated almost entirely by `ld (ix-N),l / ld (ix-N+1),h`
immediately followed by `ld l,(ix-N) / ld h,(ix-N+1)` pairs - i.e. every
computed address/value is stored to its "home" backend slot and then
reloaded from that exact same slot for its one and only use, with zero
intervening instructions that could have required spilling it in the
first place.

This is not unique to comparisons - `mir_try_emit_spilled_scalar_cfg`'s
architecture gives every MIR value a fixed home slot and unconditionally
stores to it after every definition (`mir_emit_virtual_store`, 34 call
sites), then reloads from that home for every use, regardless of whether
the value's one use immediately follows its definition with nothing in
between. Root Cause A's Items 1/4/T2 fusion is really a narrow special
case of this same waste, hand-solved only for comparison results feeding
a branch.

**Verified this is not just a diagnostic/force-accept artifact**: running
`dccpeep -Ot` (the real production peephole pass) against the forced-accept
`.mac` for `tfarrsub` reduced total file size by only 2.3% (16726 -> 16339
bytes) - `dccpeep`'s existing passes (`fold_hl_base_const_offset`,
`ix_pair_load_to_de`, etc.) catch a few specific shapes but do not
generally eliminate this store-immediately-reload pattern. This means the
waste substantially survives into whatever the real compiled output would
be for any function using this selector, not just an artifact of the
un-peepholed diagnostic dump.

**Why this is a bigger, riskier lift than T1/T2** (not attempted this
session): a general fix requires tracking, at the point of a value's
single use, whether *no* intervening MIR instruction has been emitted
since its definition (not just "next MIR instruction in program order" -
any codegen-visible side effect, call, or control-flow point in between
invalidates keeping it live in HL/DE without a real spill). This is
effectively local value forwarding/copy-propagation across `mir_emit_*`
call boundaries, touching most of the 34 `mir_emit_virtual_store` call
sites and their paired loads - a fundamentally larger and more
correctness-sensitive change than the narrow, single-opcode-scoped fusion
work in Items 1/4/T2, and needs its own careful incremental design (most
likely: extend the existing "last computed value" register-forwarding
idea already proven safe for compare+branch to the general single-def/
single-use-immediately-following case) rather than a same-session
extension.

**Recommended next step for a future session**: design and implement this
as its own item, starting from the narrowest safe slice (e.g. a MIR_CONST
or address computation whose dst has `mir_value_use_count == 1` and whose
one use is the *literal next* MIR instruction with no MIR_CALL, MIR_LABEL,
or branch between them - the same "no side effect could have happened"
invariant Items 1/4/T2 already lean on) and validate with the same
whole-corpus byte-sum-diff + census + full-mode discipline used for T1/T2.
Given the file-wide 34-site touch surface, expect this to be the largest
remaining `text-size` win available and to warrant its own dedicated,
carefully-staged multi-item plan rather than a single commit.

### Item T3: fix dead `mir_virtual_iy_base` gate on general single-use HL forwarding (Root Cause C)

**Root cause confirmed via git blame**: `mir_can_forward_hl_to_next`'s
literal-adjacent-instruction case required `mir_virtual_iy_base` to be
true before allowing forwarding into any consumer opcode other than
`MIR_RETURN`/`MIR_STORE`. Commit `164ae0e` ("MIR: forward binary/unary/
divmod results to a following store") explicitly documented
`mir_virtual_iy_base` as "dead scaffolding for an unimplemented IY-relative
frame base" at the time and special-cased only `MIR_STORE` to route around
it - but left `MIR_MEMBER_ADDRESS`, `MIR_INDEX_ADDRESS`, `MIR_LOAD_INDIRECT`,
`MIR_UNARY`, and 16-bit `MIR_BINARY` consumers still gated behind the same
dead flag. Since Item T1 (this session) gave `mir_virtual_iy_base` a real,
non-constant value, this dead gate came back to life for large frames only
- exposing exactly how much general single-def/single-use forwarding was
being denied for every function *not* crossing T1's threshold (the vast
majority of the corpus). Confirmed via a direct MIR dump of
`tfarrsub::set_direct` (`ds.arr[ds.n] = val; ds.n++;`, frame only 6 bytes):
half its emitted body was store-immediately-reload pairs for values with
exactly one, immediately-following use through `MIR_MEMBER_ADDRESS`/
`MIR_INDEX_ADDRESS`/`MIR_LOAD_INDIRECT` - opcodes the switch below the gate
already explicitly allowlists, just unreachable for small frames.

**Fix**: removed the `mir_virtual_iy_base` condition from the
literal-adjacent-instruction branch of `mir_can_forward_hl_to_next` -
the switch statement immediately below it is already the real,
opcode-precise safety gate (`MIR_INDEX_ADDRESS`, `MIR_MEMBER_ADDRESS`,
`MIR_LOAD_INDIRECT`, `MIR_UNARY`, 16-bit `MIR_BINARY`, narrow
`MIR_STORE_INDIRECT`/`MIR_STORE`, `MIR_RETURN`, `default: return 0`),
so the flag added no independent correctness value - it only ever
existed because the flag itself used to be permanently 0. Verified on
`set_direct`: 1048 -> 788 generated bytes (-25%) on that one function
alone.

**Threshold-interaction regression found and fixed**: the first full
corpus validation surfaced a severe regression in
`tptrlhs::touch_ptr_to_array_deref` (6469 -> 11782 bytes, +82%, 615 ->
1198 generated instructions). Root cause: this fix reduced the function's
backend-slot count (fewer values need real spill slots now that more are
forwarded), which dropped its `frame_bytes` from 151 to 149 - crossing
back *below* Item T1's `frame_bytes > 150` threshold and disabling the
virtual-IY mechanism entirely for a function whose 141-byte `locals`
region alone already has several genuinely out-of-`(ix+d)`-range objects
(unrelated to backend slots). This exposed that T1's `> 150` margin,
chosen to fix one specific near-boundary case (`tsprintf::main`, which
needs the threshold to stay *above* 130), was not automatically safe for
every shape of large-frame function - a single global byte-count
threshold cannot distinguish "marginal, not worth the fixed overhead"
frames from "substantial real out-of-range traffic" frames campaigned
similarly close to the cutoff. Since `tsprintf::main` requires the
threshold to stay above 130 and `touch_ptr_to_array_deref` requires it
to stay at or below 149, lowered Item T1's threshold from `150` to `140`
(comfortably between both known constraints) and re-validated the full
corpus: the severe regression is gone, and the two remaining sub-10-byte
"regressions" reported by the raw byte-sum diff (`forint::parse_source`
+7, `tnestfor::main` +2) were confirmed, via a label/offset-normalized
diff showing byte-for-byte identical instruction counts, to be the same
text-length-metric digit-width artifact already documented for Item T2
(here from backend-slot offset numbers shifting digit width, e.g.
`(iy-4)` vs `(iy-10)`, rather than label numbers) - not real code-size
changes. This class of threshold fragility is a known, accepted
limitation of the current single-cutoff design (see Item T1's own
Execution Log); a fully robust fix would replace the cutoff with an
exact per-object/per-slot range pre-scan, left as documented future work
rather than attempted in this pass given the two known constraints are
already satisfied with margin.

**Validation**:
- Whole-corpus byte-sum diff (2018 functions): total `generated_bytes`
  7,611,255 -> 6,917,821 (**-693,434 bytes**, -9.1% further reduction on
  top of T1+T2's prior -777,240), **979 functions improved**, 0 real
  regressions (2 sub-10-byte digit-width artifacts, confirmed non-real).
- `mir-migration-census.py --compare --fail-on-regression`: **5 newly
  MIR-emitted functions** (`tc99apar::read_paren_const/restrict/volatile`,
  `tinlinfb::pair_right`, `too::shape_scale`) - this fix widens the
  acceptance gate as a side effect of removing dead-code waste, not a
  gate change of its own; 237 apps with census changes; 4 apps required
  runtime validation (`tc89size`, `tc99apar`, `tinlinfb`, `too`).
- Focused `runall.ps1 -Apps tc89size,tc99apar,tinlinfb,too -Mode full`:
  all 4 apps passed correctness. `tc99apar` and `too` showed small
  (+0.42%, +0.34%, +0.01%, +0.01%) cycle-count increases from the newly
  admitted functions - an expected, understood trade-off of the
  size-only acceptance gate (SKILL.md's existing, pre-established
  pattern for every prior newly-admitted function in this migration),
  confirmed correctness-clean and fully explained (not hidden), so
  baselines were updated for exactly these two apps per baseline policy.
  `tc89size` improved (-0.06%); no baseline change needed there.
- Wide safety net: `runall.ps1 -Mode fast`, 323 apps: 314 passed, 0
  failed, 9 skipped, diagnostics/dccpeep/performance all passed.

**Outcome**: the single largest win of this session's text-size work -
fixes a dead-code gate that was silently blocking a large fraction of
the corpus from an already-implemented, already-safe general
single-use-forwarding optimization. Combined T1+T2+T3 total corpus
reduction so far: -1,470,674 bytes (8,389,143 -> 6,917,821 initial to
current, ~17.5%). Committed and pushed to `origin/perf/unified-regalloc`.

**Next**: `mir_can_forward_stack_to_index` (the sibling stack-forwarding
helper) still has its own `mir_virtual_iy_base` entry gate - this one
was *not* touched this session since its logic actually references stack
push/pop sequencing tied to the mechanism's own bookkeeping (lower
confidence it is dead scaffolding rather than a genuine dependency); a
future session should audit it the same way (git blame + a forced-accept
probe) before deciding whether to extend it too. Root Cause C's broader
"general redundant store/reload elision" investigation is substantially
addressed by this fix for the single-adjacent-use case; remaining waste
(non-adjacent single uses separated by a skippable label, or values with
more complex live ranges) is a smaller residual, lower-priority next
target.

## Refactor: split `dcc_mir.c` into six files (2026-08-01)

`dcc_mir.c` had grown to 12,494 lines (nearly 2.5x the next-largest
source file, `dcc_ast_gen_expr.c` at 5,949 lines) purely from this
migration's accumulated selector/helper growth. Not a functional item,
but flagged by the user as increasingly unwieldy to navigate/edit.

**Approach**: mapped the file's 239 top-level declarations by line
range and identified six natural, low-coupling architectural
boundaries: (1) core lowering/capture-API/CFG-dataflow/register-
allocation, (2) shared scalar-value emission helpers + the DAG
selectors, (3) `mir_try_emit_homed_scalar_cfg` (822 lines, one
function), (4) `mir_try_emit_spilled_scalar_cfg` (1,460 lines, the
dominant selector) plus its exclusive helpers, (5) loop selectors +
the top-level dispatcher + `mir_end_function`. Verified cross-file
usage precisely with a comment/string-literal-stripped identifier scan
(catching two classes the first pass missed: functions whose return
type didn't match a naive regex, e.g. `const struct MirInsn *`, and a
file-scope global, `mir_spilled_scalar_cfg_elided_epilogue_bytes`, used
only across two of the five files).

**Result**: `dcc_mir.c` (4,953 lines, core), `dcc_mir_emit_common.c`
(1,175 lines), `dcc_mir_homed_cfg.c` (838 lines),
`dcc_mir_spilled_cfg.c` (3,990 lines), `dcc_mir_select.c` (1,383
lines), and a new `src/dcc/dcc_mir_internal.h` (335 lines) holding the
shared `struct MirInsn`/`struct MirFunction`/`enum MirOpcode`/`enum
MirPhysicalColor` types, the shared file-scope state (`mir`,
`mir_virtual_iy_base`, the HL/stack/call forwarding caches, etc., now
non-`static` with `extern` declarations), and prototypes for the ~67
helpers/globals that are defined in one of the five `.c` files and used
from another. This header is internal to the MIR backend split (not
part of the public `dcc_mir.h` API consumed by the rest of the
compiler). `build-dcc.sh` needed no changes since it already globs
`./*.c`.

**Validation** (pure code motion, so the bar is byte-identical output,
not just non-regressing):
- Clean rebuild: 1 pre-existing warning only (`mir_emit_wide_home_to_hl_de`'s
  unused `out` parameter - confirmed present in the original single-file
  build too via `git stash`, not newly introduced).
- Full-corpus census, before (original single-file binary) vs. after
  (split binary), `--fail-on-regression`: **0 newly-emitted, 0
  no-longer-emitted, 0 apps with any census-metric change** - proves
  the split changed no generated bytes, instruction counts, or
  selector outcomes anywhere in the 2021-function corpus.
- `runall.ps1 -Mode fast`, 323 apps: 314 passed, 0 failed, 9 skipped,
  diagnostics/dccpeep/performance all passed.
- `runall.ps1 -Mode full` (fast + nopeep), 323 apps: 314 passed, 0
  failed, 9 skipped, diagnostics/dccpeep/performance all passed.

**Outcome**: safe, verified-behavior-identical refactor. Future
sessions should edit the specific `dcc_mir_*.c` file that owns a given
selector/helper rather than re-growing a single monolithic file, and
add any new cross-file prototypes to `dcc_mir_internal.h`.

## Item T4: dead `mir_virtual_iy_base` gate on stack-forwarding (2026-08-01)

**Hypothesis**: `mir_can_forward_stack_to_index` (its sibling,
`mir_can_forward_hl_to_next`, was fixed in Item T3) has the same class
of bug - a `!mir_virtual_iy_base` entry gate that was dead scaffolding
when introduced and has come back to life, wrongly, for large frames
only.

**Investigation**: `git log -S "mir_can_forward_stack_to_index"`
showed this helper and its `mir_virtual_iy_base` gate were both
introduced in the same commit, `938c45b` ("make full rollout
transactional and regression-free"), which also set
`mir_virtual_iy_base = 0;` (a hard-coded constant) at both of that
commit's own assignment sites - i.e. the gate was unconditionally false
at introduction, exactly like the `mir_can_forward_hl_to_next` case in
Item T3. The optimization itself is a self-contained physical `push
hl` / `pop hl-or-de` handoff spanning a fixed 2-instruction window
(`MIR_CONST` then `MIR_INDEX_ADDRESS` using this value as the array
base) with a use-count check ruling out any call or repeat use in
between - nothing about it depends on whether the eventual store
destination uses `ix`- or `iy`-relative addressing. Session earlier
than this one's Item T1 gave `mir_virtual_iy_base` a real per-function
value (`frame_bytes > 140`), which reawakened this dead gate for large
frames only, silently denying the optimization to the ~93% of
functions with smaller frames.

**Fix**: removed the `!mir_virtual_iy_base` condition from
`mir_can_forward_stack_to_index`'s entry gate in
`src/dcc/dcc_mir_spilled_cfg.c`, leaving the existing
opcode/use-count checks as the sole safety gate (same shape as Item
T3's fix).

**Validation**:
- Whole-corpus census before/after (`--fail-on-regression`): 0
  newly/no-longer-emitted functions, 0 regressions - every one of the
  230 changed functions got *smaller* (`-71,913` bytes total across
  135 apps); the worst single-function "delta" was actually an
  improvement (`-35` bytes, several apps), i.e. there were no positive
  deltas at all.
- `apps requiring runtime validation: 0` - since no already-*accepted*
  MIR function's generated output changed (only still-`text-size`-
  fallback candidates got smaller without crossing the acceptance
  threshold), no already-active MIR selection changed at all, so this
  change cannot affect any currently-shipped Z80 output.
- Wide safety net: `runall.ps1 -Mode full` (fast + nopeep), 323 apps:
  314 passed, 0 failed, 9 skipped, diagnostics/dccpeep/performance all
  passed.

**Outcome**: -71,913 bytes across the corpus, 0 regressions, 0
baseline changes needed (no already-active function's output changed).
Coverage unchanged at 190/2021 (9.40%) - this narrows the size gap for
still-rejected candidates without flipping any across the acceptance
threshold this time.

**Next**: re-run the full census and re-bucket the `text-size` gap
fresh (population shifts with every item); investigate Root Cause C's
remaining residual (non-adjacent single-use values, or values with
more complex live ranges than the single-adjacent-use case Items T3/T4
now cover).

## Item T5: unrolled byte-by-byte aggregate-return copy (2026-08-01)

**Hypothesis**: a worst-ratio-outlier sweep of the fresh `text-size`
population (bucketing by `generated_bytes / captured_bytes`) surfaced
`tstructv::return_big_ptr` (`static struct Big return_big_ptr(struct
Big *src) { return *src; }`, `struct Big` = `char data[40]; int tag;`,
42 bytes) at 8.93x - 1723 generated bytes against only 193 captured,
for a MIR stream of just 4 instructions (`label`/`param`/`load`
(aggregate)/`return`). A 4-instruction stream producing a
177-instruction candidate pointed at the emitter, not the source
shape.

**Investigation**: `mir_try_emit_spilled_scalar_cfg`'s `MIR_RETURN`
case (`dcc_mir_spilled_cfg.c`) handles a struct-typed return value by
fully unrolling a byte-by-byte copy: `ld a,(de) / ld (hl),a` per byte,
plus `inc de / inc hl` between bytes - for `struct Big`'s 42 bytes,
that is 42 x ~3-4 instructions of copy body alone, all written out in
the assembly text (hence the outsized `generated_bytes`, which counts
assembly-source characters, not just resulting machine-code bytes -
but the *machine code* is bloated too, since each unrolled iteration
literally re-emits its own 4 instructions rather than looping).
Comparing against the legacy backend's own equivalent path
(`emit_copy_de_to_hl_bytes`, `dcc_expr.c`, used by `gen_return_ast` for
the exact same struct-return case) showed legacy never unrolls either:
it emits a single `djnz`-based runtime loop (`ld b,n` + a 4-instruction
loop body + `djnz`), producing a fixed ~9 machine bytes regardless of
struct size - this is why `captured_bytes` stayed small (193) while
the MIR candidate exploded with struct size.

**Fix**: replaced the unrolled byte-by-byte copy in the `MIR_RETURN`
struct-object case with the Z80 `ldir` block-copy instruction: HL
already holds the source address (from the existing
`mir_emit_virtual_load` call), so DE is loaded directly from the hidden
return-buffer pointer parameter (`ld e,(ix+4)` / `ld d,(ix+5)`, no
longer routed through HL via `ex de,hl` first) and `ld bc,<size>` /
`ldir` performs the whole copy in one runtime block-copy instruction.
This is smaller than either the old unrolled form *or* legacy's own
`djnz` loop (`ldir` needs no loop-body instructions at all), faster
(21 T-states/byte vs. the `djnz` loop's ~30+), and is the same,
already-trusted idiom used pervasively for block copies throughout
`DCCRTL.MAC` (11+ occurrences) - not a new, unproven code shape.

**Validation**:
- Whole-corpus census before/after (`--fail-on-regression`): 0
  regressions, 0 no-longer-emitted; **5 newly MIR-emitted functions**
  (`tclit.echo_pair`, `tstruct.ret_arr_elem`,
  `tstructv.return_big_ptr`, `tstructv.return_pair_ptr`,
  `tstructv.return_wrap_ptr`) - every struct-by-value-return function
  in the corpus small enough to be otherwise eligible flipped straight
  from `fallback text-size` to accepted. Coverage moved 190/2021
  (9.40%) -> 195/2021 (9.65%). 10 apps had census changes; 3 required
  runtime validation (functions whose MIR output is now actually
  shipped).
- Focused `runall.ps1 -Apps tclit,tstruct,tstructv -Mode full`: 3/3
  passed, 0 regressions, **6 genuine performance improvements** (not
  just static-metric noise): tstruct -0.01% both peep/nopeep cycles,
  tclit -0.1%/-0.11%, tstructv -0.3%/-0.3% peep/nopeep cycles and
  smaller `.COM` sizes - accepted via `-UpdatePerfBaseline` (only for
  these 3 apps, only because full-mode proved the improvement is real
  and correctness-clean, per baseline policy).
- Wide safety net `runall.ps1 -Mode fast`: 314/323 passed (9 skipped,
  as usual), 0 failed, diagnostics/dccpeep/performance all passed.

**Outcome**: +5 functions accepted (190 -> 195, 9.40% -> 9.65%), 0
regressions, real (not just static) performance wins on every affected
app. This is a **reusable emitter fix**, not a one-off: every
struct-by-value return in the corpus now takes the compact `ldir`
path, so any future struct-returning function under the `text-size`
threshold benefits automatically, and larger struct-returning
functions still on `fallback text-size` are now much closer to the
threshold (smaller `generated_bytes` even where not yet flipped).

**Next**: this opens a **new structural class** worth surveying
directly - struct assignment/copy sites in general (not just `return`)
may share the same unrolled-copy defect. `dcc_expr.c`'s
`emit_copy_de_to_hl_bytes` is the legacy convention already used by
struct assignment (`dcc_ast_gen_expr.c` lines ~1825, ~4831-4878) -
check whether `dcc_mir_spilled_cfg.c`/`dcc_mir_emit_common.c` have
equivalent unrolled-copy sites for struct assignment (not just struct
return) and apply the same `ldir` fix there if so.

## Item T6: struct-copy/assignment sites beyond `return` (2026-08-01)

**Hypothesis** (T5's own "Next" note): the same unrolled
byte-by-byte copy defect T5 fixed for `MIR_RETURN`'s struct-object
case plausibly exists at other struct-copy/assignment sites, since
legacy shares one `emit_copy_de_to_hl_bytes` helper across both the
return path and assignment paths (`dcc_ast_gen_expr.c` lines ~1825,
~4831-4878), but the MIR backend implements each site independently.

**Investigation**: grepped `dcc_mir_spilled_cfg.c` and
`dcc_mir_homed_cfg.c` for the same unrolled-copy pattern (`ld a,(de)` /
`ld (hl),a` / `inc de` / `inc hl` loops) T5 removed, and found 5 more
occurrences of the identical bug:
- `dcc_mir_spilled_cfg.c` `MIR_STORE`'s struct-object case (struct
  assignment to a global/local/param destination).
- `dcc_mir_spilled_cfg.c` `MIR_COPY_AGGREGATE` (general struct
  assignment, `a = b;`).
- `dcc_mir_spilled_cfg.c` `MIR_CALL`'s struct-argument-copy case
  (passing a struct byval argument onto the stack).
- `dcc_mir_spilled_cfg.c` `MIR_CALL_AGGREGATE`'s struct-argument-copy
  case (same, for calls returning a struct).
- `dcc_mir_homed_cfg.c` `MIR_COPY_AGGREGATE` (the homed-scalar-cfg
  selector's equivalent struct-copy case).

**Fix (landed)**: applied the identical `ldir` fix from T5 to the
first 3 of these 5 sites - `MIR_STORE`, `MIR_COPY_AGGREGATE` in
`dcc_mir_spilled_cfg.c`, and `MIR_COPY_AGGREGATE` in
`dcc_mir_homed_cfg.c`. Each required a register-swap variant tailored
to how source/destination already flow through existing code (an
extra `ex de,hl` where the code already naturally swaps, or a
`push`/`pop` pair where destination is computed via `push ix/pop hl`
after `mir_emit_virtual_load`/`mir_emit_home_to_hl` loads the source),
always preserving the original call order of
`mir_emit_virtual_load`/`mir_emit_home_to_hl` relative to any
HL-clobbering address computation (these functions have an internal
HL-forwarding fast path keyed on instruction adjacency - reordering
would silently load stale/wrong data, not just cost performance).

**Fix (found but reverted - deferred)**: the `MIR_CALL` and
`MIR_CALL_AGGREGATE` struct-argument-copy sites also had the exact
same unrolled-copy bug and were initially fixed the same way. Doing so
caused `tsretret.make_normal` (`struct Pair result =
make_pair(first, second); return normalize(result);`) to cross the
`text-size` acceptance threshold as a **new** MIR-emitted function -
but `runall.ps1 -Mode full` showed it **regressed** real cycle counts
(peep +0.03%, nopeep +0.2%), violating SKILL.md's Rule 3 (peep and
nopeep must both be non-regressing for newly emitted functions).
Root-caused via a stash-based before/after A/B test (per SKILL.md
step 9): reverted to the pre-fix unrolled form, force-accepted
`make_normal` via `DCC_MIR_FORCE_ACCEPT_FUNCTION`, and re-measured -
the regression was present **and slightly worse** without this fix
(peep 61504/nopeep 62481 vs. 61485/62462 with the `ldir` fix). This
proves the `ldir` fix itself is not the cause; it's a **latent,
pre-existing defect** that this fix's byte reduction merely exposed by
letting the function cross the acceptance threshold for the first
time. Assembly/MIR inspection of `make_normal` found the real cause:
the address of the local struct `result` is recomputed **three
separate times** in the generated code (`push ix / pop hl / ld
de,-8 / add hl,de`, 4 instructions each) - once for `make_pair`'s
hidden destination-pointer setup, and twice more immediately after
(once for `MIR_ADDRESS`'s own store-to-spill-slot emission, and again
when the struct-argument-copy code reloads that same address value
from scratch via `mir_emit_virtual_load` rather than reloading the
just-written spill slot). This matches the already-documented,
deferred **Root Cause C** class below (general single-use
immediate store/reload not elided) - it is not a new bug, and not in
scope for this item's quick fix.

**Decision**: reverted just the `MIR_CALL`/`MIR_CALL_AGGREGATE`
struct-argument-copy sites back to their original unrolled form (the
other 3 sites are unaffected by this regression - `make_normal`'s MIR
stream doesn't use `MIR_STORE`, and its `MIR_COPY_AGGREGATE` calls, if
any, aren't census-visible as changed). This is deferred, not
abandoned: once Root Cause C's residual (below) is fixed - eliminating
the redundant address recomputation - it should become safe to
re-apply the call-argument `ldir` fix without regression, since the
`ldir` fix's own contribution was proven to *improve*, not worsen,
`make_normal`'s cycles in isolation.

**Validation** (final, post-revert):
- Whole-corpus census before/after (`--fail-on-regression`): 0
  regressions, 0 no-longer-emitted, **0 newly-emitted** (confirms the
  revert successfully avoided flipping `make_normal` or any other
  function to acceptance - the 3 landed sub-fixes only shrink
  `generated_bytes` within functions still on `fallback text-size`).
  Coverage unchanged at 195/2021 (9.65%). 8 apps had census changes
  (`fint`, `tclit`, `tgnarly`, `tptrlhs`, `tstruct`, `tstructp`,
  `tstructv`, `tunion2`); 0 apps required runtime validation (no
  already-shipped MIR output changed - safe by construction).
- Focused `runall.ps1 -Apps fint,tclit,tgnarly,tptrlhs,tstruct,tstructp,tstructv,tunion2 -Mode full`:
  8/8 passed, 0 regressions.
- Wide safety net `runall.ps1 -Mode fast`: 314/323 passed (9 skipped,
  as usual), 0 failed, diagnostics/dccpeep/performance all passed.
- No baseline update needed (no already-shipped app's output or
  performance profile changed).

**Outcome**: 0 functions newly accepted this item (coverage holds at
195/2021, 9.65%), 0 regressions. 3 of 5 surveyed struct-copy sites
landed the `ldir` fix (shrinking `generated_bytes` broadly across the
still-fallback population, moving many struct-copy/assignment-heavy
functions closer to the `text-size` threshold for a future item to
flip). The remaining 2 sites (`MIR_CALL`/`MIR_CALL_AGGREGATE`
struct-argument-copy) are a **documented defer**, same discipline as
Item 6's precedent: a valid, proven-beneficial-in-isolation fix exists
but is withheld because it exposes an unrelated, pre-existing
Root-Cause-C-class bug in the one function it currently affects: this
is not a rejection of the fix, just a sequencing dependency on Root
Cause C landing first.

**Next**: proceed to the comparison-fusion project (this plan's Root
Cause 1, selector-side) as the next highest-yield item, or continue
Root Cause C's residual directly - fixing Root Cause C first would
also unblock re-applying the deferred call-argument `ldir` fix as a
bonus.

## Item T7: call-result HL-forwarding across the store/reload gap (2026-08-01, deferred)

**Hypothesis**: SKILL.md's own root-cause finding names `check_s`
(`if (strcmp(got, expected) != 0) fail_s(...);`) as a representative
example of unconditional boolean materialization. Direct inspection
this session found the *actual* remaining gap is narrower than
originally framed: `mir_binary_is_fusable_comparison` (Items 1/2/4/25/
27, already landed in a prior phase of this migration) already fully
elides the 0/1 boolean materialization for `check_s`'s compare - the
generated code tests the `strcmp` call's raw HL result directly with
`or l` and branches, exactly as hoped. The remaining 34-byte gap
against legacy is a *different*, narrower defect: the call result
itself (`v5 = call strcmp`) is still stored to a backend slot
immediately after the call and reloaded one instruction later purely
because `mir_can_forward_hl_to_next` has a blanket
`definition->opcode == MIR_CALL` exclusion, and because
`mir_forward_skip_target`'s existing "skip a silent instruction"
mechanism (already used for NOPs and single-predecessor labels) doesn't
know a `MIR_CONST` sitting between the call and its one use can also be
silent (elided entirely elsewhere whenever it is only ever consumed as
one narrow comparison operand, a call argument, or a small multiplier).

**Investigation**: implemented a two-part fix - (1) drop the
unconditional `MIR_CALL` exclusion in `mir_can_forward_hl_to_next`
(keeping `MIR_CALL_AGGREGATE` excluded, since its result is an address
via a hidden return pointer, a materially different case not
investigated here); (2) extend `mir_forward_skip_target` to also skip
a `MIR_CONST` instruction proven to emit no code of its own (reusing
`mir_call_only_constant`/`mir_binary_only_constant`/
`mir_multiply_by_small_constant` - the exact predicates the real
emission loop already uses to decide whether that constant's own
`MIR_CONST` case emits anything). Neither change alone nor combined
moved `check_s` off `fallback text-size`, because a *third*, older
check in `mir_can_forward_hl_to_next` (`next_instruction !=
mir_emit_instruction_index + 1 && next->opcode != MIR_RETURN`) rejects
any skip target that isn't the literal next array position (except for
`MIR_RETURN`, explicitly carved out) - meaning Item 15's own
single-predecessor-label skip capability, and any NOP run longer than
one, appear to suffer this exact same dead-on-arrival problem today
(both call sites of `mir_forward_skip_target` re-apply this same
equality check). Confirmed via a whole-corpus census
(`--fail-on-regression`) with parts (1)+(2) alone: 0 regressions, 0
newly-emitted, 0 no-longer-emitted, 57 apps with (harmless, still-
fallback-only) census metric changes, 0 apps requiring runtime
validation - i.e. no measurable win materializes without also
loosening that third equality check.

**Decision to defer, not implement the full chain**: `mir-migration-
plan-100.md`'s own Item 14 (2026-07-30) already investigated an
adjacent form of this exact idea (forwarding a call-argument-cached
value across a call boundary) and deferred it, citing a documented
"occupancy-safety" hazard: whether a value can safely be assumed to
still be resident in a register at a given emission point depends on
dynamic, emission-order state that a static one-pass accounting scan
(`mir_prepare_backend_slots`) cannot always prove matches what the
real emitter does later, without duplicating that state machine - "the
two-divergent-paths hazard the repo's own Item 19 discriminator warns
about (documented root cause of a prior stack-corruption bug)". Item
16 (also 2026-07-30) directly examined `check_s`'s pattern and
classified it as "structurally identical" to Item 14's hazard, and
also deferred without attempting even the narrow call-exclusion
removal tried here. This session's experiment is a genuine, evidence-
based attempt to test whether the *narrower* slice (just the
`MIR_CALL` forwarding case, not the argument-cache mechanism Item 14
was actually about) is safe on its own - but since it requires
*also* loosening the equality-check gate (a third, independently
untested change with its own blast radius across every existing NOP/
label-skip site) before it produces any measurable effect, the
combined change is no longer the narrow, single-concept edit this
migration's discipline calls for. Rather than stack three
compounding, previously-flagged-risky changes together to chase one
34-byte gap, this is deferred - same rationale class as Item 6's and
Items 14/16's precedent: a real opportunity exists, but proving it
safe requires the same "whole-function occupancy-safety pass" Item 14
already called for, not an incremental follow-on to this session's
work.

**Outcome**: reverted both changes (working tree restored to the pre-
T7 state); 0 coverage change, 0 risk taken. Documented for a future
session: the concrete next step, if this is revisited, is to first
fix the `mir_forward_skip_target`/equality-check mismatch in
isolation (verifying Item 15's label-skip actually activates for a
real function, which the evidence here suggests it currently does
not), independently validate that narrow fix alone, and only then
reconsider whether call-result forwarding across the resulting wider
skip window is provably safe - each as its own separately-validated
item, not a combined one.

## Item T8: unconditional jump to the literal next instruction is never elided (2026-08-01)

**Hypothesis**: a fresh worst-ratio sweep of the whole-corpus
`text-size` population (looking for near-miss candidates not related
to Item T7's deferred call-forwarding class) surfaced `tgoto::gt_block_label`
(`int r; r = 0; goto block; block: { r = 7; } return r;`) at only a
14-byte gap (223 generated vs. 209 captured). Direct MIR IR inspection
showed `MIR_JUMP L1` immediately followed by `MIR_LABEL L1` - a
`goto` whose target is the literal next MIR instruction, i.e. a pure
fallthrough with nothing between the jump and its target. The `jp
L1\nL1:\n` pair `mir_try_emit_spilled_scalar_cfg`'s `MIR_JUMP` case
unconditionally emits is dead weight in this case: legacy never emits
a jump to the position immediately following it.

**Investigation**: confirmed this isn't specific to `gt_block_label`
or to `goto` - any straight-line `if` with no `else` whose true-branch
label happens to land immediately after an unconditional jump (e.g.
the end of a preceding loop body, a `switch` case falling through to
the next label, or - as here - a source-level `goto` to the very next
statement) hits the identical pattern. `MIR_BRANCH_FALSE` already
avoids the analogous waste via the existing `mir-migration-plan-next200`
Item 1 fix (probes for phi-copies first and folds to a single
inverted-condition jump when none exist) - `MIR_JUMP` alone had no
equivalent check.

**Fix**: in both `dcc_mir_spilled_cfg.c`'s and `dcc_mir_homed_cfg.c`'s
`MIR_JUMP` case, after resolving the jump's target instruction index
and emitting any needed phi copies (unchanged), skip emitting the `jp
Lxxx` line entirely when the target equals the jump's own instruction
index + 1 - the literal next MIR instruction, meaning control already
falls through there once the phi copies (if any) have executed.

**Validation**:
- Whole-corpus census before/after (`--fail-on-regression`): 0
  regressions, 0 newly/no-longer-emitted (coverage unchanged, 195/2021,
  9.65% - this item only shrinks bytes, it doesn't flip any additional
  function to acceptance on its own). 51 apps had census changes
  (byte-sum -2,582 across the corpus's still-generated candidates);
  **1 app required runtime validation** (`tdead`, whose already-
  accepted MIR output changed).
- Focused `runall.ps1 -Apps tdead -Mode full`: passed, 0 regressions,
  **1 genuine improvement** (nopeep 35,211 -> 35,201 cycles, -0.03% -
  a real, if small, win from the shorter code path, not just a static-
  metric artifact). Accepted via `-UpdatePerfBaseline` for `tdead` only.
- Wide safety net `runall.ps1 -Mode fast`: 314/323 passed (9 skipped,
  as usual), 0 failed, diagnostics/dccpeep/performance all passed.

**Outcome**: 0 functions newly accepted this item (195/2021, 9.65%
unchanged), 0 regressions, 1 genuine (if small) real performance win
on `tdead`. Byte-sum shrink across 51 still-fallback candidates
(-2,582 bytes) moves several closer to the `text-size` threshold for a
future item to flip (e.g. `gt_block_label`'s gap narrowed from 14 to
5 bytes) - a reusable, low-risk emitter fix (any function whose
control flow happens to produce a jump-to-fallthrough shape benefits
automatically), independent of and complementary to the still-deferred
Item T7 call-forwarding class.

**Next**: re-bucket and re-sweep the worst-ratio list again fresh
(byte counts shifted for 51 functions this item); `gt_block_label`
(gap now ~5 bytes) and the `tvla` trio (`vla_sizeof_op_add/mullhs/sub`,
each ~18-byte gaps before this item) are worth a direct look first, as
the closest remaining non-Item-T7-class candidates.

## Item T9: single-copy phi merges route through a needless push/pop stack round-trip (2026-08-01)

**Hypothesis**: a fresh worst-ratio sweep post-Item-T8 surfaced
`tinline::inline_fold_check` at a 2-byte gap (2139 generated vs. 2137
captured) - by far the closest candidate seen this session. Force-
accepting and reading its generated assembly directly showed 4
occurrences of the literal, useless instruction pair `push hl` /
`pop hl` (value goes onto the stack and immediately comes back into
the identical register with nothing in between), each sandwiched
between a load from one frame slot and a store to a different frame
slot: `ld l,(ix-38) / ld h,(ix-37) / push hl / pop hl / ld (ix-40),l /
ld (ix-39),h`.

**Investigation**: traced this to `mir_emit_spilled_phi_copies`
(`dcc_mir_spilled_cfg.c`). Its general shape - push every phi-copy
source in order, then pop every destination in reverse order - exists
to let several *simultaneous* phi copies swap through each other
safely (so that writing an earlier destination can't clobber a value
a later copy still needs to read). That safety concern is entirely
moot when there is exactly one pending copy: with `copy_count == 1`
there is no second copy to be clobbered by or clobber, so the push/
pop pair does nothing but move the value through the stack and back
into the same register. A direct load-then-store reaches the
identical result with no stack traffic at all. (The parallel homed-cfg
helper, `mir_emit_homed_phi_copies`, was checked too: it pushes a
*source register* and pops into a *different destination register*
whenever their allocated colors differ - a genuine cross-register move
via the stack, not a same-register round-trip - so it does not have
the same bug and was left unchanged.)

**Fix**: in `mir_emit_spilled_phi_copies`, after building the
`sources`/`destinations` arrays, added a `copy_count == 1` fast path
that emits `mir_emit_virtual_load[_wide]` followed directly by
`mir_emit_virtual_store[_wide]`, returning before the general push/pop
loops. The `copy_count >= 2` path is untouched (its swap-safety
requirement is real and unaffected).

**Validation**:
- Whole-corpus census before/after (`--fail-on-regression`): 0
  regressions, **+1 newly-accepted function** (`tvla.fixed_cast_bounds`,
  coverage 195/2021 (9.65%) -> 196/2021 (9.70%)). `inline_fold_check`
  itself did not flip - its byte count dropped further (2139 -> 2105,
  now under the 2137-byte legacy size) but it is now blocked by a
  different, unrelated gate (`inline-substitution`), not `text-size`.
  170 apps had census changes (this helper is used by every phi merge
  in the dominant selector, so the blast radius is broad by design);
  **1 app required runtime validation** (`tvla`, whose already-accepted
  MIR output changed).
- Focused `runall.ps1 -Apps tvla -Mode full`: passed, 0 regressions,
  **3 genuine improvements** (peep: 25,428,158 -> 25,428,104 cycles
  and 29,568 -> 29,440 bytes (-0.43%); nopeep: 28,179,081 ->
  28,178,999 cycles) - accepted via `-UpdatePerfBaseline` for `tvla`
  only.
- Wide safety net `runall.ps1 -Mode fast`: 314/323 passed (9 skipped,
  as usual), 0 failed, diagnostics/dccpeep/performance all passed.
  Given the 170-app blast radius, also ran the full milestone-tier
  `runall.ps1 -Mode full` across all 323 apps: 314/323 passed, 0
  failed, diagnostics (106/106), dccpeep fixtures (17/17), and
  performance all passed.

**Outcome**: +1 function newly accepted (196/2021, 9.70%), 0
regressions, 3 genuine real performance/size wins on `tvla`. This is
a reusable, structural fix (any function with exactly one live phi
merge on an edge benefits automatically, which is common - straight-
line `if`/`for`/`while` joins with a single live variable are the
majority shape), and is a clean complement to Item T8 (both remove
dead stack/control-flow overhead from the same dominant selector
without touching slot allocation or forwarding predicates).

**Next**: re-sweep the worst-ratio list fresh again (byte counts
shifted broadly, across 170 apps this time); check whether
`inline_fold_check`'s new blocker (`inline-substitution`) is itself a
tractable near-miss, and continue down the freshly-reranked list for
the next `text-size` near-miss candidate.

### Item T10: dead-store-feeding-value elision (slot allocation) + dccpeep local-alloc peephole widening (2026-08-02)

**Hypothesis**: Item T9's `mir_value_only_used_by_dead_stores` predicate
(from an even earlier item, gating whether a `MIR_CONST`'s emission can be
skipped entirely when its only "uses" are stores whose object is itself
dead) was checked at *emission* time only. A value can pass that same
"every use is a dead store" test yet still get a backend slot allocated
for it earlier, in `mir_prepare_backend_slots` - wasted allocation work
whose only visible cost is a slightly larger `frame_bytes`/`slots` count
(and everything downstream that scales with it: prologue/epilogue size,
`(ix±d)` range pressure). Extending the same predicate to slot allocation,
not just emission, should be a pure size win with no coverage risk on its
own.

**Fix (`dcc_mir_spilled_cfg.c`)**: added
`mir_value_only_used_by_dead_stores(value)` to the OR-chain of skip
conditions inside `mir_prepare_backend_slots`'s slot-assignment loop
(alongside the pre-existing `last[value] <= first[value]` dead-value
check), so a value whose only uses are dead stores never consumes a slot
in the first place.

**Investigation surfaced a second, independent bug while validating this
change on `tests/tgoto.c`'s `gt_block_label`** (a MIR_CONST candidate
this fix doesn't itself flip, since its remaining live value has a
genuine second use at `MIR_RETURN`): `gt_block_label`'s frame is
`locals=2, slots=1, bytes=4` where legacy needs only `-2`. Diffing the
peep-optimized output showed the extra 2 bytes prevented dccpeep's
existing `try_local_alloc_at` peephole (`peep_pass_once.c`) from firing:
it was hardcoded to compact only `-1`/`-2` frame allocations (`ld
hl,-N/add hl,sp/ld sp,hl` -> N x `dec sp`) and never `-3`/`-4`, even
though the Z80 cycle-cost math strictly favors the compacted form for
N up to 4 (confirmed via `/Users/dave/GitHub/ntvcm/x80.cxx`'s own
`z80_cycles` opcode-timing table: `ld hl,nn`=10T, `add hl,sp`=11T, `ld
sp,hl`=6T -> 27T/6 bytes fixed cost regardless of N, vs. N x `dec
sp`=6T each -> 24T/4 bytes at N=4, breaking even only at N=4 and
becoming strictly worse at N=5 (30T > 27T) - so widening is capped at
N<=4, consistent with SKILL.md Rule 4's "smaller byte count alone is not
proof of speed").

**dccpeep fix, with a real pass-ordering hazard discovered and corrected
along the way**: an initial attempt widened `try_local_alloc_at` itself
in place to parse `ld hl,-N` generically for N=1..4. This regressed
`tests/ttt.c`'s `_MinMax` (a hot game-AI function) by +1.08% cycles in
the wide `-Mode fast` safety net. Root cause: `dccpeep.c` already has
pre-existing, `_MinMax`-name-specific passes
(`pass_shrink_minmax_frame3_after_score_cache`,
`pass_shrink_minmax_frame2_after_loop_ctr_b`) that progressively shrink
its frame `-4 -> -3 -> -2` across later fixed-point iterations, by
detecting when specific `(ix-N)` slots become dead only after other
minmax-specific passes run first. The widened `try_local_alloc_at` runs
early, inside `pass_once` (first in `fixed_point_passes[]`), so it was
eagerly and irreversibly consuming the `ld hl,-4` text the very first
time it was seen - before the name-specific shrink passes ever got a
chance to fire on a later iteration - permanently locking `_MinMax` at a
3-or-4-byte frame instead of its true 2-byte minimum. This is a newly-
identified hazard class distinct from ordinary pass-ordering
non-determinism: within `dccpeep`'s `do { ...for each pass in array
order...} while (changed)` fixed-point loop, a pass that irreversibly
consumes a text pattern another (later-in-array, or later-converging)
pass also wants can win permanently, not just non-deterministically.

Fixed by reverting `try_local_alloc_at` to its exact original N=1/2-only
form (with an explanatory comment), and instead adding a brand-new,
separately-scoped pass, `pass_local_alloc_wide` (`peep_pass_final.c`,
handling only N=3/4), shared the existing `local_alloc_hl_result_dead`
liveness guard by making it non-static (`peep_pass_once.c` /
`dccpeep_internal.h`). Critically, `pass_local_alloc_wide` is registered
as a standalone `RUN_PASS(...)` call in `dccpeep.c` **after** the
fixed-point `do-while` loop exits (alongside the existing
`pass_signed_cmp_const_bias_fold`, which has the same "must see fully
converged output" rationale) - not inside `fixed_point_passes[]` - so it
only ever sees text that every other fixed-point pass, including the
`_MinMax`-specific shrink passes, has already fully finished
transforming.

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. pre-T10 baseline: 0
  regressions, **+1 newly-accepted function**
  (`tgoto.gt_block_label`, 196/2021 (9.70%) -> 197/2021 (9.75%)); 5 apps
  required runtime validation (`tbug2`, `tdmfuse`, `tgoto`, `tmirslot`,
  `tvla`).
- Focused `runall.ps1 -Apps tbug2,tdmfuse,tgoto,tmirslot,tvla -Mode
  full`: 5/5 correctness pass; 8 genuine improvements (incl. `tbug2`
  -0.81%/-0.74%); one small residual `tgoto` peep regression (+9 cycles,
  +0.02%) traced via a whole-file `.mac` diff to exactly one other
  function change (`gt_basic`, a hot function, also legitimately
  benefiting from the same widened peephole) - no unexplained code
  changed, both differing functions objectively improved in isolation;
  accepted as SKILL.md's documented "code-placement sensitivity"
  category and baselined for these 5 apps via `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (run twice, since the first pass fired
  before the `_MinMax` ordering bug was found and fixed): first run
  found `cobint` (peep, +0.047%) and `ttt` (peep, +1.08%); after the
  `pass_local_alloc_wide` reordering fix, second run showed `ttt` fully
  resolved (its own change traced by direct diff to `_MinMax` correctly
  reaching its true 2-byte frame again, matching pre-regression
  behavior), leaving only `cobint`.
- `cobint`'s regression (COBOL interpreter; a `.mac` diff showed 8 clean
  `-4 -> 4 x dec sp` conversions with no other differences, and the Z80
  timing table above confirms 4 x `dec sp` (24T) is unambiguously cheaper
  than the 3-instruction form (27T) in isolation) matches SKILL.md's own
  documented "code-placement sensitivity in interpreter heaps" class by
  name - the byte-shrinkage at 8 sites shifts everything downstream,
  and something layout-dependent elsewhere absorbs a small net cost.
  Confirmed via a focused `runall.ps1 -Apps cobint -Mode full` that
  correctness passes cleanly and only the `peep` mode (not `nopeep`)
  shows any delta - consistent with this being purely a dccpeep-output
  layout effect, not a real defect in the underlying MIR/legacy code.
  Given the tiny magnitude (+0.047%, 758,058,178 -> 758,415,604 cycles)
  and the exhaustive, evidence-backed trace matching a class SKILL.md
  explicitly documents as expected noise, accepted and baselined via
  `-UpdatePerfBaseline`.
- Milestone-tier full run (`runall.ps1 -Mode full`, all 323 apps): 314
  passed, 0 failed, 9 skipped, diagnostics (106/106), dccpeep fixtures
  (17/17), and performance (both peep and nopeep) all passed cleanly.

**Outcome**: +1 function newly accepted (197/2021, 9.75%), 0
unaccepted regressions, 8 genuine improvements across the 5 focused
apps, and a real, general-purpose dccpeep peephole gap closed (any
function with a 3- or 4-byte stack-only frame now gets the cheaper
`dec sp` compaction, not just 1-2 byte frames) - independent of and
complementary to the MIR-side slot-allocation fix. The `_MinMax`
pass-ordering hazard this investigation uncovered is now documented as
a reusable discipline: any future widening of an existing
fixed-point-internal peephole pass must first check whether a
name-specific or precondition-dependent later pass could still want the
same text on a subsequent iteration, and if so must be added as a
separate post-convergence pass rather than widened in place.

**Next**: re-sweep the worst-ratio list fresh again post-T10 (both the
slot-allocation and peephole changes shift the population broadly -
170+ apps had census/text changes); continue down the freshly-reranked
`text-size` near-miss candidates.

### Item T11: generalize constant-RHS-to-DE materialization from div/mod to every binary operator (2026-08-02)

**Hypothesis**: a fresh worst-ratio sweep post-T10 surfaced several
near-miss `text-size` candidates (`tvla`'s `vla_sizeof_op_add`/
`_mullhs`/`_sub`, 18-byte gaps) whose generated assembly, on direct
inspection (`DCC_MIR_FORCE_ACCEPT_FUNCTION`), showed the exact wasteful
idiom `ld l,(ix-N)/ld h,(ix-N+1) / push hl / ld hl,1 / ex de,hl / pop hl
/ add hl,de` for a simple `+ 1` - i.e. the left operand is loaded into
HL, pushed to the stack purely to free HL for loading the constant `1`,
then swapped into DE via `ex de,hl`, then popped back. Item 16 (an
earlier migration item) already recognized this exact waste for `/`/`%`
and fixed it - by observing that a constant *load* can never clobber
HL, so the divisor/modulus can go straight into DE with `ld de,<imm>`,
skipping the push/ex/pop dance entirely - but its comment explicitly
scoped the fix to `insn->immediate == '/' || insn->immediate == '%'`
only, leaving every other binary operator (`+`, `-`, `*`,
non-zero-constant comparisons, etc.) on the old, wasteful path in
`dcc_mir_spilled_cfg.c`'s `MIR_BINARY` case.

**Fix**: Item 16's reasoning ("a constant load cannot clobber HL") is
not operator-specific at all - it holds identically for every operator.
Removed the `insn->immediate == '/' || insn->immediate == '%'`
restriction from the guard so the direct-into-DE shortcut fires for
*any* binary operator whenever `src2` is a compile-time constant (not
already handled by the even-more-specific Items 25/27 fusions, which
skip materialization into DE entirely for const-zero comparisons and
so must still be checked first). The pre-existing `!mir.has_vla`
restriction (Item 16's own documented byte-size/accept-gate/VLA
frame-cost-model safety margin) was kept unchanged and applies
identically to the generalized case.

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. post-T10 baseline: 0
  regressions, **+5 newly-accepted functions**
  (`tc99scpe.mid_block_simple`, `tinlinfb.local_helper`,
  `tpostptr.bump_local_paren`, `tunused.aggregates`,
  `tunused.scalars`) - coverage 197/2021 (9.75%) -> **202/2021
  (10.00%)**, crossing the 10% milestone. 254 apps had census changes
  (expected: this is the dominant `MIR_BINARY` arithmetic path, used
  by nearly every function with a constant operand); 8 apps required
  runtime validation.
- Focused `runall.ps1 -Apps
  tbug2,tc89size,tc99scpe,tinlinfb,tmirslot,tpostptr,tunused,tvla -Mode
  full`: 8/8 correctness pass; 10 genuine improvements (incl. `tbug2`
  nopeep -0.49%); 3 tiny (0.01%-0.07%) peep-mode "regressions"
  (`tunused`, `tinlinfb`, `tpostptr`), each on one of the 5 *newly
  MIR-accepted* functions from this item's own delta list (not a
  previously-accepted function regressing). Traced via a before/after
  `dccpeep -Ot`-optimized `.mac` diff for all three: each diff is fully
  explained and small (18-47 lines) - the entire delta is the natural
  difference between legacy's fallback code (which includes an
  un-elided `jp Lxx / Lxx:` no-op jump the legacy backend never
  optimizes away) and the new MIR-emitted code (which never emits that
  jump at all, per Item T8, and uses the new direct-to-DE constant
  load) for a function that is switching code-generation strategy
  entirely, not a regression in previously-accepted output. `nopeep`
  improved in every one of these three apps, confirming the underlying
  semantic code is strictly better; the trivial peep-mode deltas (12,
  12, and 70 cycles respectively, out of 69k-99k total) are the
  expected legacy-vs-MIR code-shape noise this kind of coverage-flip
  always carries, the same category validated for Items T5/T9's own
  newly-accepted functions. Accepted and baselined via
  `-UpdatePerfBaseline` for all 8 apps.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, 9
  skipped, diagnostics/dccpeep/performance all passed.
- Given the 254-app blast radius, also ran the milestone-tier full
  `-Mode full` safety net (323 apps): 314 passed, 0 failed,
  diagnostics (106/106), dccpeep fixtures (17/17), performance (both
  peep and nopeep) all passed cleanly.

**Outcome**: +5 newly-accepted functions (202/2021, 10.00% - the
biggest single-item coverage jump since Item T5, and the first time
this migration effort has crossed 10%), 0 unaccepted regressions, 10
genuine performance/size wins across the 8 focused apps. This is a
clean, broad, structural generalization of an already-proven pattern
(Item 16) with no new predicate invented - it simply removes an
artificial operator-specific restriction whose own justification never
applied to the excluded operators in the first place.

**Next**: re-sweep the worst-ratio list fresh again post-T11 (254 apps
changed, a broad shift); the VLA exclusion this item inherited from
Item 16 still blocks the fix for VLA-bearing functions like `tvla`'s
`vla_sizeof_op_*` family (confirmed: their byte counts were unchanged
by this item) - revisit whether a narrower, VLA-safe version of the
same fix is possible as a future item, now that the general case is
proven safe.

### Item T12: skip a value's entire producer chain when its only use is a dead MIR_UNARY (2026-08-02)

**Hypothesis**: a fresh worst-ratio sweep post-T11 surfaced
`bint::goto_line_op` (22-byte gap). Forced-accept diffing it against
legacy found two distinct causes. The first (investigated first, since
it matched the plan's already-documented Root Cause C): a wasted
store-then-immediate-reload of a freshly-loaded global (`tok`) right
before its single use. `DCC_MIR_REPORT=1` showed the root cause was an
intervening `MIR_CONST` between the load and its use that emits zero
text of its own (folded directly into the consuming `MIR_BINARY` per
Item 16/T11) - so `mir_forward_skip_target`'s existing NOP/label-skip
loop would need to also skip over such elided constants for the
existing `mir_can_forward_hl_to_next` machinery to bridge the gap.

**Blocking discovery (deferred, not fixed this item - see below)**:
tracing `mir_can_forward_hl_to_next`'s own adjacency gate found that
*any* gap `mir_forward_skip_target` computes (NOP, label, or a
hypothetical elided-constant skip) is unconditionally rejected for
every consumer opcode except `MIR_RETURN`:
```c
if (next_instruction != mir_emit_instruction_index + 1 &&
    next->opcode != MIR_RETURN)
    return 0;
```
`git log -S`/`git show` on `fed34c9` (the commit that introduced this
line, "MIR: general constant-multiply strength reduction (Item 37)")
confirms this `MIR_RETURN`-only carve-out is original, deliberate
design, not a stale/dead relic like the `mir_virtual_iy_base` gate
Items T1/T3 fixed - so relaxing it to help non-return consumers is a
genuinely new capability, not a bug fix, and needs the same
whole-function occupancy-safety proof already flagged as an open risk
for the previously-deferred Item T7 (call-result forwarding). Deferred
per the same discipline as Item 6/T7: this is a real design decision,
not a same-session fix, and is left as a fully-scoped candidate for a
future item (see "Deferred: Item T12b" below).

**A separate, distinct cause was pursued instead this item**: while
investigating `goto_line_op`, a much more broadly reusable defect
surfaced independently in `too::scale_all_visitor`
(`(void)w; shape_scale(s, 200);` - the common cast-to-void idiom used
to silence unused-parameter warnings in callback/visitor signatures).
`mir_try_emit_spilled_scalar_cfg`'s `MIR_UNARY` case already skips
emitting the cast/store when its own result has no use, but it
unconditionally loads its *operand* into `hl` first regardless -
`mir_value_has_use(value)` only asks whether *some* instruction
references `value`, not whether that referencing instruction's own
result will ever be observed. For `(void)w`, the load of `w` (v2) is
"used" by the void-cast (`MIR_UNARY op=0`, v3), but v3 itself has zero
uses - so the emitted code loaded `w` into `hl` (`ld l,(ix+6)/ld
h,(ix+7)`) and then immediately overwrote it with `ld hl,200` for the
call argument, two lines later, without ever reading it.

**Fix**: added `mir_value_only_used_by_dead_unary(value)`
(`dcc_mir_spilled_cfg.c`), mirroring the existing
`mir_value_only_used_by_dead_stores` precedent from Item T10: a value
is "only used by a dead unary" when every reference to it is as
`src1` of a `MIR_UNARY` instruction whose own `dst` has no use. Wired
into three places: (1) `mir_prepare_backend_slots`'s slot-skip
OR-chain, so such a value never gets a backend slot at all; (2)
`MIR_LOAD`'s existing dead-value skip condition, alongside
`!mir_value_has_use`; (3) `MIR_CONST`'s existing dead-value skip
OR-chain, for consistency with the same producer-side treatment Item
T10 already gave `MIR_STORE`-only-dead values. Applied the identical
reasoning to `dcc_mir_homed_cfg.c`'s `MIR_UNARY` case (the
`mir_emit_homed_unary_instruction` call site), which had the same
missing `!mir_value_has_use(insn->dst)` guard `MIR_PARAM`/`MIR_CONST`
already have in that selector, added as a matching one-line skip.

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. post-T11 baseline:
  0 regressions, **+2 newly-accepted functions**
  (`tdecl.pick_same_node`, `too.scale_all_visitor`) - coverage
  202/2021 (10.00%) -> **204/2022 (10.09%)**. 14 apps had census
  changes; 2 apps (`tdecl`, `too`) required runtime validation.
- Focused `runall.ps1 -Apps tdecl,too -Mode full`: 2/2 correctness
  pass, 0 regressions, 2 genuine tiny improvements (`tdecl` peep
  -0.02%, `too` peep -0.01%). Accepted and baselined via
  `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, 9
  skipped, clean.
- Given this touches core `MIR_LOAD`/`MIR_CONST`/`MIR_UNARY` emission
  paths shared by nearly every MIR-accepted function in the corpus
  (not just the 2 newly-flipped ones), also ran the milestone-tier
  full `-Mode full` safety net (323 apps): 314 passed, 0 failed,
  diagnostics (106/106), dccpeep fixtures (17/17), performance (both
  peep and nopeep) all passed cleanly.

**Outcome**: +2 newly-accepted functions (204/2022, 10.09%), 0
unaccepted regressions, 2 genuine tiny performance wins. Small direct
yield, but the underlying predicate (a value whose only use is itself
provably dead) is a broadly reusable structural pattern, and the
`(void)param;` idiom it targets is common across the corpus's many
callback/visitor/comparator function signatures.

**Deferred: Item T12b - relax `mir_can_forward_hl_to_next`'s
`MIR_RETURN`-only gap-forwarding carve-out.** Not attempted this
session; documented here so a future session does not have to
re-derive the finding. The gate quoted above means `mir_forward_skip_
target`'s NOP/label-skip capability (Item 15) - and any future
elided-constant-skip built on top of it for cases like
`bint::goto_line_op` - can only ever matter when the very next real
instruction after the skipped gap happens to be the function's
`return`. Confirmed via `git log -S`/`git show` on `fed34c9` that this
restriction is original, deliberate design (not a stale flag like the
ones Items T1/T3 fixed), so relaxing it for other consumer opcodes is
a new capability, not a bug fix. Before attempting: (1) determine
whether the `MIR_RETURN`-specific carve-out exists for a reason tied
to `mir_emit_virtual_iy_epilogue`'s own register-clobbering behavior
or the `has_vla`/`mir_function_has_any_call()` check already
special-cased immediately below it for `MIR_RETURN`, or is genuinely
general-purpose; (2) if judged safe, implement cautiously and validate
on a minimal focused set (`bint` plus 2-3 representative apps) before
any wider census run, since this is core selector-path logic touched
by nearly every MIR-accepted function; (3) this stacks with the
previously-deferred Item T7 (call-result HL-forwarding), which flagged
the exact same gate as a prerequisite risk - a future session should
resolve both together rather than separately.

**Next**: re-sweep the worst-ratio list fresh again post-T12 (14 apps
changed); continue down the post-T11 bucket list toward the next
non-VLA, non-T12b-blocked candidate (e.g. `tenumfsm::main`,
`pint::while_stmt`, `tstructv::assign_return_pair_ptr`,
`tdmfuse::sdm_pair`/`sdm_pair_r`, `tesc::check_s`/`tscanf::check_str`/
`tstr3::check_s`/`tsyntax::check_s`).

**Update (next session): Item T12b was resolved and landed as Item
T13 below** - the deferred rationale above turned out not to be a
correctness hazard; static reading plus full empirical validation
found the restriction was simply overly conservative, with no tied
hazard in `mir_emit_virtual_iy_epilogue`. See Item T13's entry for the
full investigation and validation record.

### Item T13: relax `mir_can_forward_hl_to_next`'s `MIR_RETURN`-only
call-restriction gate (resolves deferred Item T12b), plus fix a
pre-existing `MIR_INDEX_ADDRESS` constant-multiply gap it exposed

**Hypothesis**: the `mir.has_vla || mir_function_has_any_call()` gate
on `MIR_RETURN`-target HL-forwarding (introduced whole, undocumented,
in `fed34c9`) is broader than necessary - `mir_function_has_any_call()`
is a whole-function predicate with no adjacency link to the specific
value being forwarded, so it blocks the exact same safe,
adjacent-single-use-into-return shape `mir_try_emit_comparison_branch`
already exploits, for every function that happens to call anything
*anywhere*, e.g. `tenumfsm::main`'s `return state != S3;` immediately
after the comparison, blocked purely because the function also calls
`scan()`/`printf()` elsewhere.

**Investigation**: traced `mir_emit_virtual_iy_epilogue`'s
`exx`-based register-bank-swap trick (the only mechanism plausibly
tied to "does this function have calls") and found it is gated by
`mir_virtual_iy_base` (a frame-size flag), not by call presence -
no static-reading justification for the restriction was found.

**Fix**: removed `mir_function_has_any_call()` from the one gate line
in `mir_can_forward_hl_to_next` (`dcc_mir_spilled_cfg.c`), keeping
`mir.has_vla` alone (VLA frames can still reuse/shrink stack space
between a value's definition and a non-adjacent skipped-to return, so
that half of the restriction remains a genuine hazard). Removed the
now-fully-unused `mir_function_has_any_call()` helper and replaced the
gate's terse comment with one documenting this finding for future
readers. Added a comment at the gate itself.

**A second, pre-existing bug this exposed and also fixed**: applying
the change alone flipped `t2denum::main` to MIR-accepted but with a
genuine nopeep regression (+3.69% cycles, +2.44% bytes) - confirmed via
`DCC_MIR_FORCE_ACCEPT_FUNCTION=main` **on the pre-T13 tree** that this
regression is a wholly separate, pre-existing defect merely exposed by
crossing the acceptance threshold (same class as Item T6's precedent):
`MIR_INDEX_ADDRESS`'s dynamic-index case
(`dcc_mir_spilled_cfg.c`, the non-`MIR_CONST`-index branch and the
`base_name`/VLA-row-stride branch's `secondary_offset` scaling)
unconditionally emitted `ld de,<stride>` + `call __mulu` for the
per-element stride multiply, never routing through the existing
`mir_mul_const_fast_path_eligible`/`mir_emit_mul_hl_const` shift/add
fast path that `MIR_BINARY '*'` already uses for the exact same
compile-time-constant-multiplier shape (row/element byte strides are
always compile-time constants here). Fixed both call sites to check
`mir_mul_const_fast_path_eligible` and use `mir_emit_mul_hl_const` when
eligible, falling back to `__mulu` only when the fast path itself
declines (e.g. VLA-alloc-feeding values needing runtime division
support). Verified this eliminates the `__mulu` call for
`t2denum::main`'s `transitions[state][STOP]` (stride 4, a plain
`add hl,hl` x2 shift chain) and resolved the nopeep regression to
+0.14% (13,821 -> 13,840 cycles, noise-level, matching the accepted
class from other items this session).

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. post-T12 baseline
  (`/tmp/census-post-t12.tsv`): 0 regressions, **+8 newly-accepted
  functions** (`t2denum.main`, `tautolcs.main`, `tenumfsm.main`,
  `texlog.main`, `tmirslot.forward_into_store`, `trw.fail`,
  `tsretmem.hi_in_return`, `wumpus.prmt`); coverage 204/2022 (10.09%)
  -> **212/2022 (10.48%)**. 253 apps had census metric changes (by far
  the largest blast radius of any item this session, as expected -
  `mir_function_has_any_call()` was a whole-function gate), 8 apps
  flagged as requiring runtime validation.
- Focused `runall.ps1 -Apps t2denum,tautolcs,tenumfsm,texlog,tmirslot,
  trw,tsretmem,wumpus -Mode full`: 8/8 correctness PASS (no
  register-clobber bug from the relaxed forwarding). Before the
  `__mulu` fast-path fix: 7 apps showed regressions, most <0.1%
  (accepted MIR-vs-legacy frame-size noise, same class validated for
  T5/T9/T11/T12) except `t2denum` (peep +1.38%, nopeep +3.69%/+2.44%
  bytes - investigated and fixed as above). After the fast-path fix:
  `t2denum` nopeep dropped to +0.14% (noise-level); `t2denum` peep
  remains at +1.38%, traced via `git stash`/rebuild/`dccpeep -Ot`-diff
  to the same accepted "MIR allocates an 8-byte/3-slot frame via
  `ld hl,-8/add hl,sp` vs legacy's more compact 2-byte `dec sp` x2"
  code-shape difference already validated for `tenumfsm`/T9/T11/T12
  (MIR's generated-bytes for the function are still smaller than
  legacy's: 684 vs 702). 9 genuine tiny improvements across the other
  7 apps (largest: `texlog` peep -0.68% cycles/-1.89% bytes). Ran
  `-UpdatePerfBaseline` for all 8 apps.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given the 253-app blast radius (largest of the session), ran the
  milestone-tier full `-Mode full` safety net (323 apps): 314 passed,
  0 failed, diagnostics (106/106), dccpeep fixtures (17/17),
  performance (both modes) all clean.

**Outcome**: +8 newly-accepted functions (212/2022, 10.48%), 0
unaccepted regressions, 9 genuine performance improvements, one
pre-existing constant-multiply-to-`__mulu` defect found and fixed as a
byproduct (broadly reusable - any `MIR_INDEX_ADDRESS` with a
non-constant runtime index and a small/power-of-2 compile-time stride
benefits, not just the 8 newly-flipped functions). This is the
largest single-item coverage jump and blast radius of the session,
resolving the deferred Item T12b cleanly.

**Next**: re-sweep the worst-ratio list fresh post-T13 (253 apps
changed - a much larger population shift than any prior item); the
previously-deferred Item T7 (call-result HL-forwarding) flagged this
same gate as a shared prerequisite risk and should be revisited now
that the gate is better understood and partially relaxed.

### Item T14: share one function epilogue among multiple `MIR_RETURN`s
instead of duplicating it at every return site

**Hypothesis**: `wumpus::pact` was only 2 bytes over the acceptance
threshold (443 generated vs. 441 captured). Its source is
`for (;;) { ...; if (ch=='S') return TSHO; if (ch=='M') return TMOV; }`
- two early returns inside a loop. A forced-accept diff against
legacy's own captured output showed legacy emits the full epilogue
(`ld sp,ix`/`pop ix`/`ret`, or more with `pop iy`) exactly once, with
both early returns doing a 3-byte `jp` to that single shared
occurrence; both MIR selectors (`mir_try_emit_spilled_scalar_cfg` and
`mir_try_emit_homed_scalar_cfg`) instead re-emit the full epilogue
text inline at every `MIR_RETURN`, independent of how many other
returns exist in the same function. Any function with 2+ returns pays
this cost on every non-final return - a broad, structural, systemic
gap, not a one-off.

**Fix**: in both `dcc_mir_spilled_cfg.c` and `dcc_mir_homed_cfg.c`,
precompute `return_count` (total `MIR_RETURN`s in the function) and
`last_insn_is_return` (whether the function's literal last MIR
instruction is itself a `MIR_RETURN`) before the main emission loop.
During emission, only the "owner" return - the one at the function's
true tail (either the last MIR instruction if it's a return, or the
existing fall-off-the-end epilogue tail if not) - keeps the full
inline epilogue text; every other return instead does `jp` to a
lazily-allocated shared label defined once at the owner's site. HL/DE
already hold the return value from the value-load code immediately
above in every case, and a bare `jp` never disturbs registers, so this
is safe regardless of which return's value reaches the shared
epilogue - the same guarantee legacy's own version of this pattern
already relies on. `mir_try_emit_homed_scalar_cfg`'s frameless case
(bare 1-byte `ret`, no ix/iy restore) is excluded from sharing, since a
3-byte `jp` to a 1-byte `ret` would be a net regression, not a win; an
additional guard also disables sharing for the theoretical (unproven
to be reachable) case of a non-void homed-cfg function whose last
instruction isn't a return, since neither existing code path would
then define the label.

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. post-T13 baseline:
  0 regressions, 0 coverage change (this item's yield is broad byte
  reduction across the still-fallback population - shrinking the
  average gap for future items - not an immediate coverage jump: the
  one function that motivated the investigation, `wumpus::pact`,
  shrank from 443 to 424 generated bytes as hoped, crossing the
  `text-size` threshold, but is still separately blocked by the
  pre-existing, unrelated `cfg-backedge` migration boundary - its own
  `for (;;)` loop - so its `result` moved from `fallback text-size` to
  `fallback cfg-backedge`, not to `accepted`). 131 apps had census
  metric changes; total generated-bytes across the whole corpus
  dropped by 7,188 bytes across 310 functions. 1 app
  (`tc89size`) flagged as requiring runtime validation.
- Focused `runall.ps1 -Apps tc89size -Mode full`: correctness PASS, 2
  tiny performance deltas (+0.01% peep, +0.01% nopeep - 10-11 cycles
  out of ~100k+) - consistent with the expected, unavoidable trade-off
  of a `jp`'s own execution cost on the (rare) early-return path
  versus the byte savings, the same trade-off legacy's own
  shared-epilogue pattern already accepts for the identical code
  shape. Accepted and baselined via `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given this touches the core `MIR_RETURN` emission path in both
  selectors (131-app blast radius), ran the milestone-tier full
  `-Mode full` safety net (323 apps): 314 passed, 0 failed,
  diagnostics (106/106), dccpeep fixtures (17/17), performance (both
  modes) all clean.

**Outcome**: 0 coverage change, 0 regressions, -7,188 bytes across 310
functions (the average `text-size` gap - most acute for functions with
several early returns - is now measurably smaller corpus-wide, which
should help future comparison/forwarding-style items reach the
acceptance threshold for more candidates). One immediate near-miss
(`wumpus::pact`) was resolved on the `text-size` axis specifically but
remains blocked by the separate, pre-existing `cfg-backedge` migration
boundary (out of scope for this item - loop backedges are a
deliberate, documented migration barrier, not a bug).

**Next**: re-sweep the worst-ratio list fresh post-T14 (310 functions
changed); the `cfg-backedge` gate is now the sole blocker for at least
one otherwise-ready function (`wumpus::pact`) and may be worth
revisiting as its own future item once enough non-loop `text-size`
candidates are exhausted.

## Item T15: forward a value across an intervening MIR_CONST into a binary operator's constant-RHS push/pop dance (2026-08-01)

**Hypothesis**: a fresh post-T14 whole-corpus census (`/tmp/census-post-t14.tsv`)
re-bucketed the `text-size` gap population (near(<16): 0, close(16-64):
30, mid(64-256): 328, far(>256): 1367) and found the smallest real
(non-synthetic) gaps clustered in `tests/tvla.c`'s
`vla_sizeof_op_add`/`_sub`/`_and`/`_mullhs` (gap 18-42 bytes) and
`tesc`/`tscanf`/`tstr3`/`tsyntax`'s `check_s` (gap 34 bytes, 4 apps at
once - SKILL.md's own flagged example). `check_s` turned out to be
exactly the already-deferred Item T7 call-result-forwarding class (the
`strcmp()` result gets store/reloaded because of the still-flagged
`mir_forward_skip_target`/adjacency-equality-check risk T7 declined to
loosen) - re-confirmed the deferral stands rather than re-litigating
it, since nothing this session changed that gate's risk calculus.

Direct MIR IR inspection of `vla_sizeof_op_add`
(`int a[n]; return (int)(sizeof a + 1);`) showed a *different*, novel
root cause: `v4 = vlasize a` is immediately followed by `v5 = const 1`,
which is immediately followed by `v6 = v4 + v5`. `mir_can_forward_hl_to_next`
only recognizes a fixed set of opcodes as the literal next consumer and
`MIR_CONST` is not one of them, so v4 gets stored to its backend slot at
its definition and immediately reloaded one instruction later purely to
feed the following `MIR_BINARY`'s left operand - a redundant round trip
via `(ix-8)/(ix-7)` with nothing in between that could have clobbered
HL. This is unrelated to VLA specifically: any `computed_expr OP
literal` shape where `computed_expr`'s own MIR construction happens to
be immediately followed by the literal's `MIR_CONST` (the typical,
source-order construction pattern) hits the identical gap.

**Fix**: added a new predicate, `mir_can_forward_stack_to_binary_const`,
mirroring the existing `mir_can_forward_stack_to_index` shape (`value`;
`MIR_CONST`; consumer) but for `MIR_BINARY` instead of
`MIR_INDEX_ADDRESS`. It requires the value be the binary's `src1`, the
constant be the binary's `src2`, the binary's operand type not be wide
(4 bytes, handled by a separate protocol), and explicitly excludes any
shape with a different code path for the constant: divmod pairing
(`mir_divmod_partner`), the multiply-by-constant fast path
(`mir_mul_const_fast_path_eligible`), and fused-comparison branches
(`mir_binary_is_fusable_comparison`) - all bypass the plain "push left,
evaluate right, pop, combine" sequence this fix targets, so forwarding
across them is out of scope here, not attempted.

Wired into `mir_emit_virtual_store` (pushes `hl` immediately and
records the forwarding, same as the existing stack-to-index case) and
into `MIR_BINARY`'s non-wide emission path: skips the now-redundant
`mir_emit_virtual_load(out, insn->src1)` when stack-forwarded, forces
the `!mir.has_vla && mir_binary_only_constant(insn->src2)` direct-`ld
de,const` fast path to fall through to the full push/pop branch when
stack-forwarded (since HL doesn't hold the left operand at that point
any more), and skips that branch's own now-redundant leading `push hl`
(the value was already pushed at its definition site) - the trailing
`pop hl` is unchanged and correctly retrieves the forwarded value,
preserving stack balance in every case.

**Validation**:
- Focused check: `vla_sizeof_op_add`/`_sub`/`_and` all flipped from
  `fallback text-size` to `mir accepted` (add: 606->554 generated
  bytes vs. 588 captured; sub: 612->560 vs. 594; and: 641->589 vs.
  599). `vla_sizeof_op_mullhs` (`3 * sizeof a`, constant on the left,
  not the right) is a different shape not covered by this predicate
  and remains fallback, as expected/unaffected.
- Whole-corpus census (`--fail-on-regression`) vs. post-T14 baseline:
  0 regressions, **+5 newly-accepted functions** (212/2022 -> 217/2022,
  10.48% -> 10.73%): the 3 `vla_sizeof_op_*` functions above plus two
  bonus flips found by the same fix, `vla_sizeof_2d_rows` and
  `vla_sizeof_shadow_outer_after`. 199 apps had census metric changes;
  1 app (`tvla`) flagged as requiring runtime validation. Corpus-wide
  generated-bytes total dropped by 32,370 bytes across 576 functions -
  the broadest single-item byte-sum shrink this session (T14's was
  7,188 bytes/310 functions), consistent with this being a genuinely
  common shape (`computed_expr OP literal`), not VLA-specific.
- Focused `runall.ps1 -Apps tvla -Mode full`: correctness PASS; 1 tiny
  nopeep regression (28,178,772 -> 28,178,846 cycles, +74 cycles /
  +0.0003%) and 1 tiny peep improvement (25,428,092 -> 25,427,428,
  -0.003%) - the same noise-level, MIR-vs-legacy frame-shape trade-off
  class already characterized and accepted for T9/T11/T12/T13/T14's
  comparable deltas this session, not a genuine regression. Accepted
  via `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given the 199-app blast radius touching the core `MIR_BINARY`
  emission path (the single most heavily-used instruction case in the
  dominant selector), ran the milestone-tier full `-Mode full` safety
  net (323 apps): 314 passed, 0 failed, diagnostics (106/106), dccpeep
  fixtures (17/17), performance (both modes) all clean.

**Outcome**: +5 newly-accepted functions (212/2022 -> 217/2022,
10.73%), 0 regressions, -32,370 bytes across 576 still-fallback
candidates corpus-wide - the broadest byte-sum shrink of this
session's items, since the underlying shape (`computed_expr OP
literal`) is common well beyond VLA code. `check_s`'s Item T7
deferral is reconfirmed unchanged (a structurally distinct call-result
forwarding gap, not addressed by this fix).

**Next**: re-sweep the worst-ratio list fresh post-T15 (576 functions
changed - likely surfaces new near-miss candidates); consider whether
an analogous predicate is worth adding for the `MIR_UNARY`/
`MIR_STORE_INDIRECT`/etc. consumer opcodes `mir_can_forward_hl_to_next`
already recognizes for the literal-adjacent case, mirrored the same
way for the "one MIR_CONST in between" case, if evidence supports it
after re-measuring.

## Item T16: forward a binary operator's right-hand (non-constant) operand across a stack push/pop when the left operand is a plain constant (2026-08-01)

**Hypothesis**: fresh post-T15 whole-corpus census (`/tmp/census-post-t15.tsv`)
re-bucketed the `text-size` gap population (near(<16): 0, close(16-64):
26, mid(64-256): 396, far(>256): 1297). The closest real (non-deferred,
non-artifact) candidate was `tvla.vla_sizeof_op_mullhs` (gap 18 bytes,
`3 * sizeof a`). Direct MIR IR inspection (`DCC_MIR_REPORT=1`) showed a
shape that is the mirror image of T15's: `v4 = const 3; v5 = vlasize a;
v6 = v4 * v5` - the constant (`v4`) comes *first* in program order and
is `MIR_BINARY`'s `src1`, while the computed value (`v5`) is `src2`
(the right-hand operand). `mir_can_forward_hl_to_next`'s guard
(`else if (next->src1 != value) return 0;`, immediately before its
opcode switch) means it can only ever match a value against
`MIR_BINARY`'s `src1` - it has no mechanism at all for recognizing a
value as the immediately-following binary's `src2`, even when nothing
intervenes. This is a different, narrower gap than T15 (T15 was about
skipping an *intervening* `MIR_CONST` for a `src1` match; this is the
complete absence of any `src2`-matching capability).

Simple HL-persistence forwarding cannot work for this shape: `MIR_BINARY`'s
emission always loads `src1` into HL first, and here `src1` is a plain
constant (`mir_binary_only_constant`), so the `ld hl,3` load
unconditionally clobbers whatever HL held from `src2`'s immediately-prior
computation before `src2` is ever used. The fix has to be stack-based
(push `v5`/src2 immediately after computing it, retrieve via `pop`
later), not HL-based.

**Fix**: added `mir_can_forward_stack_to_binary_rhs`, mirroring T15's
`mir_can_forward_stack_to_binary_const` but for the opposite operand
position: requires `value` be the immediately-following `MIR_BINARY`'s
`src2`, with `src1` satisfying `mir_binary_only_constant` (so the
const-load-clobbers-HL precondition structurally holds), excludes wide
(4-byte) operands, divmod-paired binaries, and fused-comparison
branches (same exclusions as T15, for the same reason - those bypass
the plain push/pop combining sequence this fix targets). Wired into
`mir_emit_virtual_store` (ORed alongside the existing two stack-forward
predicates) and into `MIR_BINARY`'s non-wide emission path: added a
`stack_forwarded_right` local: when true, the entire "push hl / load or
materialize src2 / ex de,hl / pop hl" dance collapses to a single `pop
de`, since `src1`'s constant is already correctly in HL from the
earlier `ld hl,<const>` and `src2`'s value - pushed at its own
definition site - can be popped straight into DE. This new branch is
checked first (before the Item 25/27 fused-comparison and constant-RHS
fast-path branches), but since the predicate already excludes
fused-comparison shapes, there is no overlap with those branches in
practice.

**Validation**:
- Focused check: `vla_sizeof_op_mullhs` flipped from `fallback
  text-size` to `mir accepted` (560 generated bytes vs. 606 captured,
  46 vs. 63 instructions).
- Whole-corpus census (`--fail-on-regression`) vs. post-T15 baseline:
  0 regressions, **+2 newly-accepted functions** (217/2022 -> 219/2022,
  10.73% -> 10.83%): `vla_sizeof_op_mullhs` plus a bonus flip,
  `tdmfuse.test_first_stmt_reassigns_operand` (686 generated bytes vs.
  693 captured). 21 apps had census metric changes; 2 apps (`tdmfuse`,
  `tvla`) flagged as requiring runtime validation. Corpus-wide
  generated-bytes total dropped by 3,514 bytes across 37 functions -
  smaller blast radius than T15 (this shape - const-on-the-left times
  computed-value - is less common than T15's computed-value-plus-
  literal shape, as expected).
- Focused `runall.ps1 -Apps tdmfuse,tvla -Mode full`: correctness PASS
  for both; `tvla` showed 2 tiny noise-level regressions (peep:
  25,427,428 -> 25,427,772 cycles, +0.0014%; nopeep: 28,178,846 ->
  28,179,400, +0.0020%) and `tdmfuse` showed 2 tiny improvements (peep
  195,624 -> 195,623; nopeep 215,975 -> 215,971) - the same
  noise-level, MIR-vs-legacy frame-shape trade-off class already
  characterized and accepted for T9/T11/T12/T13/T14/T15's comparable
  deltas this session, not a genuine regression. Accepted via
  `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given this again touches the core `MIR_BINARY` emission path, ran
  the milestone-tier full `-Mode full` safety net (323 apps): 313
  passed, 1 failed (`tkbd`); confirmed via isolated re-run
  (`-Apps tkbd -Mode full`) that `tkbd` passes cleanly alone - it is a
  known-flaky, `perf_ignore`-marked interactive stdin-timing test
  (`kbhit()`/`getch()` polling loop, `tests/_test_overrides.json` line
  43), unrelated to this change (no comparison/binary-operator code in
  its source). Diagnostics (106/106), dccpeep fixtures (17/17), and
  performance (both modes) all clean.

**Outcome**: +2 newly-accepted functions (217/2022 -> 219/2022,
10.83%), 0 regressions, -3,514 bytes across 37 still-fallback
candidates corpus-wide. Smaller yield than T15 (as expected - the
const-first-then-computed-value shape is rarer than the reverse), but
a genuinely distinct structural gap in `MIR_BINARY`'s operand-forwarding
coverage, now closed for both operand positions.

**Next**: re-sweep the worst-ratio list fresh post-T16; `MIR_BINARY`'s
operand forwarding now covers src1-adjacent-to-const (T15) and
src2-adjacent-with-const-src1 (T16) - the remaining gap in this family
would be src1 and src2 *both* non-constant and computed (neither can be
forwarded via a simple HL/stack rule without extending the mechanism to
track two pending forwarded values simultaneously; not attempted here,
revisit only if evidence shows it's a material remaining shape).

## Item T17: allow fusable comparisons to use T15/T16's stack-forwarding predicates (2026-08-01)

**Hypothesis**: fresh post-T16 bucket sweep found `bint.goto_line_op`
(gap=22, `if (tok != 257) die(...)`) as the closest real candidate
after `tinline.edge_outer_body` (reconfirmed as the already-known
static-inline measurement artifact, skipped again). Direct MIR IR and
`.mac` inspection showed the exact T15 shape (`v2 = load tok; v3 =
const 257; v4 = v2 != v3`), but `v2` was NOT forwarded: it was stored
to its backend slot at its own definition and immediately reloaded
right before the comparison, even though nothing intervened that could
have clobbered it. The reason: `v4` is a **fusable comparison**
(immediately followed by `MIR_BRANCH_FALSE` with no other use), and
both T15's `mir_can_forward_stack_to_binary_const` and T16's
`mir_can_forward_stack_to_binary_rhs` explicitly excluded fusable
comparisons from forwarding entirely, reasoning (at the time, not
empirically verified) that they "bypass the plain push/pop combining
sequence" T15/T16 target.

Tracing the actual emission code disproved that assumption: the
src1/src2-loading logic in `MIR_BINARY`'s non-wide case (including the
"constant right-hand operand -> `ld de,<const>`" fast path and the
full push/pop fallback) is **shared** between ordinary arithmetic and
fusable comparisons - the `fuse_skip`/`mir_emit_fused_comparison_branch`
branch-vs-store fork happens only *after* both operands are already
loaded into HL/DE. `mir_emit_fused_comparison_branch` itself only
consumes whatever HL/DE already hold; it has no dependency on how they
got there. So excluding fusable comparisons from T15/T16 was
unnecessarily conservative for the general case.

One genuine hazard did surface during this investigation, not merely a
false conservatism: Items 25/27's **zero-RHS shortcuts**
(`mir_fused_compare_is_const_zero_rhs`/`_signed_zero_sign_test`) skip
materializing DE *entirely* for a comparison against the constant 0,
testing HL directly instead - and that skip path does **not** perform
the `pop` that a stack-forwarded operand's push depends on. If T15's
predicate forwarded a value across an intervening `MIR_CONST 0` into
exactly this zero-RHS comparison shape, the value's earlier `push hl`
would never be popped, permanently unbalancing the stack - a real
correctness bug, not just a missed optimization. (T16's `src2`-position
predicate is not exposed to this same hazard in practice, since it
requires `src1` be the constant and `src2` be the forwarded - therefore
non-constant - value, so `mir_fused_compare_is_const_zero_rhs`'s check
of `src2`'s own definition being `MIR_CONST` can never hold; a defensive
mirror of the same guard was still added for robustness.)

**Fix**: removed the blanket `mir_binary_is_fusable_comparison(...) > 0`
exclusion from both `mir_can_forward_stack_to_binary_const` (T15) and
`mir_can_forward_stack_to_binary_rhs` (T16). Added forward declarations
for `mir_fused_compare_is_const_zero_rhs`/
`mir_fused_compare_is_signed_zero_sign_test` (previously defined later
in the file than these predicates) and replaced the blanket exclusion
in T15's predicate with a narrow one: `mir_binary_is_fusable_comparison
(...) > 0 && (mir_fused_compare_is_const_zero_rhs(...) ||
mir_fused_compare_is_signed_zero_sign_test(...))` - i.e., only the two
specific shapes that skip the pop are excluded; every other fusable
comparison (non-zero-RHS, or zero-RHS but unsigned/wrong-operator so
neither shortcut applies) is now eligible for forwarding, matching
plain arithmetic operators. The same narrow guard was added
defensively to T16's predicate even though it should be unreachable
there.

**Validation**:
- Focused check: `goto_line_op` flipped from `fallback text-size` to
  `mir accepted` (520 generated bytes vs. 523 captured, 49 vs. 48
  instructions).
- Whole-corpus census (`--fail-on-regression`) vs. post-T16 baseline:
  0 regressions, **+1 newly-accepted function** (219/2022 -> 220/2022,
  10.83% -> 10.88%). 67 apps had census metric changes (the broadest
  blast radius since T13, since fusable comparisons are pervasive
  across the corpus); 1 app (`bint`) flagged as requiring runtime
  validation. Corpus-wide generated-bytes total dropped by **18,900
  bytes across 225 functions** - a substantial shrink given the
  breadth of the comparison-fusion path this reaches.
- Focused `runall.ps1 -Apps bint -Mode full`: correctness PASS; 2
  genuine (not noise-level) performance improvements: peep
  352,155,080 -> 352,154,795 cycles, nopeep 481,298,609 -> 481,298,409
  cycles - real wins, not just static-metric noise. Accepted via
  `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given the 67-app blast radius touching the shared fusable-comparison
  operand-loading path, ran the milestone-tier full `-Mode full`
  safety net (323 apps): 314 passed, 0 failed, diagnostics (106/106),
  dccpeep fixtures (17/17), performance (both modes) all clean (no
  recurrence of the unrelated `tkbd` interactive-timing flake seen
  during T16's run).

**Outcome**: +1 newly-accepted function (219/2022 -> 220/2022,
10.88%), 0 regressions, -18,900 bytes across 225 still-fallback
candidates corpus-wide - the broadest byte-sum shrink since T15,
closing a real, previously-unverified conservative assumption in both
T15 and T16's predicates while also fixing a genuine (if previously
latent/unexercised) stack-imbalance hazard for the zero-RHS shortcut
combination along the way.

**Next**: re-sweep the worst-ratio list fresh post-T17 (225 functions
changed byte counts). The 67-app blast radius suggests fusable
comparisons combined with an intervening `MIR_CONST` or a
constant-first shape are common - worth checking whether any of the
now-still-fallback functions in those 67 apps have a *different*,
not-yet-covered operand-forwarding gap once re-measured.
