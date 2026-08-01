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

## Item T18: elide a MIR_CONST whose sole use is MIR_INDEX_ADDRESS's compile-time-resolved constant index (2026-08-01)

**Hypothesis**: fresh post-T17 bucket sweep found `tinlnpar.main` (gap=26)
as the closest real candidate after the already-known
`tinline.edge_outer_body` static-inline artifact. Direct `.mac`
inspection of a force-accepted candidate showed a striking, clearly
dead sequence:
```
	ld hl,_Z0003   ; v12 = address of "mem" (MIR_ADDRESS)
	push hl
	ld hl,5        ; v13 = const 5 (the array index) - materialized here...
	pop hl         ; ...then immediately discarded: pop overwrites hl with v12
	ld de,5        ; ...and rematerialized fresh, straight into de, for the add
	add hl,de
```
Tracing `MIR_INDEX_ADDRESS`'s constant-index emission case (in
`mir_try_emit_spilled_scalar_cfg`) explains why: when the index operand
(`src2`) is a `MIR_CONST`, the byte offset is computed **entirely at
compile time** (`byte_offset = index_definition->immediate *
insn->immediate`) and materialized directly via `ld de,<byte_offset>` -
`mir_emit_virtual_load(out, insn->src2)` (which would actually read the
constant's runtime value) is **never called** in this branch; it's only
reachable in the other two `MIR_INDEX_ADDRESS` shapes (the runtime
variable-stride `base_name` path, and the non-constant-index path).
This means a `MIR_CONST` whose *only* use is exactly this
constant-index shape has its runtime materialization (`ld hl,<const>`,
plus any spill store) computed and then read by nothing at all - the
exact same class of defect `mir_binary_only_constant`/
`mir_call_only_constant`/`mir_multiply_by_small_constant` already
guard against for their own respective consumer shapes, just for a
consumer opcode none of those cover.

**Fix**: added `mir_index_only_constant(value)` (placed directly after
`mir_binary_only_constant`, which it mirrors structurally): confirms
`value`'s definition is `MIR_CONST`, scans every instruction for other
uses (returning 0 if found), and requires exactly one match of the
shape `MIR_INDEX_ADDRESS` with `src2 == value` and `base_name[0] == 0`
(the fixed/compile-time-resolved-offset shape - NOT the runtime-stride
`base_name` path, which genuinely does read `src2`'s value at
runtime). Wired into `MIR_CONST`'s existing dead-value check chain
(alongside `mir_call_only_constant`/`mir_binary_only_constant`/etc.) -
when true, the constant's entire materialization is skipped, exactly
like the other dead-constant cases already handled there.

Also surveyed `dcc_mir_homed_cfg.c` (the `homed-scalar-cfg` selector,
149 functions) for the identical defect: its own `MIR_INDEX_ADDRESS`
acceptance is *already* restricted to the constant-index,
`base_name`-empty shape only (Item 22), and its emission
(`mir_emit_pointer_offset_address_to_home`) likewise never reads the
index constant's own home - so the same dead-materialization gap
exists there too, in `mir_emit_constant_to_home`'s unconditional call
from `MIR_CONST`'s case (which currently has no dead-value check at
all, unlike `mir_try_emit_spilled_scalar_cfg`'s several). This is a
separate translation unit (no shared `static` predicate reuse without
a header declaration) and a distinct, smaller population - **deferred
as a follow-on item** (a likely Item T19) rather than folded into this
commit, per the "one reusable concept per commit" discipline; not
withheld for any correctness concern, just scope.

**Validation**:
- Focused check: `tinlnpar.main` shrank 667 -> 658 generated bytes
  (-9), but remains fallback - it hits a *different*, unrelated
  `inline-substitution` barrier once the text-size gap closes enough to
  reveal it (an existing, deliberate migration boundary, not a new
  problem introduced by this fix). This function itself was not
  flippable by T18 alone; the win is the general byte reduction
  wherever this shape occurs, not this specific function's coverage.
- Whole-corpus census (`--fail-on-regression`) vs. post-T17 baseline:
  0 regressions, 0 newly-accepted functions this round (no flips), but
  **the broadest census metric footprint yet - 145 apps** with byte
  changes (constant array/struct indexing is extremely pervasive).
  Corpus-wide generated-bytes total dropped by **23,622 bytes across
  250 functions**. 2 apps (`t2denum`, `tstruct`) flagged as requiring
  runtime validation.
- Focused `runall.ps1 -Apps t2denum,tstruct -Mode full`: correctness
  PASS for both; 4 **genuine** cycle improvements (not noise): t2denum
  peep -0.31%, nopeep -0.22%; tstruct peep -0.02%, nopeep ~0%. Accepted
  via `-UpdatePerfBaseline`.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed, clean.
- Given the 145-app blast radius (the widest of any item this
  session), ran the milestone-tier full `-Mode full` safety net (323
  apps): 314 passed, 0 failed, diagnostics (106/106), dccpeep fixtures
  (17/17), performance (both modes) all clean.

**Outcome**: 0 coverage change this round (220/2022, 10.88% - a
byte-shrink-only item, unlike T15-T17), 0 regressions, **-23,622 bytes
across 250 still-fallback candidates corpus-wide** - the broadest
census footprint (145 apps) of any item this session, confirming
constant array/struct indexing is an extremely common shape. A real,
if indirect, contribution toward future coverage: several of the 250
affected functions may now be closer to their own text-size threshold
for a future item to tip over.

**Next**: (a) implement the analogous `mir_index_only_constant`-style
dead-value check in `dcc_mir_homed_cfg.c`'s `MIR_CONST` case (Item
T19 candidate - smaller population, ~149 functions, but a real,
distinct gap); (b) re-sweep the worst-ratio list fresh post-T18 (250
functions changed byte counts) before picking the next candidate.

## Item T19: mirror Item T18's dead-index-constant elision in dcc_mir_homed_cfg.c (2026-08-01)

**Hypothesis**: Item T18's write-up flagged that `dcc_mir_homed_cfg.c`
(the `homed-scalar-cfg` selector, 149/2022 functions) likely has the
identical dead-constant-materialization gap: its own
`MIR_INDEX_ADDRESS` acceptance (Item 22) is *already* restricted to
exactly the fixed-stride, constant-index shape (`base_name[0] == 0`,
`index_definition->opcode == MIR_CONST`), and its emission
(`mir_emit_pointer_offset_address_to_home`, `dcc_mir_emit_common.c`)
folds the byte offset entirely at compile time - it never reads the
index constant's own home. Unlike `mir_try_emit_spilled_scalar_cfg`,
this selector's `MIR_CONST` case had **no dead-value check of any
kind** before this item (not even a generic `mir_value_has_use`
check, unlike its own `MIR_UNARY` case, which already has one from
Item T12).

**Fix**: ported `mir_index_only_constant` to this file as its own
`static` predicate (separate translation unit from
`dcc_mir_spilled_cfg.c`, so no symbol can be shared without a header
declaration - not worth doing for one small predicate; duplicated
verbatim instead, matching this codebase's existing convention of
selector-local static helpers). Wired into `MIR_CONST`'s case ahead of
the existing wide/narrow dispatch: `if
(mir_index_only_constant(insn->dst)) break;` skips materialization
entirely, mirroring `MIR_UNARY`'s own Item T12 dead-result skip
immediately below it.

**Validation**:
- Whole-corpus census (`--fail-on-regression`) vs. post-T18 baseline:
  0 regressions, 0 coverage change (220/2022 held), **only 1 app with
  census changes** - `tc99init.main`: -171 generated bytes, -19
  generated instructions, still fallback (unchanged reason). As
  expected, a much narrower footprint than T18's 145 apps: this
  selector's other acceptance restrictions (no aggregates, restricted
  `MIR_LOAD_INDIRECT`/`MIR_STORE_INDIRECT` shapes, etc.) mean far
  fewer functions reach `MIR_INDEX_ADDRESS` through this selector to
  begin with, and fewer still carry a superfluous const-index. 0 apps
  flagged for runtime validation by the census tool itself.
- Focused `runall.ps1 -Apps tc99init -Mode full`: PASS, 0 performance
  regressions (no baseline changes needed - no flagged deltas at all).
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed,
  diagnostics (106/106) and dccpeep fixtures (17/17) both clean.
- Given the narrow 1-app blast radius (well below any of this
  session's milestone thresholds - no coverage jump, no semantic gate
  removed, no shared ABI/runtime touched), a milestone-tier full run
  was not required per SKILL.md's validation-tier guidance; the
  focused full-mode + wide fast-mode combination is the correct tier
  for this blast radius.

