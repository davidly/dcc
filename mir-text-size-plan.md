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
