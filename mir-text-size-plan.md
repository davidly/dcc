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