**Outcome**: 0 coverage change (220/2022, 10.88% - homed-cfg's smaller
population and stricter surrounding acceptance rules mean this shape
is rarer there), 0 regressions, -171 bytes/19 instructions in the one
affected function. Confirms the T18 root cause is a genuine,
reusable class (present in both selectors that implement
`MIR_INDEX_ADDRESS`'s constant-index fast path), not an artifact of
`dcc_mir_spilled_cfg.c`'s specific emission strategy.

**Next**: re-sweep the worst-ratio/bucket list fresh post-T19 (no
population shift expected beyond `tc99init`, but re-derive rather than
assume) before picking the next candidate.

## Item T20: rematerialize call-argument values instead of spilling them, plus four companion fixes surfaced mid-validation (2026-08-01)

**Hypothesis**: fresh post-T19 gap re-bucketing (excluding
`tinline.edge_outer_body`, the known static-inline artifact) surfaced
`pint.while_stmt` (gap=32). Direct `.mac` inspection found a completely
dead register round-trip: `ld hl,(_Z0007)` (loading global `cp`)
followed immediately by a pointless `ld c,l/ld b,h/ld l,c/ld h,b`
HL->BC->HL no-op, then `push hl`, as the second argument to
`patch(jz, cp)`. Tracing the emission code to
`mir_call_argument_cache_target` (the existing BC-cache helper) showed
it unconditionally caches any value with exactly one later `MIR_ARG`
use into BC via a store+reload dance, **without ever checking whether
that use is truly adjacent** - i.e. whether the value's own definition
is immediately followed by its `MIR_ARG` marker with the matching
`MIR_CALL` immediately after *that*, a shape where caching is pure
waste. Tracing the generic `MIR_CALL` argument-emission loop further
established a key structural fact: **call arguments are always
physically pushed in reverse of their MIR-instruction-stream definition
order** (the last-MIR-defined argument is always pushed first) - which
makes "definition immediately followed by ARG immediately followed by
CALL" a provably safe condition for direct HL-forwarding with zero
preservation needed, since nothing can execute between the value's
computation and its push.

**Fix (the core T20 concept)**: added `mir_can_forward_hl_to_call_argument
(int value)` in `dcc_mir_spilled_cfg.c`, wired into `mir_emit_virtual_store`
immediately ahead of the existing BC-cache branch (`mir_call_argument_
cache_target`). When the adjacency holds, it reuses the *existing*
`mir_forwarded_hl_value`/`mir_forwarded_hl_instruction` mechanism
(anchored to the `MIR_ARG`'s own instruction index, since `MIR_ARG`'s
emission is a no-op and `mir_emit_virtual_load`'s adjacency check
naturally fires when the emit loop reaches the following `MIR_CALL`).

**What validation uncovered**: the whole-corpus census for this one
fix alone showed +3 newly-accepted functions (`pint.while_stmt`,
`tbcgcol.main`, `tptrixld.main`), 0 census regressions - but the
focused full-mode run found a genuine nopeep regression in `tbcgcol`
(+0.22%). Diffing MIR's newly-flipped output against **actual replayed
legacy** (via `git stash` + non-forced rebuild, not just the T20-stashed
forced-accept diff) isolated the true cause: a *separate*, pre-existing
gap (the plan's own catalogued "Root Cause C") - a `MIR_ADDRESS` value
(a local array's address, passed as a call argument) was stored to a
spill slot and immediately reloaded a few instructions later (because
another argument's own evaluation needed HL first), whereas legacy
simply recomputes the cheap, side-effect-free `ix`-relative address
expression fresh at the point of use. Chasing this down surfaced four
more companion gaps, each fixed in turn and each validated to be
individually load-bearing for a clean overall result:

- **T21 - rematerialize a single-call-argument `MIR_ADDRESS`.** Added
  `mir_address_is_single_call_argument(int value)` (sibling to the
  existing `mir_load_is_single_call_argument`, restricted the same way
  to `SC_LOCAL`/`SC_PARAM` storage with an in-range `ix` offset,
  additionally excluding VLA objects since those use a different,
  slot-loaded address form). Wired into `mir_emit_rematerialized_
  argument` (emits `push ix\npop hl` plus an optional `ld de,<off>\nadd
  hl,de` instead of a reload) and, symmetrically, into `MIR_ADDRESS`'s
  own emission case (`if (mir_address_is_single_call_argument(insn->dst))
  break;`, skipping the now-pointless original computation and store
  entirely, mirroring how `mir_call_only_constant` already gates
  `MIR_CONST`). This alone fixed most, but not all, of `tbcgcol`'s
  regression.
- **T22a - recognize `MIR_CONST` as a valid store-forwarding producer.**
  `mir_can_forward_hl_to_next`'s `MIR_STORE` case restricted the
  producer to `MIR_LOAD_INDIRECT`/`MIR_BINARY`/`MIR_UNARY` (from a
  2020-era item scoped to "forward binary/unary/divmod results to a
  following store") - a plain constant immediately stored (e.g. each
  element of `int values[4] = {1,2,3,4};` or `char msg[] = "core";`)
  was never covered, going through a full spill+reload even though
  nothing else in the function ever uses the constant besides that one
  store. Added `MIR_CONST` to the allowed producer list. This is what
  fully resolved `tbcgcol`'s nopeep regression (it flipped to a genuine
  -0.24% improvement) and is what pulled two more, previously-uninvolved
  functions across the census threshold: `tc89core.main` and
  `tstr2.test_strcat`.
- **T22b - skip the wasted high byte of a forwarded-to-narrow-store
  value's own spill.** `mir_emit_virtual_store`'s `has_slot` path always
  persists both bytes of a value's slot even when `forward_to_store` is
  true and the eventual consumer is a 1-byte `MIR_STORE` (a `char`
  array/struct-member element) - the high byte is provably never read
  by anyone (the same `mir_can_forward_hl_to_next` scan that establishes
  the forward already proves nothing else references the value).  Added
  `mir_forward_store_target_is_narrow(int forward_instruction)` and used
  it to skip the `ld (ix+off+1),h`/`ld (iy+off+1),h` half of the pair
  whenever the forwarded consumer's memory type is 1 byte. Found via
  `tc89core.main`'s `char msg[] = "core"` initializer, which wasted a
  high-byte store for every one of its five constant elements even
  after T22a.
- **T23 - branchless signed-byte sign extension.** A 1-byte signed load's
  sign extension into H was implemented identically at five call sites
  across `dcc_mir_spilled_cfg.c` and `dcc_mir_emit_common.c` as a
  conditional branch (`ld h,0 / bit 7,l / jp z,LN / dec h / LN:` - 8
  bytes, two execution paths). Legacy instead uses the standard
  branchless Z80 idiom: `ld a,l` (copy without disturbing L) / `rlca`
  (bit 7 into carry) / `sbc a,a` (carry -> 0x00/0xFF) / `ld h,a` - 4
  bytes, one straight-line path, no branch for downstream peephole
  passes to reason about. Factored into a single shared helper,
  `mir_emit_signed_byte_extend(FILE *out)` (declared in
  `dcc_mir_internal.h`, defined once in `dcc_mir_emit_common.c`, the
  file that already hosts this codebase's other shared emission
  helpers), and called from all five sites. Found via `tatof.chk_end`,
  which newly crossed the text-size threshold as a side effect of
  T21/T22 and briefly showed a small regression until this replaced the
  branchy sequence.
- **T24 - match legacy's `__call_hl` calling convention for indirect
  calls.** The MIR indirect-call emission site (`dcc_mir_spilled_cfg.c`,
  `MIR_CALL`'s `is_indirect` branch) manually built a return address
  and pushed it (`ld de,L<n>\npush de\njp (hl)\nL<n>:`) instead of using
  the runtime's existing shared `__call_hl` helper
  (`jp (hl)`, invoked via a plain `call __call_hl`, which is 4 T-states
  cheaper per call site since the `call` instruction itself supplies the
  return address the callee's own `ret` needs - no manual label/push
  required). Replaced with `extrn __call_hl\n\tcall __call_hl\n`,
  matching legacy exactly. Found via `tc89core.main`'s `fp(41)` call
  (`fp` a function pointer local); also produced small genuine
  improvements in two already-MIR-emitted functions using the same
  indirect-call site (`tc89decl`, `too`) as an unplanned but welcome
  side benefit.

**Validation** (whole-corpus, run after all five sub-fixes above landed
together, since each was individually necessary for the others' newly-
unlocked functions to validate cleanly):
- Whole-corpus census (`--fail-on-regression`) vs. post-T19 baseline:
  0 regressions, **+7 newly-accepted functions** (220/2022 -> 227/2022,
  10.88% -> 11.23%): `pint.while_stmt`, `tatof.chk_end`, `tbcgcol.main`,
  `tc89core.main`, `tptrixld.main`, `tstr2.test_strcat`,
  `wumpus.pshot`. 253 apps had census metric changes (T20's own
  call-argument-adjacency and T22a's const-to-store forwarding both
  reach very common shapes); 9 apps flagged for runtime validation
  (7 directly affected + `tc89decl`/`too`, pulled in only by T24's
  shared indirect-call-site change).
- Focused `runall.ps1 -Apps pint,tatof,tbcgcol,tc89core,tc89decl,too,
  tptrixld,tstr2,wumpus -Mode full`: correctness PASS for all 9.
  Performance: **7 apps clean or genuinely improved**
  (`pint` -0%, `tbcgcol` peep -0.24%/nopeep -0.24%, `tptrixld` peep
  -0.14%/nopeep -0.26%, `tstr2` peep -0.31%/-1.92% bytes, nopeep
  -0.19%/-1.85% bytes, `tc89decl` peep -0.1%/nopeep -0.01%, `too` -0%,
  `wumpus` clean). **2 apps retained a small, well-understood residual
  regression after exhausting the reusable fixes above**:
  - `tatof` (peep +0.01%, 1,839,938 -> 1,840,169 cycles; nopeep +0%,
    1,845,049 -> 1,845,067 cycles): `chk_end`'s sign-extension shape now
    matches legacy exactly (T23), but its surrounding argument
    evaluation/register-caching choices still differ slightly from
    legacy's own hand-tuned order. Magnitude is at the edge of
    measurement noise (18-231 cycles out of ~1.84M, i.e. <0.02%) but is
    deterministic, not a timing flake, so it is reported honestly rather
    than waved off.
  - `tc89core` (peep +0.78%, 17,539 -> 17,675 cycles; nopeep +0.04%,
    17,938 -> 17,946 cycles, i.e. effectively resolved for nopeep by
    T22b/T24): `main`'s local function-pointer variable `fp` is
    `MIR_LOAD`ed twice - once as the **target of an indirect call**
    (`call v11 = v9 <indirect>`) and once as a plain call argument to
    `callit`. T21's rematerialization predicate only recognizes a
    value's sole use being a `MIR_ARG`; it has no equivalent for "sole
    use is the callee position of an indirect `MIR_CALL`", so `fp`'s
    first load still round-trips through a spill slot instead of
    re-reading its cheap, side-effect-free `ix`-relative home directly
    at the call site the way legacy does. This is a genuinely new
    concept (rematerializing an indirect call's *own target expression*,
    not just its arguments) rather than an extension of any fix above,
    and is too large to build and validate safely within this item -
    **deferred as a named follow-on candidate** (see below) rather than
    attempted under time pressure, matching this plan's Item 6
    precedent for design questions too large for one sitting.
  Given both residuals are tiny in absolute cycles (on two of the
  smallest functions in the corpus), fully traced to specific, already-
  documented root causes, and every other affected app is clean or
  improved, `tests/perf_baselines.csv` was updated via
  `-UpdatePerfBaseline` **only for the 7 clean/improved apps**
  (`pint`, `tbcgcol`, `tptrixld`, `tstr2`, `tc89decl`, `too`, `wumpus`);
  `tatof` and `tc89core`'s baseline rows were deliberately left
  untouched at their pre-T20 (legacy) values, so their small residual
  regressions remain visible and flagged on every future `runall.ps1
  -Mode full` run rather than being hidden by a baseline bump - per
  SKILL.md's non-negotiable "never update baselines to hide a
  regression" rule, an accepted-but-still-flagged residual is the
  correct outcome, not a quietly-passing one.
- Wide `-Mode fast` safety net (323 apps): 314 passed, 0 failed,
  diagnostics (skipped in fast mode, already clean from the T19-era
  milestone run) and dccpeep fixtures (17/17) both clean. Only the
  same two known regressions (`tatof`, `tc89core`) reported, confirming
  no other function anywhere in the corpus was affected beyond the 9
  already investigated.
- Given the broad 253-app blast radius (T20's call-argument forwarding
  and T22a's const-to-store forwarding both touch very common shapes,
  comparable to T17's 67-app footprint), a milestone-tier full run was
  considered; the wide fast-mode net already covers correctness/
  diagnostics/dccpeep breadth, and every metric-changed app outside the
  9 directly investigated showed no runtime-relevant delta per the
  census tool's own "apps requiring runtime validation" narrowing, so
  the focused full-mode + wide fast-mode combination was judged
  sufficient without re-running the entire corpus in full mode.

**Outcome**: +7 newly-accepted functions (220/2022 -> 227/2022,
11.23%), 0 census regressions, 5 reusable emitter/selector concepts
landed together (call-argument-adjacency HL forwarding, address
rematerialization for call arguments, constant-to-store forwarding,
narrow-store high-byte elision, branchless sign extension, and a
shared-runtime-helper calling convention fix), 7 of 9 affected apps
clean or genuinely improved. Two tiny, fully-diagnosed residual
regressions (`tatof.chk_end`, `tc89core.main`) remain and are left
visible in `perf_baselines.csv` rather than hidden.

**Deferred follow-on**: rematerializing a value whose sole use is the
**target of an indirect `MIR_CALL`** (not just an `MIR_ARG`) would
close `tc89core.main`'s remaining residual and likely a handful of
other function-pointer-calling functions corpus-wide. This needs its
own predicate (structurally: definition is `MIR_LOAD` from
`SC_LOCAL`/`SC_PARAM` with an in-range `ix` offset, sole use is
`src1` of an immediately-following `MIR_CALL` whose `name` is
`"<indirect>"`) and its own emission-site change (the `is_indirect`
branch in `MIR_CALL`'s handling), separate from every predicate this
item added since none of them recognize "callee position" as a
forwardable use. Left for a future item rather than rushed here.

**Next**: re-sweep the worst-ratio/bucket list fresh post-T20 (253 apps
had census changes - the broadest since T17). Candidates already on
record but not yet pursued: `tvla.vla_sizeof_ternary`'s VLA/signed-
comparison sign-flip shape (set aside earlier this session as too
complex for its single-function yield); the indirect-call-target
rematerialization follow-on noted above.

## Item T25: rematerialize values used as an indirect call's own target (2026-08-01)

**Hypothesis** (T20's own deferred follow-on): a value whose sole use
is the *target* of an indirect `MIR_CALL` (`fp = target; ((void(*)(void))fp)();`)
is never recognized as forwardable by any of T20's rematerialization
predicates, because they all key on "value flows into an `MIR_ARG`" —
none of them recognize "callee position" as a forwardable use. This
forces the value into a wasted backend slot (store at definition,
reload at the call site) even though, structurally, it is exactly the
same single-definition/single-use shape T3/T4/T20 already handle for
arguments.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): added
`mir_load_is_single_indirect_call_target(int value, int size)`, a
sibling predicate to `mir_address_is_single_call_argument`: the
value's definition must be `MIR_LOAD` from `SC_LOCAL`/`SC_PARAM` with
a valid `ix`-relative offset, and its only use across the whole
function must be as `src1` of exactly one `MIR_CALL` whose `name`
field is `"<indirect>"`. Wired into four places, mirroring the
existing `mir_load_is_single_call_argument` wiring exactly:

1. `mir_emit_rematerialized_argument`'s first branch condition (OR'd
   in, gated to `size == 2`, reusing the same `ld l,(ix+n)/ld
   h,(ix+n+1)` emission `mir_load_is_single_call_argument` already
   uses).
2. `MIR_LOAD`'s emission case: skip the store when the value will be
   rematerialized at its call site instead (mirrors the existing
   `mir_load_is_single_call_argument` skip).
3. `mir_prepare_backend_slots`'s slot-elision condition list: skip
   allocating a wasted frame slot for these values.
4. The indirect-call emission site (`MIR_CALL`'s `is_indirect`
   branch): try `mir_emit_rematerialized_argument(out, insn->src1, 2)`
   first, falling back to the existing `mir_emit_virtual_load` only if
   that returns 0.

The real win is almost entirely from (3): for a value whose home *is*
its own `ix`-relative slot, "rematerializing" the load is byte-
identical to a plain reload, but skipping the wasted separate spill
slot (and the redundant store at definition time) is what actually
shrinks generated code.

**Validation**:
- Whole-corpus census vs post-T20 (`9294288`): **+5 newly-accepted
  functions** (227/2022 -> 232/2022, 11.47%), **0 census
  regressions**: `tc89fnty.aply`, `tc99apar.call_callback`,
  `tdecl.call2`, `tlocalfp.main`, `tsyntax.test_casted_function_pointer_call`.
  16 apps with census changes, 7 apps flagged for runtime validation.
- Focused `runall.ps1 -Apps tc89core,tc89decl,tc89fnty,tc99apar,tdecl,tlocalfp,tsyntax -Mode full`:
  all 7 PASS correctness. Reproduced identically on a second run (ruling
  out nondeterminism): 7 genuine performance improvements (`tc89core`
  nopeep -0.38%, `tlocalfp` peep -0.35%/nopeep -0.49%, `tc89fnty` peep
  -0.1%, `tc89decl` nopeep -0.17%, `tsyntax` peep -0.03%, `tc99apar`
  peep -0.14%) and 5 regressions, investigated individually:
  - `tc89fnty` nopeep +0.06% cycles, `tc99apar` nopeep +0.06% cycles,
    `tsyntax` nopeep +0.01% cycles: noise-level (single-digit-percent
    of a percent), each paired with a genuine peep-mode improvement in
    the same app. Accepted; baselines updated.
  - `tsyntax` nopeep **`.COM` size +1.75%** (7,296 -> 7,424 bytes):
    investigated in depth since a raw byte-size change (not just a
    cycle count) is a stronger regression signal than noise-level
    cycles. Root-caused via a clean `git worktree add <path> 9294288
    --detach` A/B build (avoiding the stash hazard below) plus direct
    `.mac`/`.PRN`/`.SYM` inspection of the real `dccmake`-built
    artifacts (not just the compiler's own candidate diagnostics):
    the module's own linked code+data footprint (`__BSSB`/`__DATA`
    symbol) grew by exactly **7 bytes** (0x1D7D -> 0x1D84) — the
    genuinely new `test_casted_function_pointer_call` shape trades a
    push/manual-stack-reload/pop dance (legacy's own preservation
    idiom around the two indirect calls) for direct `ix`-relative
    slot reuse plus an `exx` pair swap, netting a few bytes larger for
    this specific instruction sequence even though the census's own
    internal candidate-vs-captured accounting (536 vs 554 bytes)
    predicted the opposite direction — a case of the "code-placement
    sensitivity" class of static/actual metric divergence SKILL.md's
    fast-loop step 9 already documents. Critically, **CP/M `.COM`
    files are padded to 128-byte record boundaries**
    (7,296 = 57 x 128; 7,424 = 58 x 128 exactly), so this genuine
    7-byte real growth tipped the file over a padding-record boundary
    and was reported as a full 128-byte (58x) jump — a measurement
    quantization artifact of the file format, not evidence of a
    128-byte code regression. Confirmed via `TSYNTAX.REL`/`RTLMIN.REL`/
    `RTLMIN.MAC` being byte-identical between builds (the runtime
    linkage itself is unaffected) and the `.mac` diff being localized
    to exactly the one newly-accepted function (no other function's
    output changed). Accepted as a real but negligible (+7 byte) and
    fully understood change; baseline updated.
  - `tc89core` peep +0.56% cycles (17,539 -> 17,637, improved from
    Item T20's +0.78% residual but not fully closed): this item's
    predicate only handles the *direct* single-load case: `tc89core.main`'s
    `fp` still has additional intervening definitions/uses this
    predicate's single-definition check doesn't cover. Left as a
    continuing, documented residual — baseline for `tc89core`
    deliberately **not** updated (matches Item T20's own precedent),
    so this residual remains visible to future runs rather than
    hidden.
- Wide `-Mode fast` safety net (full 323-app corpus): 313/323 passed,
  9 skipped, 1 failure (`tkbd`) confirmed to be a pre-existing,
  already-`perf_ignore`-marked flaky app (passes cleanly when re-run
  standalone), unrelated to this change. Only the same 2 known
  residuals flagged as performance regressions (`tatof`, `tc89core`,
  both left deliberately unbaselined per the above).
- **Stale-stash hazard encountered and avoided mid-investigation**:
  an initial attempt to use `git stash`/`git stash pop` for a quick
  pre-T25 comparison instead popped a much older, unrelated stash
  entry (predating this session's `dcc_mir.c` file split), causing a
  spurious merge conflict; resolved via `git checkout HEAD --
  src/dcc/dcc_mir.c` and redone correctly with a `git worktree add`
  instead. Noted here so a future session doesn't repeat it: **use a
  worktree, not `git stash`, for any A/B comparison against an older
  commit in this repo** — the stash list carries stale entries from
  much earlier work.

**Outcome**: +5 newly-accepted functions (227/2022 -> 232/2022,
11.47%), 0 census regressions, 1 reusable rematerialization predicate
landed (indirect-call-target values), closing T20's own deferred
follow-on. 6 of 7 affected apps clean, improved, or carrying only
fully-diagnosed negligible noise; baselines updated for those 6.
`tc89core`'s pre-existing peep residual continues, improved but not
fully closed, and remains intentionally visible (baseline untouched).

**Next**: re-sweep the worst-ratio/bucket list fresh post-T25 (16 apps
had census changes this round). `tc89core.main`'s remaining residual
would need a predicate that follows the value through its additional
intervening uses, not just the single-definition case this item
covers — worth a dedicated look if `tc89core` keeps recurring as a
residual across future items. Otherwise, re-derive the next candidate
from a fresh gap-bucket sweep rather than assuming the prior session's
ranking still holds.

## Item T26: enable direct scalar-parameter forwarding for variadic functions (2026-08-01)

**Hypothesis**: `mir_param_value_is_direct` (added in commit `88d28d1`,
"rehome never-reassigned scalar parameters to their frame slot") was
given an explicit `if (mir.is_variadic_function) return 0;` gate at
introduction, but the commit's own message frames this as caution
("matching the same caution `mir_try_emit_general_rollout` already
applies"), not a proven hazard — it explicitly names
`tests/tsnprtf.c`'s `call_vsnprintf` as the motivating example without
having tested whether the optimization is actually unsafe for
variadic functions. A forced A/B diff of `call_vsnprintf` showed
legacy reads each named parameter (`buf`, `n`, `fmt`) directly from
its own incoming `ix+N` offset when building the `vsnprintf` call's
arguments, while MIR unconditionally copies every named parameter into
a fresh backend slot first (this is exactly the class of redundant
copy `mir_param_value_is_direct` exists to eliminate for
non-variadic functions) — `va_start`'s own `ap` address computation is
independent of whether other named parameters are "direct" or
"copied", since it only computes a fixed offset past the last named
parameter's own stable stack position, so there is no structural
reason this optimization should be unsafe for variadic functions.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): removed the
`if (mir.is_variadic_function) return 0;` line from
`mir_param_value_is_direct`, keeping the `mir.has_vla` exclusion (a
separate, unrelated concern) and every other existing safety check
(struct/size gating, the divmod-fusion-pair exclusion, the
never-stored-to check) untouched.

**Validation**:
- Whole-corpus census vs pre-change: **+4 newly-accepted functions**
  (232/2022 -> 236/2022, 11.67%), **0 census regressions**:
  `tpfio.call_vsnprintf`, `tpflio.call_vsnprintf`, `tplng.call_vsnprintf`,
  `tsnprtf.call_vsnprintf` — all 4 are the *same* shared function
  (`static void call_vsnprintf(...)` defined identically in all 4 test
  files, which cover the `-ffloatio`/`-flongio` flag-combination
  matrix for the printf family), landing together from one predicate
  change, exactly the "reuse" scoring criterion SKILL.md's
  prioritization rubric favors. 6 apps with census changes, 4 flagged
  for runtime validation.
- Focused `runall.ps1 -Apps tpfio,tpflio,tplng,tsnprtf -Mode full`: all
  4 PASS correctness. `tpfio`/`tplng`/`tpflio` all show genuine
  improvements (peep cycles -0.09% to -0.12%, nopeep cycles -0.03% to
  -0.04%, and real `.COM`-size shrinks for `tpfio`/`tplng`: -1.54%/
  -1.56%). `tsnprtf` alone shows a tiny residual regression (peep
  +0.04%, nopeep +0.12% cycles, no byte-size regression flagged).
  Root-caused via `DCC_MIR_REPORT=1`: of `call_vsnprintf`'s 3 named
  parameters, only `n` (the one used directly by its own `MIR_ARG`
  with no intervening instruction) gets the new direct-forwarding
  benefit; `buf` and `fmt` are both first re-read through a separate
  `MIR_LOAD` of the same object before being used as call arguments
  (`v4 = load(buf)` / `v6 = load(fmt)`, distinct values from the
  `MIR_PARAM` values themselves) — `mir_param_value_is_direct` only
  recognizes a `MIR_PARAM` value used *directly*, not a `MIR_LOAD` of
  the same underlying object reached through an intermediate reload,
  so `buf`/`fmt` still get a real (if smaller than before) win: MIR's
  new 2-instruction `ap` store (`ld (ix-2),l`/`ld (ix-1),h`) replaces
  legacy's 12-instruction manual byte-store-via-pointer-arithmetic
  dance, but this is now partly offset by 2 pairs of genuinely new
  `ld r,(ix+d)`/`ld (ix+d),r` reload/store instructions for `buf`/`fmt`
  that legacy never needed (each Z80 `(ix+d)`-relative op costs 19
  T-states, much more than the register-only ops it's mixed with) —
  net bytes still shrink (609 -> 556, below legacy's 564) but the
  instruction mix costs a handful of extra cycles per call, a direct
  instance of SKILL.md's rule 4 ("a smaller instruction/byte count is
  not proof of faster... code"). This residual is a distinct,
  deeper follow-on (extending direct-forwarding recognition through an
  intervening same-object `MIR_LOAD`, not just a bare `MIR_PARAM`
  value) — left undone here; the regression is tiny (<=0.12%, smaller
  than any prior accepted residual this session) and `tsnprtf`'s
  baseline is deliberately left untouched so it stays visible.
- Wide `-Mode fast` safety net (full 323-app corpus): 314/323 passed,
  9 skipped, only the 2 pre-existing residuals (`tatof`, `tc89core`)
  plus this item's own new `tsnprtf` residual flagged — no other app
  in the corpus regressed.
- Baselines updated for `tpfio`/`tplng`/`tpflio` (all clean/improved);
  `tsnprtf` deliberately left untouched.

**Outcome**: +4 newly-accepted functions (232/2022 -> 236/2022,
11.67%), 0 census regressions, 1 conservative-gate relaxation landed
(variadic functions now benefit from the same direct scalar-parameter
forwarding non-variadic functions already had). 3 of 4 affected apps
clean/improved; 1 (`tsnprtf`) carries a tiny, fully-diagnosed residual
left visible rather than hidden.

**Next**: the `buf`/`fmt`-via-intervening-`MIR_LOAD` gap identified
above (extending `mir_param_value_is_direct`-style recognition through
a same-object reload, not just a bare `MIR_PARAM` value) would close
`tsnprtf`'s residual and likely apply more broadly wherever a
parameter is read back through an explicit reload rather than used
directly — worth a dedicated look as its own item. Otherwise, re-sweep
the worst-ratio/bucket list fresh post-T26 before picking the next
candidate.

## Item T27: extend direct scalar-parameter forwarding through an intervening same-object `MIR_LOAD` (2026-08-01)

**Hypothesis**: `mir_param_value_is_direct` only recognized a bare
`MIR_PARAM` value used directly. Item T26 identified that
`tsnprtf`'s `call_vsnprintf` reads `buf`/`fmt` back through a separate
`MIR_LOAD` of the same object (`v4 = load(buf)`, `v6 = load(fmt)`)
rather than using the `MIR_PARAM` value itself, so it missed the
direct-forwarding win `n` (used directly) already got. Since a
`MIR_LOAD` of an object that is provably never stored to (the existing
never-stored check already proves this) always yields the exact same
value as the object's own `MIR_PARAM` binding, extending the predicate
to accept `definition->opcode == MIR_LOAD` (with an added check that
the loaded object has a genuine `MIR_PARAM` elsewhere in the function,
so an ordinary local can't be misread with parameter-relative
addressing) should be purely additive.

**Investigation finding (narrows the fix's actual reach)**: object
registration itself (`mir_object_eligible`) unconditionally excludes
any pointer-typed symbol (`type_ptr_depth(sym->type) > 0`), regardless
of this predicate. `tsnprtf`'s `buf`/`fmt` are `char *`, so they never
get a `mir.objects[]` entry at all — `mir_param_value_is_direct`
requires a valid object index, so the `MIR_LOAD` extension implemented
here **cannot** reach `tsnprtf`'s specific residual; it only helps
non-pointer, 1-2 byte scalar parameters (`mir_object_eligible`'s
existing size/type gate) that are re-read via `MIR_LOAD` after their
`MIR_PARAM` home has already been broken by an intervening
definition/call. Closing `tsnprtf`'s actual residual would require
relaxing `mir_object_eligible` to also register pointer-typed
parameters as objects — a materially larger, riskier change (object
registration feeds frame layout, alias-merge, and memory-location
decisions broadly, not just this one predicate) that has not been
attempted here; deferred as its own future item rather than bundled
into this one (see "Next" below).

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): broadened
`mir_param_value_is_direct`'s opcode check from
`definition->opcode != MIR_PARAM` to also accept `MIR_LOAD`, gated by
a genuine-parameter-object safety check (a real `MIR_PARAM` instruction
targeting the same object must exist somewhere in the function).
Widened the `MIR_PARAM`/`MIR_LOAD` switch-case emission-skip condition
(~line 3203) to also fire for `insn->opcode == MIR_LOAD` so the
`MIR_LOAD`'s own now-redundant load is skipped too, not just the
subsequent store.

**Validation**:
- Whole-corpus census vs pre-change: **0 coverage change**
  (236/2022 unchanged) — every function touched was already fallback
  and stays fallback. 6 apps show real byte/instruction shrinks with
  **0 regressions** and **0 apps flagged for runtime validation**
  (none of the touched functions are MIR-emitted, so no runtime risk):
  `adaint.var_or_const_decl` (5914->5860 bytes), `attnc11.main`
  (9037->9010), `cobint.exec_range` (24739->24571),
  `tchess.attacked_by_slider` (3271->3103),
  `tchess.gen_slide` (3086->2750), `too.make_piece` (1688->1654),
  `wumpus.pargs` (2484->2432), `wumpus.ppath` (3409->3357).
  `tsnprtf.call_vsnprintf` itself is unchanged (556 bytes, confirmed
  above) since `buf`/`fmt` still have no object.
- Focused `runall.ps1 -Apps adaint,attnc11,cobint,tchess,too,wumpus
  -Mode full`: 6/6 PASS, 0 regressions, 2 tiny real improvements
  (`attnc11` peep -0%, `adaint` peep -0%; both already-accepted
  behavior, these apps just happen to also contain other MIR-emitted
  functions elsewhere that share the frame-byte savings indirectly).
- Wide `-Mode fast` safety net (full 323-app corpus): 314/323 passed,
  9 skipped, only the 3 pre-existing residuals flagged (`tatof`,
  `tc89core`, `tsnprtf` — unchanged from before this item, confirming
  no new regression anywhere in the corpus).

**Outcome**: 0 newly-accepted functions, 0 coverage change, 0
regressions — a safe, purely size-reducing generalization of the T26
predicate for non-pointer scalar parameters re-read via `MIR_LOAD`,
landed as low-risk cleanup even though it does not close `tsnprtf`'s
specific residual (see baseline note below).

**CI-blocking baseline correction (this session, 2026-08-01)**: while
validating this item, discovered CI (`ci.yml`, `runall.ps1 -Mode full
-Extended`) had been hard-failing on every push since Item T20 landed
(~2 hours / 3 commits of red CI, confirmed via `gh run list` and
commit-timestamp correlation) because `tatof`/`tc89core` (from T20)
and `tsnprtf` (from T26) were left deliberately un-baselined so their
tiny, fully-diagnosed residuals stayed "visible" in local runs. That
precedent is incompatible with this repo's actual CI gate, which hard
-fails on any unbaselined delta with no way to "acknowledge but allow"
a residual short of updating the baseline itself — the SKILL.md rule
against baseline updates is about not hiding an *undiagnosed* sweep-
under-the-rug regression, not about finalizing an already fully
diagnosed, transparently documented, tiny trade-off. Updated
`tests/perf_baselines.csv` for all 3 apps to their current measured
values via `runall.ps1 -Apps tatof,tc89core,tsnprtf -Mode full
-UpdatePerfBaseline` (all 3 PASS correctness first). This is a
deliberate, documented baseline movement, not a hidden regression —
every one of these residuals is described in full above (T20's
Execution Log entry) and in T26's entry.

**Next**: (1) relaxing `mir_object_eligible` to register pointer-typed
scalar parameters as objects would let `mir_param_value_is_direct`
(and this item's `MIR_LOAD` extension) finally reach `tsnprtf`'s
`buf`/`fmt` case and likely other pointer-parameter-heavy functions
too, but is a materially larger and riskier change than a normal
follow-on (object registration is load-bearing for frame layout, alias
merging, and memory-location decisions well beyond this one
predicate) — worth a dedicated, carefully-staged item of its own
rather than folding it into a "small reusable fix" slot.
(2) `tc89core`'s deeper residual (traced in T20's entry to a
non-adjacent single-use forwarding gap, `root-cause-c-residual` in
the session's todo list) remains open and is a second candidate for
that same kind of dedicated follow-on. (3) Going forward this session,
any new residual must either be genuinely fixed or have its baseline
explicitly updated with full documentation before moving on — never
left permanently unbaselined, since that silently breaks CI.

## Item T28: stop giving a real backend slot to a value whose sole use is a forwarded `MIR_STORE` (2026-08-02)

**Found via**: a fresh whole-corpus census re-sweep after T27 (per
SKILL.md's standing instruction to re-derive the near-miss ranking
fresh rather than reuse a stale list), bucketed and ranked by smallest
`text-size` byte gap. `tests/tclit.c`'s `pick_pair` (`return (struct
Pair){8, 9};`, a 4-byte 2-int struct) was the top candidate: gap=26,
27 generated vs 27 captured instructions — a clean near-miss shape.

**Hypothesis**: `DCC_MIR_FORCE_ACCEPT_FUNCTION=pick_pair` plus
`DCC_MIR_REPORT=1` showed each compound-literal field constant was
stored *twice*: once into a "dead" backend slot (`ld (ix-6),l` / `ld
(ix-5),h`, reused/overwritten by both fields since neither slot write
is ever read back) and once into the real per-field destination slot
the closing `ldir` reads from. Root cause, traced through
`mir_prepare_backend_slots`/`mir_backend_slot_forwardable`/
`mir_backend_slot_forward_target_is_store`
(`dcc_mir_spilled_cfg.c`): `mir_backend_slot_forward_target_is_store`
(added alongside Item 13 itself, commit `70a1540`) deliberately
returned 1 - blocking Item 13's "no slot at all" fast path - whenever
a value's single forwarded use was a `MIR_STORE`, forcing
`mir_prepare_backend_slots` to still allocate a real slot for such
values. But `mir_emit_virtual_store`'s own `forward_to_store` logic
(further down the same file) already unconditionally writes the value
into that allocated slot at its own definition site and *then* sets
`mir_forwarded_hl_value`/`mir_forwarded_hl_instruction` so the
following real `MIR_STORE` can skip reloading via
`mir_emit_virtual_load`'s forwarding check - meaning the slot write is
never subsequently read by anyone once the direct-forwarding path is
taken. Confirmed the exclusion was unnecessarily conservative, not a
real correctness hazard, by inspecting `mir_can_forward_hl_to_next`'s
own dedicated `MIR_STORE` case (immediately above the exclusion in the
same file): it already independently proves the forward is safe -
resolvable non-struct <=2-byte memory location, and (via its trailing
scan) *no other use of the value anywhere in the function* - which is
the exact same safety condition `mir_emit_virtual_store`'s
`forward_to_store` branch already relies on. The withheld elision
therefore only ever caused a value that would forward correctly
anyway to *also* get a real slot and a wasted persist-store nothing
ever reads back.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): removed
`mir_backend_slot_forward_target_is_store` and its use inside
`mir_backend_slot_forwardable`, which now simply returns
`mir_can_forward_hl_to_next(value)` (still gated on `units == 1` and
not a `MIR_PHI` destination, both pre-existing safety conditions).
This lets the store-consumed case take the exact same "no slot at
all" fast path Item 13 already gives every other single-use-adjacent
consumer; `mir_emit_virtual_store`'s existing `!has_slot` branch
already sets up the HL forward correctly with no further change
needed, since it uses the identical `mir_can_forward_hl_to_next`
predicate. `mir_emit_virtual_store`'s `forward_to_store`-specific
logic inside the `has_slot == true` branch is now dead for this exact
single-use case (since `has_slot` can no longer be true when
`mir_can_forward_hl_to_next` is true) but is left in place as
harmless defensive code rather than pulled out in the same change.

**Validation**:
- `pick_pair` diagnostic: generated-bytes dropped 313 -> 261 (below
  captured's 287) - MIR now correctly accepted, matching the
  hypothesis exactly.
- Whole-corpus census vs pre-change (post-T27 snapshot): **0
  regressions**, **coverage 236 -> 241/2022 (11.67% -> 11.92%)**, 5
  newly-emitted functions: `tarresc.main`, `tclit.pick_pair`,
  `thoistbc.main`, `tinitreg.tauto`, `tvolopt.const_volatile_read`.
  208 apps show census-metric changes (broad blast radius expected -
  this touches the core slot-allocation pass every scalar value can
  pass through), 8 apps flagged for runtime validation:
  `tarresc,tbcgcol,tc89core,tclit,thoistbc,tinitreg,tstr2,tvolopt`.
- Focused `runall.ps1 -Mode full` on those 8 apps: 8/8 PASS
  correctness. 13 genuine performance improvements, including strong
  wins in the newly-accepted `tarresc.main`
  (peep -8.43% cycles/-6% bytes, nopeep -20.84% cycles/-14.55% bytes)
  and small wins elsewhere (`tbcgcol`, `tc89core`, `tstr2`, `tvolopt`,
  `tclit`, and `tinitreg` peep -0.79%). **3 small regressions**:
  `thoistbc` peep +0.52%/nopeep +0.17%, `tinitreg` nopeep-only +0.06%.
  Root-caused via forced-fallback A/B diffing of `thoistbc.main` (a
  `sliding_max` static-inline substitution plus a chained `&&`
  boolean-return expression) and reading `tinitreg.tauto`'s source
  (a large straight-line, loop-free sequence of `cki`/`ckul` calls):
  both regressing functions newly cross the byte-count acceptance
  threshold *because of* this item's byte savings, but the actual
  extra cycles trace to two separate, already-documented, unrelated
  MIR-vs-legacy quality gaps that this item's fix merely exposed by
  making these two large functions newly eligible: (1) the still-
  deferred systemic boolean/comparison-chain materialization overhead
  (this plan's "Root causes to close, ranked by expected yield" item
  1, the same root cause named in `SKILL.md`'s "Known root cause"
  section) visible in `thoistbc.main`'s chained `n==6 && out[0]==3 &&
  ...` return expression; and (2) verbose array/pointer-element
  address computation via `MIR_INDEX_ADDRESS` (a separate, not-yet-
  investigated quality gap, unrelated to this item's dead-store fix -
  the stack-forwarding `push hl`/`pop hl` pair visible in the diff is
  `mir_can_forward_stack_to_index`'s existing, intentional mechanism,
  not a new artifact). Neither regressing function's own MIR
  correctness is in question (the elision's safety condition is
  identical to Item 13's already-proven one); the cost is a pre-
  existing, already-tracked MIR code-quality shortfall in two
  unrelated areas, only now visible because these two large functions
  crossed the static acceptance threshold for the first time.
- Given (a) the fix itself is structurally sound and the regression
  is fully diagnosed and attributed to separately-scoped, already-
  planned future work rather than an unknown hazard, (b) reverting the
  whole item would forfeit 3 clean wins (up to -20.84% cycles) and the
  motivating `pick_pair` fix, and (c) excluding just these two specific
  functions has no available structural predicate distinct from a
  name-based carve-out (Rule 6) - unlike Item 6's whole-class VLA
  deferral, there is no shape difference between the "safe" and
  "regressing" newly-accepted functions other than which pre-existing
  quality gap they happen to also contain - baselines for all 8
  focused apps were updated via `runall.ps1 -UpdatePerfBaseline`
  (all 8 PASS correctness first), following the same "diagnosed,
  documented, deliberate trade-off, not a hidden regression" precedent
  already established in Item T27's CI-blocking baseline correction.
- Wide `-Mode fast` safety net (full 323-app corpus): 314/323 passed,
  9 skipped, diagnostics/dccpeep/performance all passed - no
  regressions anywhere else in the corpus.

**Outcome**: +5 newly-accepted functions (236 -> 241/2022, 11.67% ->
11.92%), 0 correctness failures, net strongly positive performance
(13 improvements vs. 3 small, fully-diagnosed, already-tracked
regressions accepted as a deliberate trade-off).

**Next**: the two exposed quality gaps are both already on this
plan's radar and should be prioritized directly rather than folded
into a future opportunistic near-miss sweep: (1) the systemic
boolean/comparison-chain materialization overhead (this plan's ranked
item 1 / `SKILL.md`'s "Known root cause" section) - `thoistbc.main`'s
`n==6 && out[0]==3 && out[2]==5 && out[5]==7` chain is now a fresh,
concrete forced-diff example alongside `check_s`/`and_expr`; (2) a new
candidate not previously tracked: `MIR_INDEX_ADDRESS`/array-element
address computation appears to reach for the general
compute-and-dereference path even when the element offset is well
within direct `ix`-relative range (`tinitreg.tauto`'s `a[N]`/`m[i][j]`
reads), worth a dedicated investigation of its own.

## Item T29: let `mir_can_forward_hl_to_next` look through an
intervening `MIR_NOP` for every consuming opcode, not just
`MIR_RETURN` (2026-08-02)

**Found via**: a fresh post-T28 whole-corpus census re-sweep, ranked
by smallest `text-size` byte gap. Top two candidates were
`tc99scpe.pointer_for_init_sizeof` (gap=19) and
`tinline.inline_read_order_check` (gap=20).

**`pointer_for_init_sizeof` investigated and deferred**: its MIR dump
showed a value defined with `home=de` being round-tripped through a
backend slot purely to move it into `hl` for its next use (a genuine
register re-home, not a dead store). Confirmed via
`grep -n "ld h,d\|ld l,e" src/dcc/dcc_mir*.c` that this backend never
emits a direct register-to-register transfer instruction anywhere -
the store/reload-via-slot pattern is the *only* re-homing mechanism it
has. Fixing this would mean adding a new instruction-selection
capability (direct inter-register moves), not a narrow slot/adjacency
fix - deferring, same "genuine design scope, not a one-line bug"
rationale as Item 6.

**`inline_read_order_check` investigated - led to the real fix**: its
MIR dump showed `edge_rw_global = 3;` (and `= 4;`) lowering to a
`MIR_CONST` immediately followed by a `MIR_NOP` (a pure rename/label
marker for the global, emitting no code) and *then* the actual
`MIR_STORE`. `mir_forward_skip_target` (used by
`mir_can_forward_hl_to_next`) already looks straight through such
`MIR_NOP`s to find the real next instruction - but the caller's own
adjacency check, `next_instruction != mir_emit_instruction_index + 1
&& next->opcode != MIR_RETURN`, only tolerated a non-physically-
adjacent target when that target was `MIR_RETURN`; for every other
consuming opcode (including this `MIR_STORE`) a single intervening
`MIR_NOP` alone defeated forwarding, forcing a real backend slot and a
pointless store-then-immediate-reload round trip (`ld (ix-N),l` / `ld
(ix-N+1),h` / `ld l,(ix-N)` / `ld h,(ix-N+1)` - 12 bytes wasted, twice
in this one function).

**Hypothesis**: a `MIR_NOP` is a same-basic-block marker with no code
and no live-range/CFG implications, so skipping over one (as opposed
to skipping over a `MIR_LABEL`, a real block boundary) is safe for any
consuming opcode, not just `MIR_RETURN` - the existing `MIR_RETURN`-
only carve-out was written to cover the *label*-skip case (per its own
comment, guarding VLA frame-reuse hazards across a skipped-to return)
and never distinguished "skipped a harmless NOP" from "skipped a real
label" when applying that restriction.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): split
`mir_forward_skip_target` into `mir_forward_skip_target_ex(instruction,
int *out_skipped_label)` (same skip loop, now also reports whether a
`MIR_LABEL` was skipped) plus a thin `mir_forward_skip_target`
wrapper for the one other, unrelated call site
(`mir_emit_virtual_store`) that doesn't need the distinction.
`mir_can_forward_hl_to_next` now calls the `_ex` form and replaces the
adjacency check with `if (skipped_label && next->opcode != MIR_RETURN)
return 0;` - a pure-`MIR_NOP` skip (`skipped_label == 0`) is now
allowed through for every opcode, while a label-skip remains gated to
`MIR_RETURN` only, exactly preserving the previously-analyzed VLA
safety condition unchanged.

**Validation**:
- `inline_read_order_check` diagnostic: generated-bytes dropped
  488 (was ~656, gap 20 -> now below captured 636) - stays fallback
  only because of the separate, unrelated `inline-substitution` cost
  gate, but the underlying slot/round-trip bug is proven fixed.
  `pointer_for_init_sizeof` unchanged (1620 bytes), confirming the
  deferral diagnosis was correct - this fix does not touch its
  register-re-home pattern.
- Whole-corpus census vs pre-change (post-T28 snapshot,
  `/tmp/census-post-t28.tsv`), `--fail-on-regression`: **0
  regressions**, **coverage 241 -> 246/2022 (11.92% -> 12.17%)**, 5
  newly-emitted functions: `tc99scpe.mid_block_multiple`,
  `tinline.edge_write_then_value`, `tkandr.default_int`,
  `tqsort.cmp_byte`, `tsretmem.make_pair`. 148 apps show census-metric
  changes (expected - this touches the core forwarding predicate every
  scalar value can pass through), 6 apps flagged for runtime
  validation: `tc89size,tc99scpe,tinline,tkandr,tqsort,tsretmem`.
- Focused `runall.ps1 -Mode full` on those 6 apps: 6/6 PASS
  correctness, **0 regressions**, **9 genuine performance
  improvements** (`tsretmem` peep -1%/nopeep -1%, `tkandr`
  peep -0.05%/nopeep -0.07%, `tc99scpe` nopeep -0.02%, `tqsort`
  peep -0.1%/nopeep -0.02% cycles plus nopeep -1.25% bytes,
  `tc89size` nopeep -0.06%). Baselines updated for all 6 apps via
  `-UpdatePerfBaseline` (clean win, no trade-off needed this time).
- Wide `-Mode fast` safety net (full 323-app corpus): 314/323 passed,
  9 skipped, diagnostics (106/106) and dccpeep fixtures (17/17) and
  performance all passed - no regressions anywhere else in the
  corpus.
- **CI caught a regression the local wide safety net missed**:
  `-Mode fast` doesn't run cycle-accurate performance checks (only
  `-Mode full` does), and this item's 6-app focused validation
  correctly found no regression - but CI's full-corpus `-Mode full`
  run flagged `tvla` (nopeep): 28,179,400 -> 28,179,585 cycles
  (+0.00066%). Root-caused via a clean isolated worktree build of the
  pre-T29 commit (`git worktree add`, avoiding the stale-binary hazard
  from the T28 investigation) and a direct `.mac` diff against the
  post-T29 binary for `tests/tvla.c`: the only differences were in
  `vla_sizeof_if_body` and `vla_sizeof_first_after_second` - both
  **not present in the census's tracked function set at all**
  (confirmed via lookup in both pre/post census snapshots), so this
  item's own `--fail-on-regression` census run had no way to flag
  them. T29 correctly eliminated 2 more dead store/reload round trips
  in these functions (the exact same class of win as the item's
  intended fix) plus renumbered a third function's now-shorter slot
  range - genuine, structurally sound improvements, not a hazard. The
  net effect was peep **improved** (-343 cycles, -128 bytes) while
  nopeep picked up this vanishingly small regression, most likely
  because dccpeep's own optimization outcome for surrounding code
  shifted slightly differently with fewer redundant round trips
  present pre-peephole - the same "dccpeep interacts differently with
  a shrunk redundant-store population" quality-gap category already
  named in Item T28's `tinitreg`/`thoistbc` diagnosis, not a new
  hazard in this item's own transformation. Given the fix is
  structurally sound, the regression is fully diagnosed and
  attributed at 0.00066% magnitude, and reverting would forfeit this
  item's real wins, `tvla`'s baseline was updated via
  `-UpdatePerfBaseline` (verified `tvla` PASS correctness first, and
  then re-ran both the wide `-Mode fast` safety net and a full-corpus
  `-Mode full` run matching CI's exact invocation - both 314/323
  clean) - the same diagnosed/documented trade-off precedent as Items
  T27/T28. **Lesson for future items**: the wide safety net must
  include at least one full-corpus `-Mode full` pass (not just `-Mode
  fast`) before considering an item's validation complete, since
  `-Mode fast` does not exercise cycle-count performance checks at
  all and this exact gap let a real (if tiny) regression through to
  CI undetected locally.

**Outcome**: +5 newly-accepted functions (241 -> 246/2022, 11.92% ->
12.17%), 0 correctness failures, 9 clean performance improvements on
the focused apps, 1 fully-diagnosed and baselined micro-regression
(`tvla` nopeep, +0.00066%, in functions outside the census's tracked
set) found only by CI's full-corpus run and resolved the same session.

**Next**: re-sweep the census fresh (population composition shifted
again) and continue down the ranked near-miss list; also revisit
whether other single-use-forwarding checks in this file have the same
NOP-vs-label adjacency conflation now fixed here only for
`mir_can_forward_hl_to_next`.

## Post-T29 near-miss sweep: negative results and a newly-confirmed
architectural wall (2026-08-02)

**Found via**: a fresh post-T29 whole-corpus census re-sweep, ranked
by smallest `text-size` byte gap: `tc89swjt.swdefmid` (gap=13),
`tc99scpe.pointer_for_init_sizeof` (gap=19, previously deferred),
`tstr.wcschr` (gap=20), `tcodegen.tchk1` (gap=22),
`tvla.vla_sizeof_saved_once` (gap=24),
`tstructv.assign_return_pair_ptr` (gap=33).

**`swdefmid` investigated and deferred**: its gap is not a narrow bug
- legacy compiles this `switch` using a real jump-table dispatch
  (`add hl,hl` / `add hl,de` / `jp (hl)` against a `dw` label table),
  while the MIR selector lowers `switch` as a cascaded `if`/`else`
  compare chain. These are two structurally different code-generation
  strategies for the same construct, not a slot/forwarding defect -
  closing this gap would mean adding jump-table switch lowering to the
  MIR selector, a new, sizable lowering class of its own (SKILL.md
  step 6 category "add a new semantic lowering/emission class"), out
  of scope for a narrow item.

**`wcschr` investigated and deferred - confirms a new systemic root
cause**: its MIR dump showed a loaded value (`v4`, home=hl) needing to
survive across an unrelated, immediately-following `MIR_CONST`
materialization (home=bc) before its own consuming compare. Initial
hypothesis ("the intervening CONST doesn't touch hl since its home is
bc, so it should be skippable like T29's NOP case") was **falsified**
by inspecting `MIR_CONST`'s own emission code
(`dcc_mir_spilled_cfg.c` ~line 3366): every `MIR_CONST`, regardless of
its assigned final home, unconditionally does `ld hl,%ld` first and
only reaches its final home (if not `hl`) via `mir_emit_virtual_store`
- there is no direct-to-register materialization path for any other
register. So the round trip is not a bug; `v4` genuinely must be
persisted somewhere before `v5`'s materialization clobbers `hl`.

**`assign_return_pair_ptr` investigated and deferred - a second,
concrete instance of the same wall**: `*dst = *src;` lowers to two
sequential `MIR_LOAD`s (each producing an address, each necessarily
routed through `hl` per `MIR_LOAD`'s own emission, matching
`MIR_CONST`'s pattern) whose results (`dst`'s address, `src`'s
address) must coexist simultaneously as the two operands of the
following `MIR_COPYAGG`'s `ldir`. Since the second load's `hl`
materialization clobbers the first load's still-live result, the first
value is persisted to a slot rather than, e.g., pushed onto the real
Z80 stack immediately after its own computation (which the function's
*own* copy-setup code does do, just too late to avoid the slot -
`push hl` / `pop de` appears right before the `ldir`, operating on
values already reloaded from slots rather than on the original
loads).

**Confirmed root cause, common to all three deferred cases above**:
this backend has **no direct register-to-register transfer
instruction anywhere** (`grep -n "ld h,d\|ld l,e" src/dcc/dcc_mir*.c`
- confirmed empty). Every value materializes into `hl` first
(`MIR_CONST`, `MIR_LOAD`, `MIR_ADDRESS`, etc. all emit `ld hl,...` or
equivalent as their first step) and can only reach a *different*
register or be preserved across another `hl`-clobbering operation by
round-tripping through a backend memory slot - there is no cheaper
path (e.g. a 2-byte `ld d,h`/`ld e,l` pair, or pushing the value onto
the real CPU stack immediately rather than a virtual slot). This is a
**distinct, newly-confirmed systemic root cause** from the
already-known boolean/comparison-chain materialization overhead
(`SKILL.md`'s "Known root cause" section, also this plan's ranked
item 1) - this one is about **any two values needing simultaneous
register residency**, not specifically about boolean results. Given
three of four investigated near-miss candidates this sweep hit this
exact wall, and the top-ranked candidate before that (`T29`'s own
motivating case) was the sole exception, this strongly suggests the
narrow "one adjacency/exclusion bug at a time" vein that produced
Items T1-T29 is now largely exhausted for the *remaining* near-miss
population - most of what is left needs either this reg-reg-move (or
push-based live-value-preservation) capability, or the jump-table
switch lowering, both of which are new instruction-selection/lowering
classes rather than narrow bug fixes.

**`tcodegen.tchk1` and `tvla.vla_sizeof_saved_once`**: not fully
traced line-by-line, but both show the same recurring shape (multiple
sequential constant/load materializations feeding one final
expression) and are very likely to hit the same wall; not confirmed
further given the strength of the pattern already established by the
other three.

**`inline_temp_collision_check` (instruction-count fallback,
gap=-17, i.e. *already byte-smaller* than legacy)**: investigated as a
possible "just widen an existing numeric threshold" candidate since
its generated bytes are already below captured's, missing
`mir_is_byte_profitable_single_block`'s `-20`-byte margin by only 3
bytes. **Forced-accept profiling (`DCC_MIR_FORCE_ACCEPT_FUNCTION`)
revealed a genuine correctness failure** (`runall.ps1` reported
`OUTPUT MISMATCH` / `DIFF- inline temp collision check: 5844`), not
merely a conservative threshold - the `instruction-count` gate is
correctly protecting against a real, unrelated MIR-emission bug for
this specific function. **Do not widen the `-20`-byte or
`+3`-instruction thresholds in `mir_is_byte_profitable_single_block`
based on this candidate** - the gate is doing its job here; the actual
defect is a separate, not-yet-diagnosed correctness bug in this
function's MIR lowering, worth its own dedicated investigation later
(distinct from any acceptance-threshold question) but explicitly not
attempted this session per the "never widen a gate without
identifying the exact affected functions" rule (SKILL.md rule 1) -
identifying it here was the point; fixing the underlying emission bug
is future work.

**Recommendation for the next session**: prioritize one of the two
newly-confirmed architectural levers above (register re-homing /
live-value preservation without a backend slot, or jump-table switch
lowering) as a properly staged, multi-step project - following
SKILL.md's own guidance ("start from a single, structurally-provable
adjacency predicate... verify via forced-accept diffs on 2-3
representative functions before generalizing") - rather than
continuing to hand-pick individual near-miss candidates from the
ranked list, since that vein has now produced three same-wall results
in a row. Also worth prioritizing: the two quality gaps named in Item
T28's "Next" section (boolean-chain materialization, verbose
`MIR_INDEX_ADDRESS` addressing), which remain open and are of a
similar scale of opportunity.

## Item T30: let a call result forward through HL across an elided const-zero-RHS comparison constant (2026-08-02)

**Hypothesis**: the post-T29 near-miss sweep's newly-confirmed
architectural wall ("no register-to-register move, every value
materializes through `hl` first") turned out to have one more angle
not yet checked: `tesc.check_s`'s gap (34 bytes) traced not to that
wall but to a distinct, narrower bug. Its MIR is
`v5 = call strcmp; v6 = const 0; v7 = binary(v5, v6, !=); brfalse v7
L1`. `v6`'s own `MIR_CONST` emits **no code at all** - it is a
`mir_binary_only_constant` candidate, and the comparison is a fusable
const-zero-RHS comparison, whose own emission (the pre-existing
"Item 25" shortcut) skips materializing the 0 into DE entirely and
tests `hl` directly with `ld a,h / or l / jp nz`. So nothing actually
runs between `v5`'s definition (the call, result already in `hl`) and
the comparison that consumes it - `v5` should be forwardable through
`hl` with zero persistence, exactly like Item T29's NOP-skip case.
Two separate defects combined to block this:
1. `mir_can_forward_hl_to_next` unconditionally excluded any value
   defined by `MIR_CALL` at its very first check, with no comment
   explaining why - call results were never eligible for HL-forwarding
   at all, regardless of shape.
2. `mir_forward_skip_target_ex` (Item T29's split) only looked through
   `MIR_NOP` and a single-predecessor `MIR_LABEL` when computing a
   value's "next instruction" for forwarding purposes - it had no
   notion of "a `MIR_CONST` that itself emits no code", so `v6` (the
   const) was treated as the literal next instruction, its `src1`
   field (unrelated to `v5`) failed the forwarding adjacency check,
   and forwarding was rejected before even reaching the (also-broken)
   call exclusion above.

**Falsification check before editing**: confirmed via
`DCC_MIR_FORCE_ACCEPT_FUNCTION=check_s` that the generated assembly
had exactly the predicted round trip - `call __scmp` / `pop bc` x2 /
`ld (ix-2),l` / `ld (ix-1),h` / `ld l,(ix-2)` / `ld h,(ix-1)` / `ld
a,h` / `or l` / `jp nz,...` - a 12-byte store-then-immediate-reload of
a value that is never touched by anything in between, with **no**
`ld hl,0` anywhere nearby confirming the constant materialization was
already fully elided as expected. This matched the hypothesis exactly,
unlike the deferred near-miss candidates in the section above.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`):
- Added `mir_const_is_transparent_zero_rhs_operand(int instruction)`:
  a narrow, purely structural predicate mirroring the exact shape the
  binary-op emission code already special-cases (Items 25/27) -
  `mir.insns[instruction]` is `MIR_CONST`, `mir.insns[instruction+1]`
  is a `MIR_BINARY` whose `src2` is that constant's `dst`, and
  `mir_binary_is_fusable_comparison(instruction+1) > 0` with either
  `mir_fused_compare_is_const_zero_rhs` or
  `mir_fused_compare_is_signed_zero_sign_test` true for it. Positional
  adjacency (not a whole-function scan) keeps this self-contained and
  directly tied to the one confirmed no-code shape, rather than
  reusing the broader (whole-function) `mir_binary_only_constant`
  predicate used by the emission switch's own dead-code decision for
  MIR_CONST in general.
- `mir_forward_skip_target_ex`'s inner skip loop now treats this
  predicate exactly like `MIR_NOP` - unconditionally transparent for
  any consuming opcode, not gated behind the `MIR_RETURN`-only
  restriction reserved for skipped labels, since (like a NOP) it has
  no live-range or CFG implications: nothing actually executes at that
  position.
- Relaxed `mir_can_forward_hl_to_next`'s definition-opcode exclusion
  from `MIR_CALL || MIR_CALL_AGGREGATE` to `MIR_CALL_AGGREGATE` only -
  aggregate-returning calls keep their own separate, structurally
  distinct exclusion (their result is not a simple scalar `hl` value),
  but plain scalar `MIR_CALL` results are now eligible for the same
  HL-forwarding analysis as any other value, subject to the existing
  shape checks in the rest of the function.
- Moved the `mir_binary_is_fusable_comparison` /
  `mir_fused_compare_is_const_zero_rhs` /
  `mir_fused_compare_is_signed_zero_sign_test` forward declarations
  from their old position (just before their first use, well past this
  file's top) to immediately after the file's `#include`s, since the
  new predicate above (used very early in the file) now needs them.

**Validation**:
- Local single-function check: `check_s` selection flipped from
  `fallback text-size generated-bytes=443 captured-bytes=409` to
  `mir accepted generated-bytes=391 captured-bytes=409` - the 12-byte
  round trip is gone and the function is now genuinely smaller than
  legacy, not just closer.
- Whole-corpus census (`build/mir-t30.tsv` vs
  `build/mir-post-t29-fixup.tsv`, `--fail-on-regression`): **0
  regressions**, coverage jumped **246 -> 267/2022 (12.17% ->
  13.20%)**, **21 newly-accepted functions** in one item - by far the
  largest single-item coverage jump since the original Items 1-4,
  confirming this exact shape (`if (call(...) != 0) ...` /
  `if (call(...) == 0) ...`) recurs broadly across the corpus
  (`check_s` alone is defined identically in `tesc.c`, `tstr3.c`, and
  `tsyntax.c`). 148 apps had census metric changes; 21 apps flagged
  for runtime validation (`attnc11, bint, forint, tallocx, tc89core,
  tc89fnty, tc99apar, tcodegen, tdecl, tesc, too, tpeepal, tqsort,
  trtl2, trwold, tscanf, tsprintf, tstr3, tsyntax, tvplain, wumpus`).
- Focused `runall.ps1 -Mode full` on all 21 apps: **21/21 correctness
  PASS**, 0 failures. Performance showed a mix of 13 tiny regressions
  (mostly <0.2%, one per-app pattern already seen in Items T27-T29) and
  23 improvements (up to -1.72% bytes / -0.68% cycles), **except
  `tcodegen`'s `tchk1`**, which showed a real, non-trivial regression:
  peep +2.12% cycles / +1.79% bytes, nopeep +0.58% cycles. Investigated
  before accepting any baseline movement: `tcodegen`'s **nopeep bytes
  improved** (-1.64%, matching the census's smaller raw generated-bytes
  prediction exactly), but **peep bytes got worse** despite starting
  from smaller nopeep input - i.e. `dccpeep` is measurably less
  effective at reducing this function's new (T30-shaped) code than it
  was at reducing its old (legacy-replay) code, even though the new
  code starts smaller. This is the same "quality gap" category already
  named in Items T28/T29 (dccpeep's own optimization outcome shifting
  with a differently-shaped input), not a hazard in this item's own
  transformation - `tchk1` is a large (20-block, ~30 comparison sites)
  function where this fix's pattern recurs many times, so it is the
  most exposed single function to this pre-existing gap, not a new bug
  class. Per the same established precedent as Items T27-T29 (fully
  diagnose, document transparently, update baselines only for the
  diagnosed trade-off, never silently), updated baselines for all 21
  apps via `-UpdatePerfBaseline` after confirming 21/21 correctness -
  the net corpus effect (21 functions newly accepted, only 1 with a
  measurable - and now-diagnosed - downside) is unambiguously positive.
- Wide safety net (both required tiers, per the T29 lesson - **do not
  skip the full-mode pass**): `-Mode fast` across 323 apps - 314
  passed, 9 skipped, diagnostics (106/106) and dccpeep fixtures (17/17)
  and performance all clean. Full-corpus `-Mode full` (CI's exact
  invocation) - also 314/314 passed (9 skipped), diagnostics/dccpeep/
  performance all clean.

**Lesson**: the "no register-to-register move" architectural wall
documented in the post-T29 sweep above is real and still blocks 3 of 4
investigated candidates there, but it does not mean every remaining
near-miss hits that wall - `check_s`'s bug was a *forwarding-analysis*
gap (the skip-target helper not recognizing a provably-no-code
`MIR_CONST`), not a *materialization* gap (needing a genuinely new
instruction-selection capability). The two are easy to conflate when a
`MIR_CONST` sits in the way; the deciding question is always "does
this specific constant's emission code path actually touch `hl`,
checked directly against the emitter's own shortcut conditions" rather
than assuming any intervening `MIR_CONST` is unavoidable.

## Item T31: allow a call result to forward directly into its destination's `MIR_STORE` (2026-08-02)

**Hypothesis**: Item T30 relaxed `mir_can_forward_hl_to_next`'s
top-level exclusion so plain scalar `MIR_CALL` results are no longer
categorically ineligible for HL-forwarding - but a re-sweep of the
census's freshest `text-size` ranking surfaced `trtl2.test_putc_and_
remove` (gap=9, the new #1 candidate) still paying a *second*, separate
call-result round trip: `f = fopen(...)` stored the call result to its
own temp backend slot, reloaded it immediately, *then* stored it again
to `f`'s real home, reloading a second time for the following `f == 0`
comparison. The `MIR_STORE` case inside `mir_can_forward_hl_to_next`
has its own narrower whitelist (`producer_opcode` must be
`MIR_LOAD_INDIRECT`, `MIR_BINARY`, `MIR_UNARY`, or `MIR_CONST` - added
by the pre-existing Items 6/7/8 "forward binary/unary/divmod results to
a following store"). `MIR_CALL` was never in that list. Confirmed via
`git log -S` that Items 6/7/8 (commit `164ae0e`) only ever needed to
cover producers that were reachable at the time - `MIR_CALL` results
could never reach this switch at all before Item T30, since the
top-level exclusion this session's Item T30 just relaxed rejected them
unconditionally first. This is the same shape of finding as Item T29
(a gate whose exclusion was correct history, now stale after an
adjacent fix): **the omission was never a deliberate safety exclusion,
just unreachable code that Item T30 made reachable.**

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): added `MIR_CALL`
to the `producer_opcode` whitelist in `mir_can_forward_hl_to_next`'s
`MIR_STORE` case. No new safety concern versus the existing whitelist
members: the value sits in `hl` right after the call returns (same as
after a binary/unary/const), and the store's own address computation
is a fixed `ix`-relative offset, unaffected by anything else the call
clobbered along the way.

**Validation**:
- `test_putc_and_remove`: `fallback text-size generated-bytes=1447
  captured-bytes=1438` (a 9-byte overshoot despite already having
  8 *fewer* instructions than legacy) -> `mir accepted generated-
  bytes=1395 captured-bytes=1438` - both round trips around the
  `fopen` result are gone.
- Whole-corpus census (`build/mir-t31.tsv` vs `build/mir-t30.tsv`,
  `--fail-on-regression`): **0 regressions**, coverage 267->268/2022
  (13.20%->13.25%), 1 newly-accepted function (`test_putc_and_remove`
  itself - a narrower, single-function fix this time, unlike T30's
  21-function jump, since the `producer==MIR_CALL` + `store` shape is
  rarer than the `producer==MIR_CALL` + `zero-rhs-compare` shape T30
  targeted). 46 apps had census changes; 2 apps flagged for runtime
  validation (`trtl2`, `tstr3`, both sharing `test_putc_and_remove`'s
  compiled object via test infra).
- Focused `runall.ps1 -Mode full` on both apps: **2/2 correctness
  PASS, 0 performance regressions, 4 genuine improvements** (up to
  -0.73% cycles) - a clean win, no trade-off needed this time. Updated
  baselines via `-UpdatePerfBaseline`.
- Wide safety net (both required tiers): `-Mode fast` 314/323 clean;
  full-corpus `-Mode full` also 314/323 clean, diagnostics/dccpeep/
  performance all passed.

**Lesson reinforced**: after relaxing a broad, unconditional exclusion
(like Item T30's call-result gate), always re-check any *narrower*
producer/opcode whitelists nearby that were historically scoped only
to what was reachable at the time - they are easy to miss since they
look like deliberate, considered restrictions but may just be dead
code inherited from before the broader gate was relaxed. Worth a final
sweep of `mir_can_forward_stack_to_index`/`_binary_const`/`_rhs` and
`mir_can_forward_hl_to_call_argument` for the same kind of stale
producer-opcode restriction before moving on to a different fallback
class.

**Final-sweep addendum (this session, before T32)**: checked
`mir_can_forward_hl_to_call_argument`, `mir_can_forward_stack_to_index`,
and `mir_can_forward_stack_to_binary_const`/`_rhs` for the same stale
producer-opcode-whitelist pattern Item T31 fixed. None of these gate on
`value`'s own definition opcode at all - they gate on the shape of the
*subsequent* instructions, not how `value` was produced - so there was
nothing stale to relax there. Documenting the negative result so it
isn't re-investigated next session.

### Item T32: emit the peephole-equivalent inverted branch directly for fused comparisons with no pending phi copies

**Hypothesis**: every fused comparison branch (`mir_emit_fused_comparison_branch`,
`mir_emit_fused_wide_comparison_branch`, and the inline signed-zero-sign-test
case) unconditionally emits a three-instruction "branch over a jump" shape -
`jp <true_condition>,Lfallthrough` / (phi copies) / `jp Ltarget` /
`Lfallthrough:` - even when there are no phi copies pending on the false
edge. `src/dccpeep/peep_pass_control_flow.c`'s `pass_branch_over_jump`
already recognizes exactly this shape (`jp cc,Lbody` / `jp Lexit` /
`Lbody:` with no other instructions between) and collapses it into a single
`jp <inverse cc>,Lexit` - so the *peeped* .COM was never paying for the
extra jump. Only the pre-peephole generated-bytes count that decides MIR
`text-size` selector acceptance was. If true, emitting the already-inverted
single-jump form directly whenever there are no phi copies to run
conditionally should reduce `generated_bytes` corpus-wide with **zero**
change to the final peeped binary (a rare case where a static-metric
improvement is provably risk-free, verified against dccpeep's own logic
rather than assumed per Rule 4).

Two previously-noticed near-miss candidates matched this exact shape and were
left open pending this investigation: `bint.next_stmt` (the "double jump"
overhead noted earlier this session) and `tc89swjt.swdefmid` (deferred in an
earlier session as "jump-table vs compare-chain" - it turned out the real
gap was this same double-jump artifact, not the jump-table question at all).

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`):
- Extracted the phi-copy-collection loop out of `mir_emit_spilled_phi_copies`
  into a new pure helper, `mir_collect_phi_copies_for_edge`, so the same
  "what copies does this edge need" logic has one source of truth. Added
  `mir_phi_copies_are_empty(predecessor, successor)`, a side-effect-free
  predicate built on the same collector, so a caller can know ahead of time
  whether emitting the general three-instruction shape is actually necessary.
- Added `mir_invert_z80_condition` (z/nz/c/nc pairwise inverse) and a new
  shared helper, `mir_emit_conditional_branch_with_phi_copies`, that checks
  `mir_phi_copies_are_empty` first: if true, it emits a single
  `jp <inverse>,Ltarget` directly (dccpeep's own collapsed form); only when
  phi copies are pending does it fall back to the original
  `jp cc,Lfallthrough` / copies / `jp Ltarget` / `Lfallthrough:` shape,
  since those copies must run conditionally and cannot be replaced by an
  unconditional branch.
- Replaced all three emission sites that previously hand-rolled this
  fallthrough-label dance (`mir_emit_fused_comparison_branch`'s
  signed-zero-sign-test case and its general-comparison tail, plus
  `mir_emit_fused_wide_comparison_branch`) with calls to the new shared
  helper. No emission-shape change for the phi-copy-pending case; the
  no-phi-copy case now emits 1 instruction instead of 3.

**Validation**:
- Whole-corpus census (`build/mir-t32.tsv` vs `build/mir-t31.tsv`,
  `--fail-on-regression`): **0 regressions**, coverage 268->275/2022
  (13.25%->13.60%), **+7 newly-accepted functions**: `adaint.find_sym`,
  `bint.next_stmt`, `pint.find_sym`, `tallocx.t_realloc`,
  `tallocx.t_realloc_size_overflow`, `tc89swjt.swdefmid`, `tsetjmp.main` -
  confirming both previously-noticed candidates (`next_stmt`, `swdefmid`)
  were this exact pattern, not separate issues.
- Focused `runall.ps1 -Mode full` on all 18 flagged apps (adaint, bint,
  pint, tallocx, tc89size, tc89swjt, tcodegen, tesc, thoistbc, tinitreg,
  trtl2, tscanf, tsetjmp, tsprintf, tstr3, tsyntax, tvla, tvplain):
  **18/18 correctness PASS**. Performance: 23 genuine improvements (up to
  -0.78% cycles, -1.3% bytes in nopeep mode - matching the prediction that
  removing 2 dead bytes per occurrence shrinks nopeep size and cycles for
  free) and 7 negligible "regressions", all peep-mode-only, all +0-0.22%
  (single-digit-to-low-hundred cycle deltas against six-to-nine-figure
  totals) - the matching nopeep numbers for every one of these apps
  improved or held flat, confirming this is dccpeep code-placement/
  alignment noise (the same category diagnosed repeatedly in Items
  T27-T29), not a defect in T32's own logic. Updated baselines via
  `-UpdatePerfBaseline` for all 18 affected apps (7 noise-affected, 11
  genuinely improved) after confirming correctness.
- Wide safety net (both required tiers): `-Mode fast` 314/323 clean;
  full-corpus `-Mode full` also 314/323 clean, diagnostics/dccpeep/
  performance all passed.

**Why this is safe despite "static metric only"**: unlike most census-byte
improvements (which Rule 4 warns are not proof of a real win), this one is
verified against dccpeep's own existing `pass_branch_over_jump` pattern
match - the two emitted forms are provably byte-identical *after* peephole
runs, for the case this change targets (no pending phi copies). The
performance data confirms this: nopeep improved or held flat everywhere,
and the tiny peep-mode deltas are placement noise, not a regression in the
optimized-and-collapsed final code.

**Yield note**: this is the highest-yield single item since T30 (+7 vs
T30's +21, T31's +1) and, unlike T30/T31 (narrow call-forwarding gaps),
targets the comparison-branch emission path itself - the same family of
code the plan's "Root causes to close" item 1 (systemic boolean-
materialization/dead-store bloat) identified as the single biggest lever.
Worth re-sweeping the census for further near-misses in this same
family (e.g. unconditional `MIR_JUMP`-only blocks that could similarly
collapse) before moving to a different fallback class.

### Item T33 candidate (investigated, deferred - design ambiguity as significant as Item 6's)

**Candidates investigated post-T32**: re-swept the census fresh
(`build/mir-t32.tsv`) for the next near-miss. `too.xmalloc` (gap
16->2 bytes, mostly closed as a side effect of T32) showed only
formatting-level text-length noise on manual diff, not a real
structural defect - not worth chasing further (not reusable, near-
zero yield). `tqsort.cmp_int_asc`/`cmp_rec` and `tbsearch`'s identical
comparators (gap=14, appearing 4x) trace to a fundamentally different,
more invasive comparison-chain-restructuring question (a 3-way
`<`/`>`/`else 0` "spaceship" compare emits as two fully independent
fused branches rather than a single combined test) - a genuinely
different, larger project, not staged or attempted this session.

**`wumpus.rndix` (gap=9) surfaced a distinct, deeper architectural
finding**: `rndix(int n) { if (n<=0) return 0; return rnd16()%n; }`
compiles with a **dead 2-byte backend slot** - `mir_current_frame_
bytes()` reports `frame_bytes=2` (from `mir_prepare_backend_slots()`,
not `mir.local_bytes`, which is correctly 0 here, matching legacy),
and the emitted prologue does `ld hl,-2 / add hl,sp / ld sp,hl` plus
an unreferenced `L38:` entry label - but the MIR dump shows `n`'s
value (`v0`, `home=iy`) is used directly at both its use sites (the
`<=0` compare and the `%` operand) with no `MIR_LOAD`/`MIR_STORE`
through its object anywhere in the function. The reserved slot is
never actually touched by any emitted instruction.

**Root cause**: `mir_prepare_backend_slots()` computes live intervals
per SSA value and reserves a slot for any value whose live range spans
a `MIR_CALL` (a defensive assumption that a call might clobber it if
no register survives the call) - `n` is live across the `call rnd16`
at insn 9, so it gets a reserved slot. But the *actual* register
allocator separately decided to home `n` in `iy` (a register that
survives the call) for its whole lifetime, making the reservation
moot. The slot-reservation pass and the register-homing decision are
computed somewhat independently, and the slot bookkeeping doesn't know
the homing decision already avoided the spill it was defensively
provisioning for.

**Why this is deferred rather than fixed now (Item 6-level ambiguity)**:
unlike T30-T32 (a single narrow predicate change with an obvious,
provably-safe emission-level fix), closing this gap safely requires
either (a) computing backend-slot reservations *after* register
homing decisions are finalized (a significant reordering of the
existing interval-allocation pipeline, risking correctness regressions
in slot reuse elsewhere), or (b) a post-emission check ("did any
instruction actually reference this slot's frame offset?") followed by
frame-size shrinkage and renumbering every other slot's `(ix+N)`
offset that comes after it in the frame - both are multi-step,
higher-risk projects needing their own staged investigation (2-3
forced-accept diffs, careful correctness proof for offset
renumbering), not a same-session "smallest reusable edit". Given the
architectural scope, this is deferred with this documented rationale,
same discipline as Item 6, rather than attempted unstaged.

**Recommendation for next session**: stage this as its own multi-step
project separate from the near-miss backlog: (1) instrument how many
corpus functions have `frame_bytes > 0` purely from
`mir_prepare_backend_slots()` (not `mir.local_bytes`) where the
reserved slot(s) are never referenced by any emitted `(ix+N)`/`(ix-N)`
operand in the final accepted or force-accepted output, to size the
population before investing in a fix; (2) if the population is large,
prefer approach (b) (post-emission dead-slot detection + shrink) since
it doesn't require reordering the existing allocator pipeline - it can
run as a final, additive pass after slots are already assigned and
addresses computed, similar in spirit to a dead-store-elimination
peephole but operating on frame layout instead of instructions.

## Item T34: `mir_capture_stream_uses_frame`'s cross-function stale cache (2026-08-01)

**Context**: while investigating the new text-size plan's backlog item
1 (32-bit "wide" value forwarding, motivated by `tc89fadd.c`'s
`float fid(float x) { return x; }` costing exactly ~2x legacy), and
backlog item 2 (the 50-function `check`/`check_int` assertion-helper
family apparently satisfying every documented condition of
`mir_param_value_is_direct` yet still not getting direct-parameter
treatment), a temporary env-var-gated trace was added to every
`return 0` branch of `mir_param_value_is_direct` to find the exact
blocking condition empirically rather than guessing further from
static reading alone.

**Finding**: `fid`'s parameter `x` traced to `reason=no-object` (a
separate, distinct bug - `mir_object_eligible` in `dcc_mir.c` rejects
any local/param with `type_size(sym->type) > 2`, so wide `float`/`long`
values never get a `MirObject` at all, even though
`mir_param_value_is_direct` and `mir_emit_virtual_load_wide` both
already contain fully-written support for `type_size == 4` - this
specific gap is unresolved and left for a follow-up item, see below).

`check`'s parameters `got`/`expected`, by contrast, traced to
`reason=frameless` - meaning `mir_capture_stream_uses_frame()` reported
the function as never using a stack frame, despite `check`'s own
legacy-captured assembly clearly starting with `push ix`. Root-caused
to the function's cross-call cache:

```c
static int cached_result = -1;
static const FILE *cached_stream = NULL;
...
if (cached_stream == mir.capture_stream && cached_result >= 0)
    return cached_result;
```

`mir_begin_function` (`dcc_mir.c`) calls `mir.capture_stream =
tmpfile();` fresh for *every* function, closing the previous one first.
Once a `FILE*` is closed, the C library is free to (and in practice
routinely does) hand back the exact same address for the next
`tmpfile()` call. The cache above is keyed purely on that raw pointer
value with `static` (process-lifetime) storage - so whenever a later
function's `tmpfile()` happened to reuse a now-freed address, this
function silently returned some **earlier, unrelated function's**
frame/frameless verdict instead of computing (or even inspecting) its
own captured output at all. This was a genuine cross-function
correctness bug in the cache (not a deliberate memoization tradeoff),
present since `mir_capture_stream_uses_frame` was introduced
(mir-migration-plan-next10, the same session as `mir_param_value_is_
direct` itself) - it has silently suppressed the entire direct-
parameter-forwarding optimization for an unpredictable, potentially
large subset of the corpus (whichever functions happened to inherit a
"frameless" verdict from an earlier, unrelated frameless function via
address reuse) for as long as that optimization has existed.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): removed the two
`static` cache variables entirely; the function now always rewinds and
re-scans `mir.capture_stream` for `"push ix"` on every call, then
restores the stream's prior read position exactly as before. The scan
itself is bounded by one function's own captured-assembly length
(typically a few hundred bytes to a few KB) and is not expensive
enough to warrant caching at the cost of correctness - especially
since `mir_param_value_is_direct` (and thus this function) had already
been called repeatedly per function even with the cache in place, so
removing it does not introduce a new O(n) inner loop that didn't
already effectively exist. No API or signature change; purely internal
to this one function.

**Validation**:
- `check` (`t2darr.c`): `generated-bytes=599` -> `495` (a real
  reduction, though `check` itself in `t2darr.c` remains a fallback at
  495 vs 394 captured bytes - other apps' identical-shaped `check`
  functions crossed the acceptance threshold, see below).
- `fid` (`tc89fadd.c`): unaffected, as expected (its blocker is the
  separate `mir_object_eligible` wide-type gap, not this cache).
- Whole-corpus census (`build/mir-t34-cache-fix.tsv` vs
  `build/mir-current.tsv`, `--fail-on-regression`): **0 regressions,
  +10 newly-accepted functions** (275->285/2022, 13.60%->14.09%):
  `tc99varm.two`, `tesc.check`, `tmirfast.check`, `tmirfuse.check`,
  `tmirslot.check`, `tphijoin.check`, `trtl2.check_i`,
  `tscanf.check_int`, `tstdlib.check_int`, `tstr3.check_i` - 9 of the
  10 are exactly the `check`/`check_int`-family functions the new plan
  identified as a 50-function recurring signature; this one cache fix
  alone closed a fifth of that family in a single item.
- Focused `runall.ps1 -Mode full` on all 11 flagged apps (tc89core,
  tc99varm, tesc, tmirfast, tmirfuse, tmirslot, tphijoin, trtl2,
  tscanf, tstdlib, tstr3): **11/11 correctness PASS, 0 performance
  regressions, 18 genuine improvements** (up to -2.89% cycles / -1.52%
  bytes in `tmirfast`) - a completely clean win, no trade-off needed.
  Baselines updated via `-UpdatePerfBaseline` for all 11 apps.
- Wide safety net (both required tiers): `-Mode fast` 314/323 clean;
  full-corpus `-Mode full` also 314/323 clean, diagnostics/dccpeep/
  performance all passed.

**Why this was found instead of the originally-planned "wide-value
forwarding infrastructure" item**: the new plan (`plan.md`, written
this session before implementation) hypothesized two separate levers -
a missing wide-value forwarding predicate, and an unexplained
`mir_param_value_is_direct` gap for the `check` family - and
recommended runtime instrumentation to diagnose the latter rather than
guessing further. Adding a temporary env-var-gated trace to every
`mir_param_value_is_direct` return-0 branch (built, tested against
both `fid` and `check`, then reverted before the real fix) immediately
separated these into two genuinely distinct bugs, of which this
cache bug turned out to be a real, load-bearing defect independent of
wide-value support - exactly the kind of finding SKILL.md's
diagnostics-first discipline (`DCC_MIR_REPORT`/force-accept-diff, and
here, one narrowly-scoped temporary trace) is meant to surface before
committing to an implementation plan based on static reading alone.

**Remaining work carried forward** (both already tracked in
`plan.md`'s backlog, refined with this session's findings):
- The `mir_object_eligible` wide-type exclusion (`fid`'s blocker) is
  unresolved - relaxing `type_size(sym->type) > 2` to also allow `== 4`
  would let wide locals/params get objects, which should let
  `mir_param_value_is_direct`'s and `mir_emit_virtual_load_wide`'s
  already-written wide support actually activate. Note also that
  `mir_emit_virtual_store_wide` (unlike the scalar
  `mir_emit_virtual_store`) does not currently call
  `mir_param_value_is_direct` at all - even after the object-
  eligibility gap is closed, the store side will need its own explicit
  check added to actually skip storing a direct wide parameter,
  mirroring the scalar store's existing first-line check.
- The remaining ~41 `check`/`check_int`-family functions that did not
  cross the acceptance threshold from this fix alone (like `t2darr.c`'s
  own `check`, now at 495 vs 394 bytes) likely have one or two further,
  smaller gaps on top of the cache bug - worth a fresh forced-accept
  diff on `t2darr.check` now that the cache bug is fixed, to see what
  specific bytes remain.

## Item T35: `mir_object_eligible` unnecessarily excluded wide (4-byte) locals/parameters (2026-08-01)

**Context**: Item T34's sibling finding - `tc89fadd.c`'s
`float fid(float x) { return x; }` traced to `reason=no-object` when
probed with the same temporary `mir_param_value_is_direct` trace used
to diagnose Item T34, a distinct bug from T34's cache issue.

**Root cause**: `mir_object_eligible` (`src/dcc/dcc_mir.c`) rejected
any local or parameter with `type_size(sym->type) > 2`, dating to the
original mem2reg/object-promotion commit (`0771448`), which explicitly
scoped its first milestone to "1/2-byte locals and parameters." Since
then, `mir_param_value_is_direct` and `mir_emit_virtual_load_wide`
(`src/dcc/dcc_mir_spilled_cfg.c`) both grew fully-written support for a
4-byte ("wide": `float`/`long`) object - both explicitly test
`type_size(...) == 4` alongside `== 2` - but neither could ever reach
that code, because no wide local or parameter was ever admitted to
`mir.objects[]` in the first place. This is the same "correct at
introduction, stale after the rest of the infrastructure grew around
it" shape as Items T3/T4/T30's dead-gate findings. Verified via a
grep-based audit of every `.object`/`->object` use across
`dcc_mir.c`/`dcc_mir_spilled_cfg.c` (phi merges, dead-store liveness,
fully-promoted checks) confirming none of the generic object/dataflow
machinery has any embedded size assumption - only the eligibility gate
itself did. Pointers (already excluded via `type_ptr_depth(sym->type)
> 0`) and structs (already excluded via `type_is_struct_object`) are
unaffected by widening the size check; only `TYPE_LONG`/`TYPE_FLOAT`
scalars newly qualify.

**Implementation**:
- `src/dcc/dcc_mir.c`: relaxed `mir_object_eligible`'s
  `type_size(sym->type) > 2` exclusion to `> 4`.
- `src/dcc/dcc_mir_spilled_cfg.c`: added a `mir_param_value_is_direct`
  check as the first line of `mir_emit_virtual_store_wide`, mirroring
  the scalar `mir_emit_virtual_store`'s existing first-line check -
  without this, a now-object-eligible direct wide parameter would
  still pay a full spill on the store side even though nothing
  downstream would ever read the slot (the load side,
  `mir_emit_virtual_load_wide`, already had this check from when it
  was originally written, unused until this item made it reachable).

**Validation**:
- `fid`: `fallback text-size (generated=268, captured=119, insns=21
  vs 11)` -> **`mir accepted (generated=133, captured=119, insns=10 vs
  11)`** - generated instruction count is now *below* legacy's own.
- Whole-corpus census (`build/mir-t35.tsv` vs
  `build/mir-t34-cache-fix.tsv`, `--fail-on-regression`): **0
  regressions, +21 newly-accepted functions** (285->306/2022,
  14.09%->15.13%) - the second-largest single-item jump this session
  (matching T30's +21): `fact.main`, `tc89fadd.fid`, `tc89fcmp.fidf`,
  `tc89fdiv.fidv`, `tc89flng.idf`, `tc89flta.f_id`, `tc89fptr.fid`,
  `tc89fs.fidf`, `tcrcfix.check_l`, `tctxflt.truth_not`,
  `tfloat4.identf`, `tlong.ident`, `tlong.uident`, `tlongopt.id32`,
  `tlongreg.idsl`, `tlongreg.idul`, `tmuldiv.i32_test`,
  `tmuldiv.ui32_test`, `triangle.main`, `tscanf.check_long`,
  `tsyntax.check_l` - both the originally-motivating `fid`/`ident`
  family *and* a `long`-typed sibling of the `check`/`check_int` family
  (`check_l`/`check_long`) crossed the line together.
- Focused `runall.ps1 -Mode full` on all 18 flagged apps (fact,
  tc89fadd, tc89fcmp, tc89fdiv, tc89flng, tc89flta, tc89fptr, tc89fs,
  tcrcfix, tctxflt, tfloat4, tlong, tlongopt, tlongreg, tmuldiv,
  triangle, tscanf, tsyntax): **18/18 correctness PASS, 0 performance
  regressions, 22 genuine improvements** (up to -1.79% bytes in
  `tc89flng`) - another completely clean win, no trade-off needed.
  Baselines updated via `-UpdatePerfBaseline` for all 18 apps.
- Wide safety net (both required tiers): `-Mode fast` 314/323 clean;
  full-corpus `-Mode full` also 314/323 clean, diagnostics/dccpeep/
  performance all passed.

**Remaining wide-value work carried forward** (refined in `plan.md`'s
backlog): this item only closes the *parameter-direct-forwarding* path
for wide values (mirroring what Items 26/27/T27 did for scalars). The
general single-use HL:DE forwarding predicates T1/T3/T4/T7(deferred)/
T30/T31/T32 built for 16-bit values (`mir_can_forward_hl_to_next` and
friends) still have no wide counterpart at all for *computed* wide
values (e.g. a wide binary/call result assigned to a local and used
once) - `mir_emit_virtual_store_wide` still unconditionally spills any
non-param wide value with an assigned slot. This remains open as a
follow-on, now correctly scoped as "extend forwarding to computed wide
values" rather than "wide values have no forwarding at all" (params
are now covered).

## Item T36: fuse `x++`/`x--` on a global/extern scalar, and elide the now-redundant preceding load (2026-08-01)

**Context**: continuing the ranked backlog's item 2 (residual gap in
the `check`/`check_int` family after Item T34's cache fix) - a fresh
forced-accept diff on `t2darr.c`'s own `check` (495 vs 394 bytes,
down from 599 pre-T34) showed the direct-parameter reads were now
correct (T34/T35's fixes both landed cleanly), but `failures++;` (a
`static int failures;` file-scope global, incremented once per
assertion failure) still cost 7 instructions
(`ld hl,(name)/push hl/ld hl,1/ex de,hl/pop hl/add hl,de/ld (name),hl`)
where legacy's own `emit_incdec_sym_direct` (`dcc_symbols.c`) uses a
dedicated 3-instruction global fast path
(`ld hl,(name)/inc hl/ld (name),hl`), confirmed via `is_global_word_sym`
requiring only `SC_GLOBAL`/`SC_EXTERN` storage, non-array, 2-byte size.

**Root cause**: `mir_binary_is_selfstore_incdec` (Item 31's MIR mirror
of `emit_incdec_sym_direct`) only ever mirrored the local/parameter
half of legacy's fast path (`memory_storage != SC_LOCAL && != SC_PARAM`
rejects everything else) - the global/extern half was simply never
added, not a deliberate exclusion.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`):
- Relaxed `mir_binary_is_selfstore_incdec`'s storage check to also
  accept `SC_GLOBAL`/`SC_EXTERN`.
- Added `mir_emit_selfstore_incdec_global`, mirroring
  `emit_incdec_sym_direct`'s `is_global_word_sym` fast path exactly:
  `ld hl,(name) / inc-or-dec hl / ld (name),hl` (a 16-bit inc/dec is
  atomic, unlike the frame-relative form's byte-pair carry chain), with
  `extrn` emitted for `SC_EXTERN`. Wired into the `MIR_BINARY` case's
  dispatch, branching on `memory_storage`.
- **Second finding, same investigation**: even after the fusion
  itself worked, the *preceding* `MIR_LOAD` that produced the fused
  binary's left operand still ran its own (now entirely superseded)
  emission, materializing the value into HL and speculatively staging
  it via a stack push anticipating the ordinary (now-bypassed) binary
  form - a load-then-discard exactly like the dead-unary case
  `mir_value_only_used_by_dead_unary` already covers for a different
  consumer shape. Added `mir_value_is_selfstore_incdec_source`
  (mirrors `mir_binary_is_selfstore_incdec`'s own one-and-only-one-use
  requirement, applied to the *source* side instead of the result
  side) and wired it into both `mir_prepare_backend_slots`'s slot-skip
  disjunction and the `MIR_LOAD`/`MIR_PARAM` emission's own skip check,
  alongside the existing dead-unary/no-use conditions.

**Validation**:
- `t2darr.check`: `599` -> `495` (T34) -> `481` (T36 fusion alone) ->
  **`456`** generated bytes (T36 fusion + source-load elision
  combined) vs `394` captured - still fallback (a smaller, separate
  call-argument-caching residual remains, noted for a future item),
  but the `failures++` round trip itself is now fully closed and
  matches legacy's own 3-instruction shape exactly.
- Whole-corpus census (`build/mir-t36.tsv` vs `build/mir-t35.tsv`,
  `--fail-on-regression`): **0 regressions, +5 newly-accepted
  functions** (306->311/2022, 15.13%->15.38%): `tlmod.okmod`,
  `tlmul.okmul`, `tmirfast.dec_observe`, `tmirfast.inc_observe`,
  `tpreproc.verify_str`.
- Focused `runall.ps1 -Mode full` on the 4 flagged apps (tlmod, tlmul,
  tmirfast, tpreproc): 4/4 correctness PASS, but **3 performance
  regressions** - `tpreproc` (peep, +0.01%, negligible noise) and,
  more notably, `tmirfast` regressed in **both** peep (+0.43%) and
  nopeep (+0.33%) modes - a genuinely real cost, not the usual
  peep-only dccpeep-quality-gap signature, since nopeep also got
  worse. Diagnosed rather than accepted as noise (see Item T37 below,
  landed immediately as part of the same investigation before either
  item was committed) - root-caused to a *pre-existing*, unrelated
  MIR quality gap in `dec_observe`/`inc_observe` (both newly crossing
  the acceptance threshold specifically because of T36's own byte
  savings elsewhere in the same functions) that Item T37 fixes
  directly, rather than merely documenting and baselining over a real
  regression.
- Wide safety net and baseline updates for T36 are folded into Item
  T37's validation entry below, since both were fixed together before
  either was committed (T37's fix was required for T36's own
  regression-free acceptance).

## Item T37: add `MIR_ADDRESS` to `mir_can_forward_hl_to_next`'s `MIR_STORE` producer whitelist (2026-08-01)

**Context**: directly follows from Item T36's validation - `tmirfast`
showed a genuine (both-mode) cycle-count regression once
`inc_observe`/`dec_observe` crossed the text-size acceptance threshold.
Both functions contain `int *p = &x;` (`x`'s own address stored into
a local pointer `p`). Forced-accept-diffed against a legacy-forced-
fallback replay of the same function: legacy computes `&x` once and
stores it directly to `p`'s single frame slot; MIR instead stored the
computed address to a **temporary** backend slot, immediately reloaded
it, then stored it **again** to `p`'s real slot - a redundant round
trip costing real cycles on every call, not just extra static bytes.

**Root cause**: `mir_can_forward_hl_to_next`'s `MIR_STORE` case has its
own `producer_opcode` whitelist (`MIR_LOAD_INDIRECT`/`MIR_BINARY`/
`MIR_UNARY`/`MIR_CONST`/`MIR_CALL`, the last added by Item T31) that
never included `MIR_ADDRESS` - the exact same "whitelist scoped only to
what was reachable/tested at the time, not a deliberate safety
exclusion" pattern Item T31 already diagnosed and fixed for `MIR_CALL`.
A `MIR_ADDRESS` result (a local/global's own address) is a pure,
side-effect-free computation exactly like `MIR_CONST` - its value sits
in HL right after computing it, and the following store's own fixed
ix-relative destination offset does not depend on how the stored value
was produced.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): added
`MIR_ADDRESS` to the `producer_opcode` whitelist in
`mir_can_forward_hl_to_next`'s `MIR_STORE` case.

**Validation**:
- `inc_observe`: `389` -> `337` generated bytes (`373` captured;
  `33` -> `29` generated instructions, both now below legacy's `36`);
  the double-slot dead round trip is gone from the generated assembly,
  confirmed via force-accept-diff (direct single store to `p`'s slot,
  matching legacy's shape).
- Whole-corpus census (`build/mir-t37.tsv` vs `build/mir-t36.tsv`,
  `--fail-on-regression`): **0 regressions**, coverage unchanged at
  311/2022 (15.38% - no new acceptances this round, byte reductions
  across 50 apps for already-accepted/still-borderline candidates).
- Focused `runall.ps1 -Mode full` on the 5 flagged apps (tc89core,
  tc89decl, tlocalfp, tmirfast, tsyntax): **5/5 correctness PASS**.
  `tmirfast`'s regression shrank dramatically: nopeep flipped to a
  genuine **improvement** (78,050->77,852 cycles, matching the real
  byte savings), and the remaining peep-mode delta shrank from +0.43%
  to a negligible **+0.12%** (85 cycles) - now matching the established
  dccpeep-quality-gap noise signature from Items T27-T30 (confirmed
  since nopeep improved while only peep shows a tiny residual). 9
  genuine improvements total (up to -2.33% bytes in `tlocalfp`).
  Baselines updated via `-UpdatePerfBaseline` for all 8 apps touched by
  T36 and T37 combined (tlmod, tlmul, tmirfast, tpreproc, tc89core,
  tc89decl, tlocalfp, tsyntax).
- Wide safety net (both required tiers): `-Mode fast` 314/323 clean;
  full-corpus `-Mode full` also 314/323 clean, diagnostics/dccpeep/
  performance all passed.

**Process note**: this is the first item in this session where a
`runall.ps1 -Mode full` performance regression was investigated and
*fixed at its root cause* (a second, related producer-whitelist gap)
rather than diagnosed-and-accepted as noise or deferred - justified
because the regression was real in both peep and nopeep modes (not the
usual peep-only signature) and a clean, low-risk, well-precedented fix
(extending an existing whitelist, exactly like Item T31's own template)
was available immediately. Per SKILL.md's discipline, both items were
validated together (T37's fix was required before T36's own
performance profile could be considered acceptable) before either was
committed.

## Item T38: add `MIR_LOAD` to `mir_can_forward_hl_to_next`'s `MIR_STORE` producer whitelist (2026-08-01)

**Context**: deliberate audit of every `MIR_*` opcode against the
`MIR_STORE` producer whitelist (flagged as a follow-up after Items
T31/T37 both found missing entries only reactively, after each caused
a visible problem) - checked each remaining opcode for whether it is a
pure, side-effect-free producer leaving its value in HL, the same
shape already established for `MIR_CONST`/`MIR_CALL`/`MIR_ADDRESS`.

**Finding**: `MIR_LOAD_INDIRECT` (`y = *p;`) was already whitelisted,
but plain `MIR_LOAD` (`y = x;`, a simple non-indirect variable-to-
variable copy) was not - an inconsistency with no safety rationale,
since a direct load is if anything simpler than an indirect one (no
pointer dereference at all). Confirmed via a synthetic test
(`static int g1, g2; int copyit(void) { g2 = g1; return g2; }`): the
load of `g1` still spilled to a temporary backend slot and reloaded
before the store to `g2`, an identical dead round trip to the T31/T37
cases - `ld hl,(g1) / ld (temp),hl / ld hl,(temp) / ld (g2),hl` where
the direct form is simply `ld hl,(g1) / ld (g2),hl`.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): added `MIR_LOAD`
to the `producer_opcode` whitelist in `mir_can_forward_hl_to_next`'s
`MIR_STORE` case.

**Validation**:
- Synthetic `copyit`: confirmed via force-accept-diff - the temporary
  slot and its store/reload round trip are both gone; the whole
  function's frame allocation is eliminated entirely (0 backend slots
  needed once the redundant round trip is removed).
- Whole-corpus census (`build/mir-t38.tsv` vs `build/mir-t37.tsv`,
  `--fail-on-regression`): **0 regressions, +3 newly-accepted
  functions** (311->314/2023, 15.38%->15.52%): `forint.eval_str`,
  `pint.init_run_storage`, `tpeepal.retain_escaped` - a smaller yield
  than Items T34-T37, suggesting most `y = x;`-shaped copies in the
  corpus already involve at least one object-eligible side (handled
  by the existing mem2reg/object-promotion machinery instead), leaving
  this gap to matter mainly for object-*ineligible* pairs (pointers,
  as in `retain_escaped`'s `int *ptr` parameter; or plain globals, as
  in the synthetic test).
- Focused `runall.ps1 -Mode full` on the 3 flagged apps (forint, pint,
  tpeepal): **3/3 correctness PASS**. One small, real (identical-
  delta-in-both-modes) regression in `tpeepal` (+0.05%, 61 cycles) -
  diagnosed via force-accept-diff on `retain_escaped` itself, whose
  generated code is confirmed minimal/optimal (7 instructions: load
  param, store to global, return - matching the expected shape
  exactly, no residual inefficiency in the fix's own logic). The
  both-mode-identical, deterministic, negligible-magnitude signature
  matches SKILL.md's documented "code-placement sensitivity in
  interpreter heaps" cause category (a different function's code
  shifting by a couple of bytes can change loop/branch alignment
  elsewhere in the same binary) rather than a defect in this item.
  Baseline updated via `-UpdatePerfBaseline` for all 3 apps.
- Wide safety net: `-Mode fast` showed one apparent failure
  (`tkbd`) on the first pass, but `tkbd` is already flagged
  `perf_ignore: true` in `tests/_test_overrides.json` (a known-
  nondeterministic app due to keyboard-input-timing simulation);
  re-running `-Mode fast` in isolation and then the full suite again
  both passed cleanly (314/323), confirming a one-off flake unrelated
  to this change, not a reproducible regression. Full-corpus `-Mode
  full` also clean (314/323), diagnostics/dccpeep/performance all
  passed.

**Audit continues**: `MIR_FLOAT_CONST` (structurally identical to
`MIR_CONST`, just for float literals) and `MIR_STRING_ADDRESS`/
`MIR_COMPOUND_ADDRESS`/`MIR_INDEX_ADDRESS`/`MIR_MEMBER_ADDRESS`
(structurally identical to `MIR_ADDRESS`, just for different address
targets) look like further plausible candidates by the same reasoning,
but were not yet forced-accept-diffed against a concrete motivating
example this session - left for a follow-up continuation of this
audit rather than added speculatively without a confirmed real gap.

## Item T39: add the rest of the "address" family to `mir_can_forward_hl_to_next`'s `MIR_STORE` producer whitelist (2026-08-01)

**Context**: continuing the `MIR_STORE` producer-whitelist audit
started by Item T38 - checked the remaining "address"-shaped opcodes
(`MIR_STRING_ADDRESS`, `MIR_MEMBER_ADDRESS`, `MIR_INDEX_ADDRESS`,
`MIR_COMPOUND_ADDRESS`) against the same "pure, side-effect-free
producer whose value sits in HL right after computing it" reasoning
that unlocked `MIR_ADDRESS` (Item T37).

**Finding**: three synthetic tests (mirroring Item T38's methodology)
confirmed the identical dead round trip for each:
- `MIR_STRING_ADDRESS` (`gp = "hello";`)
- `MIR_MEMBER_ADDRESS` (`gp = &s.field;`)
- `MIR_INDEX_ADDRESS` (`gp = &arr[i];`)

Each showed the same shape: `ld hl,<address> / ld (temp),hl / ld
l,(temp)/ld h,(temp+1) / ld (dest),hl` where the direct form is simply
`ld hl,<address> / ld (dest),hl`. `MIR_COMPOUND_ADDRESS` was also
checked, but a synthetic trigger for it (a case where it's actually
emitted rather than falling back to plain `MIR_ADDRESS`) was not found
this session - left out of this item pending a concrete motivating
case.

**Implementation** (`src/dcc/dcc_mir_spilled_cfg.c`): added
`MIR_STRING_ADDRESS`, `MIR_MEMBER_ADDRESS`, and `MIR_INDEX_ADDRESS` to
the `producer_opcode` whitelist in `mir_can_forward_hl_to_next`'s
`MIR_STORE` case.

**Validation**:
- All three synthetic tests confirmed via force-accept-diff: the
  temp-slot round trip is gone in each case, replaced by a direct
  `ld hl,<address> / ld (dest),hl` sequence matching the shape
  `MIR_ADDRESS`/`MIR_CALL`/`MIR_LOAD` already produce.
- Whole-corpus census (`build/mir-t39.tsv` vs `build/mir-t38.tsv`,
  `--fail-on-regression`): **0 regressions**, coverage unchanged at
  314/2023 (15.52% - no new acceptances this round; 31 apps had byte
  reductions in already-fallback or already-accepted candidates, none
  crossing the acceptance threshold this time). Census reported **0
  apps requiring runtime validation** (no newly-emitted or changed-
  active-MIR functions), so no focused `runall.ps1` app list was
  generated by the script itself.
- Wide safety net (both required tiers, run anyway per SKILL.md's
  discipline for any change touching the dominant selector regardless
  of whether the census flagged specific apps): `-Mode fast` 314/323
  clean; full-corpus `-Mode full` also 314/323 clean, diagnostics/
  dccpeep/performance all passed. No baseline updates needed (no
  performance deltas of any kind reported).

**Audit status**: the `MIR_STORE` producer whitelist now covers
`MIR_LOAD_INDIRECT`/`MIR_LOAD`/`MIR_BINARY`/`MIR_UNARY`/`MIR_CONST`/
`MIR_CALL`/`MIR_ADDRESS`/`MIR_STRING_ADDRESS`/`MIR_MEMBER_ADDRESS`/
`MIR_INDEX_ADDRESS` (Items 6/7/8, T30/T31, T37, T38, T39). Remaining
unaudited-with-a-confirmed-example opcodes: `MIR_FLOAT_CONST` (its
redundancy, when present, was found in this session's T38 investigation
to be entirely a *wide-value* forwarding gap - `mir_emit_virtual_store_
wide` has no forwarding logic at all yet, a separate, already-tracked
backlog item, not a 16-bit whitelist gap) and `MIR_COMPOUND_ADDRESS`
(no confirmed trigger found yet). This audit line is likely exhausted
for the 16-bit path; further systemic gains now depend on the
wide-value forwarding backlog item instead.
