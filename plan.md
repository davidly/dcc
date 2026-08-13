# dcc MIR migration: coverage-first sprint to 100%

`mir-text-size-plan.md` is the authoritative experiment log. This file is the
current execution plan and handoff.

## 2026-08-13 current checkpoint (read this first)

- Branch: `pr/143`; clean base HEAD for this batch: `a3f219f`.
- Current working-tree candidate coverage: **2153/2185 (98.54%)**.
- Remaining fallback population: **32 `final-cost-policy`**, all selected by
  the spilled scalar backend and with no other fallback reason.
- The numeric module now admits the 13-block `trowinv.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **127 MIR instructions**. It proves the private four-word result array,
  private 2-by-4 word table, byte-narrowed induction PHI, all four initial
  stores, both nested index strides, the loop-carried row-source reload,
  ordered variadic report, complete short-circuit check graph and final
  boolean return. No production function, global, local, output text or
  selected-output hash is matched.
- This is not the previously rejected forced generic candidate, which remains
  excluded after measuring **+7.48% peep / +5.14% nopeep**. The dedicated
  emitter uses a frameless register schedule with the bounded induction value
  in B, recomputes the mutable row selector on every iteration, retains
  target-width row scaling and word loads/stores, preserves the selected
  variadic call variant and reloads the result array after that call. It uses
  no IY.
- Normal selected/captured metrics are **819/2,193 bytes** and **76/213
  instructions**; stack-check metrics are **848/2,222 bytes** and **77/214
  instructions**. Checked full-mode selected/fallback totals are
  **22,735/23,055 cycles** and **5,376/5,376 bytes** peep, plus
  **22,745/24,009 cycles** and **5,376/5,504 bytes** nopeep. Forced fallback
  also passes full-mode validation.
- Renamed-function/global/local singular-row and switched-row boundary A/B
  fixtures preserve the same 13-block graph and are output-identical between
  selected and fallback builds in both modes. The singular case reports
  `1 0 -32768 32767` and return check 1; selected/fallback measurements are
  **49,556/49,875 cycles** and **5,376/5,376 bytes** peep, plus
  **49,556/50,666 cycles** and **5,376/5,632 bytes** nopeep. The switched
  boundary case reports `1 1 -32768 32767` and return check 1; selected/
  fallback measurements are **49,611/49,927 cycles** and
  **5,376/5,376 bytes** peep, plus **49,611/50,798 cycles** and
  **5,376/5,632 bytes** nopeep.
- The numeric module grows from **4,526 to 4,974 source lines**. Its object
  audit still reports only `mir_try_emit_numeric_kernels [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,152/2,185 (98.49%)** to **2,153/2,185 (98.54%)**, adds exactly
  `trowinv.main`, removes no accepted function, moves selector counts from
  **1,401 spilled / 377 scheduled** to **1,400 spilled / 378 scheduled**,
  and leaves **32 `final-cost-policy`** fallbacks. The corresponding normal
  census advances **2,063/2,104 to 2,064/2,104**, adds the same function,
  moves **1,340 spilled / 377 scheduled** to **1,339 spilled / 378
  scheduled**, and removes no accepted function. No performance baseline,
  production identifier, or output-hash gate was added.
- The call-runner module now admits the 28-block `cpmenumd.enumerate`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **318 MIR instructions**. It proves the 36-byte FCB, 13-byte filename,
  400-entry pointer list, signed BDOS-result range, both filename-copy loops,
  all four PHIs, 13 conditional branches, 11 jumps, both search calls, the
  comparator callback, three report sites, size/free traversal and all three
  returns. No production function, global, local, field or output name is
  matched.
- The emitter preserves FCB initialization, BDOS find-first/find-next order,
  the 128-byte DMA base and 32-byte result slots, FCB name/type offsets,
  wildcard termination, duplicate-before-capacity behavior, qsort/bsearch
  callback order, unexpected-result and found/not-found branches, per-result
  size/report/free order and the original boolean return. It uses a compact
  **53-byte IX frame** and no IY. Normal selected/captured metrics are
  **3,054/5,431 bytes** and **274/483 instructions**; stack-check metrics are
  **3,083/5,460 bytes** and **275/484 instructions**.
- `cpmenumd` passes checked full peep/nopeep validation, as does forced
  fallback. A temporary override staged two additional COM fixtures while
  leaving the 68K and XYZ searches empty. Selected and fallback output was
  identical in both modes: the COM search found the executable and both empty
  searches completed before the success line. Controlled selected/fallback
  measurements are **172,700/187,584 cycles** and **4,992/5,888 bytes**
  peep, plus **174,813/197,986 cycles** and **5,120/6,272 bytes** nopeep.
  The override and fixtures were removed after the A/B.
- The call-runner module grows from **9,442 to 10,309 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,151/2,185 (98.44%)** to **2,152/2,185 (98.49%)**, adds exactly
  `cpmenumd.enumerate`, removes no accepted function, moves selector counts
  from **1,402 spilled / 376 scheduled** to **1,401 spilled / 377
  scheduled**, and leaves **33 `final-cost-policy`** fallbacks. No
  performance baseline, production identifier, or output-hash gate was added.
- The call-runner module now admits the 28-block `tforblk.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **703 MIR instructions**. It proves all 30 distinct scoped objects and
  their widths, 53 local stores, seven local loads, nine PHIs, ten
  conditional branches, ten jumps, every shadowed local, all four source
  loops, the switch flow, 26 ordered checker calls, seven ordered helper
  calls, both variadic reports and the final failure-derived return. No
  production function, helper, global, local or output name is matched.
- The emitter preserves the C99 `for`-declaration scopes, inner/outer
  lifetimes, nested-loop ordering, shadowed values, helper side effects,
  checker/report order and final return. It uses a zero-byte IX frame and no
  IY. Normal selected/captured metrics are **5,986/10,578 bytes** and
  **660/1,010 instructions**; stack-check metrics are **6,015/10,607
  bytes** and **661/1,011 instructions**.
- Checked `tforblk` full-mode totals improve from **55,023 to 44,360 cycles**
  and **8,064 to 7,168 bytes** peep, plus **56,840 to 45,675 cycles** and
  **8,192 to 7,168 bytes** nopeep. Forced fallback also passes full-mode
  validation.
- A renamed helper/check/report/global/local scope A/B makes every successful
  checker and helper invocation observable in the final
  `edge 26/7/1` report without changing the matched main graph. Selected and
  fallback output is identical in both modes. Peep selected/fallback
  measurements are **62,722/73,385 cycles** and **7,552/8,448 bytes**;
  nopeep measurements are **64,340/75,505 cycles** and **7,680/8,576
  bytes**. A deliberate helper-result mismatch follows the same two checker
  failures, final `edge 26/7/3: tforblk FAILED: 2` report and nonzero return
  in selected and fallback builds. Peep selected/fallback measurements are
  **204,738/215,399 cycles** and **7,552/8,448 bytes**; nopeep measurements
  are **206,970/218,119 cycles** and **7,680/8,576 bytes**.
- The call-runner module grows from **8,774 to 9,442 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,150/2,185 (98.40%)** to **2,151/2,185 (98.44%)**, adds exactly
  `tforblk.main`, removes no accepted function, moves selector counts from
  **1,403 spilled / 375 scheduled** to **1,402 spilled / 376 scheduled**,
  and leaves **34 `final-cost-policy`** fallbacks. No performance baseline,
  production name, or output-hash gate was added.
- The call-runner module now admits the 27-block `tabort.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **269 MIR instructions**. It proves the 10-byte file/buffer working set,
  the static failure counter, all three open/close lifetimes, rename,
  readback and removal, all nine checker calls, all 14 short-circuited ctype
  calls, every ordered variadic report, both reachable returns and the code
  following the noreturn call. The matcher requires the terminating function
  to carry the noreturn contract but makes no inference about instructions
  after that call.
- The emitter preserves the direct abnormal-termination call rather than
  replacing it with a return through normal exit cleanup. It also retains the
  following report and return path verbatim as unreachable source behavior,
  uses a compact **10-byte IX frame**, and never uses IY. Normal
  selected/captured metrics are **3,572/4,039 bytes** and **338/394
  instructions**; stack-check metrics are **3,601/4,068 bytes** and
  **339/395 instructions**.
- Checked `tabort` full-mode totals improve from **56,549 to 56,541 cycles**
  with **9,216 bytes** unchanged peep, plus **57,038 to 56,766 cycles** and
  **9,344 to 9,216 bytes** nopeep. Forced fallback also passes full-mode
  validation.
- A renamed-helper/global/local path A/B registers an atexit cleanup from the
  checker without changing the matched main graph. The abort path ends at
  `tabort ok` with no cleanup marker; selected/fallback measurements are
  **57,434/57,442 cycles** and **6,144/6,144 bytes** peep, plus
  **58,123/58,395 cycles** and **6,272/6,400 bytes** nopeep. The forced
  failure path returns normally, ends with `tabort FAILED 1` followed by
  `cleanup marker`, and measures **74,942/74,950 cycles** and
  **6,144/6,144 bytes** peep, plus **75,634/75,906 cycles** and
  **6,272/6,400 bytes** nopeep. Selected and fallback program output is
  identical in both modes on both paths.
- The call-runner module grows from **7,952 to 8,774 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,149/2,185 (98.35%)** to **2,150/2,185 (98.40%)**, adds exactly
  `tabort.main`, removes no accepted function, moves selector counts from
  **1,404 spilled / 374 scheduled** to **1,403 spilled / 375 scheduled**,
  and leaves **35 `final-cost-policy`** fallbacks. No performance baseline,
  production identifier, or output-signature gate was added.
- The call-runner module now admits the 26-block `tforcomm.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **345 MIR instructions**. It proves the four-word local array, all 15 PHIs,
  ten conditional branches, five backedges, every scalar/pointer load and
  store, both indexed loads, the pointer differences and all seven ordered
  variadic reports. The matcher also proves the three distinct `for`
  init/condition/increment placements, both side-effecting comma conditions,
  the value and statement comma expressions, and the complete short-circuit
  return graph.
- The emitter preserves initialization, condition, body and increment order
  separately for each loop, including `ticks++` before `ptr++` before the
  comparison and `i++` before the final `ptr++`. It retains every report call
  and observable local update in a compact **13-byte IX frame**, with no IY.
  Normal selected/captured metrics are **3,625/6,845 bytes** and
  **384/594 instructions**; stack-check metrics are **3,654/6,874 bytes**
  and **385/595 instructions**.
- Checked `tforcomm` full-mode totals improve from **137,456 to 132,751
  cycles** and **6,272 to 5,888 bytes** peep, plus **139,196 to 132,721
  cycles** and **6,528 to 5,888 bytes** nopeep. Forced fallback also passes
  full-mode validation.
- A renamed-function, renamed-local and renamed-report side-effect A/B calls
  a helper, checker and `printf` once per report. Selected and fallback output
  are identical in both modes and end with `sidefx: 7 247 0 1`. Selected/
  fallback measurements are **102,086/106,791 cycles** and
  **2,816/3,072 bytes** peep, plus **103,103/109,578 cycles** and
  **2,944/3,456 bytes** nopeep.
- The call-runner module grows from **7,271 to 7,952 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,148/2,185 (98.31%)** to **2,149/2,185 (98.35%)**, adds exactly
  `tforcomm.main`, removes no accepted function, moves selector counts from
  **1,405 spilled / 373 scheduled** to **1,404 spilled / 374 scheduled**,
  and leaves **36 `final-cost-policy`** fallbacks. No performance baseline,
  production symbol-name, or output-hash gate was added.
- The numeric module now admits the 27-block `tautolcs.lcs`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **225 MIR instructions**. It proves both nonvolatile character-pointer
  parameters, the two target-width length scans, the fixed 9-by-9 signed-int
  table, both zero-boundary loops, all five induction PHIs and both
  recurrence PHIs, every row/column address and word load/store, the
  promoted character equality, the diagonal increment, ordered maximum
  choice, backedges and final indexed result. No production function/local
  name or graph/output hash is used.
- The dedicated emitter is not the previously rejected forced generic
  candidate, which remains excluded after measuring **+29.92% peep /
  +22.09% nopeep**. It replaces the universal-spill CFG with a registerized
  one-row dynamic-programming kernel: nine target-width word cells, bounded
  byte loop state, direct character comparisons and a **25-byte IX frame**,
  with no IY. Empty strings, one-character matches/mismatches, maximum
  eight-character rows, reversed strings, repeated characters, high-bit
  characters, identical pointers and overlapping input pointers preserve
  the legacy result.
- Normal selected/captured metrics are **1,126/7,806 bytes** and
  **104/783 instructions**; stack-check metrics are **1,155/7,835 bytes**
  and **105/784 instructions**. Checked `tautolcs` full-mode totals improve
  from **111,398 to 22,634 cycles** and **6,144 to 5,376 bytes** peep, plus
  **124,840 to 22,582 cycles** and **6,400 to 5,376 bytes** nopeep.
  Forced fallback also passes full-mode validation.
- A renamed-function/parameter/local boundary and alias A/B is output
  identical in both modes and ends with `failed=0`. Selected/fallback
  measurements are **182,892/842,809 cycles** and **2,944/3,584 bytes**
  peep, plus **183,525/944,432 cycles** and **2,944/3,840 bytes** nopeep.
- The numeric module grows from **4,013 to 4,526 source lines**. Its object
  audit reports only `mir_try_emit_numeric_kernels [T]` as defined global
  code and zero global data symbols. The canonical build passes. The
  regression-gated stack-check census advances **2,147/2,185 (98.26%)** to
  **2,148/2,185 (98.31%)**, adds exactly `tautolcs.lcs`, removes no accepted
  function, moves selector counts from **1,406 spilled / 372 scheduled** to
  **1,405 spilled / 373 scheduled**, and leaves **37
  `final-cost-policy`** fallbacks. The corresponding normal census adds the
  same function at **2,059 accepted**, moves **372 to 373 scheduled**, and
  removes no accepted function. No performance baseline, production name,
  or output-hash gate was added.
- The call-runner module now admits the 24-block `tstr.test_wide`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **711 MIR instructions**. It proves the 4,096-element wide global buffer,
  the 27-element local wide-string buffer, all 15 PHIs and 13 conditional
  branches, every wide index/load/store and signed-byte restoration
  conversion, all seven random calls, both length calls, the first/last
  character, copy, substring and memory-compare helpers, all **13** ordered
  variadic reports, all eight ordered failure exits and the original void
  fallthrough. The emitter preserves the four test phases, buffer mutations,
  string and integer argument conversions, success order and every failure
  path in a compact **70-byte IX frame**, with no IY.
- Normal selected/captured metrics are **7,017/9,284 bytes** and
  **625/824 instructions**; stack-check metrics are **7,046/9,313 bytes**
  and **626/825 instructions**. `tstr` passes checked full peep/nopeep
  validation, as does the forced-fallback side. Selected/fallback totals are
  **1,382,732,616/1,388,769,453 cycles** and **11,904/12,160 bytes** peep,
  plus **1,405,854,915/1,435,282,159 cycles** and **12,544/13,056 bytes**
  nopeep.
- A renamed-function, renamed-helper, renamed-global and renamed-local
  boundary A/B is byte-identical in both modes and ends with `edge complete`.
  Selected/fallback measurements are **704,278,104/710,315,141 cycles** and
  **5,376/5,760 bytes** peep, plus **717,875,193/747,301,533 cycles** and
  **5,632/6,144 bytes** nopeep. A deliberate first length-check mismatch
  follows the same failure report and nonzero exit in both modes; selected/
  fallback measurements are **1,148,636/6,762,258 cycles** peep and
  **1,148,515/28,775,932 cycles** nopeep, with the same corresponding image
  sizes.
- The call-runner module grows from **6,286 to 7,271 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,146/2,185 (98.22%)** to **2,147/2,185 (98.26%)**, adds exactly
  `tstr.test_wide`, removes no accepted function, moves selector counts from
  **1,407 spilled / 371 scheduled** to **1,406 spilled / 372 scheduled**,
  and leaves **38 `final-cost-policy`** fallbacks. The corresponding
  regression-gated normal census advances **2,057/2,103 (97.81%)** to
  **2,058/2,103 (97.86%)**, moves **1,345 spilled / 371 scheduled** to
  **1,344 spilled / 372 scheduled**, and leaves **45
  `final-cost-policy`** fallbacks. No performance baseline, production name,
  or output-hash gate was added.
- The call-runner module now admits the 24-block `tnestfor.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **288 MIR instructions**. It proves the local 81-byte sieve and 40-byte
  mixed-value aggregate, the aggregate initialization loop and nested
  three-by-four grid loop, both mask loops and their seven PHIs, all indexed
  long/float/pointer/word/byte accesses, every branch and backedge, all
  **13** ordered build/check/count/stride/report calls, the three variadic
  reports and the final zero return. The emitter preserves the source call
  and nested-loop order while using a compact **132-byte IX frame**, with no
  IY.
- Normal selected/captured metrics are **3,504/6,438 bytes** and
  **330/604 instructions**; stack-check metrics are **3,533/6,467 bytes**
  and **331/605 instructions**. `tnestfor` passes checked full peep/nopeep
  validation, as does the forced-fallback side. Selected/fallback totals are
  **173,515/183,966 cycles** and **6,784/7,040 bytes** peep, plus
  **212,306/224,986 cycles** and **7,040/7,424 bytes** nopeep.
- A renamed-function, renamed-helper, renamed-global, renamed-aggregate and
  renamed-local edge A/B is byte-identical in both modes and prints the
  expected prime, indexed-not and stride summaries. Selected/fallback
  measurements are **170,811/181,262 cycles** and **3,456/3,840 bytes**
  peep, plus **209,602/222,282 cycles** and **3,712/4,224 bytes** nopeep.
- The call-runner module grows from **5,530 to 6,286 source lines**. Its
  object audit still reports only `mir_try_emit_call_runners [T]` as defined
  global code, with zero global read-only or writable data. The canonical
  build passes. The regression-gated stack-check census advances
  **2,145/2,185 (98.17%)** to **2,146/2,185 (98.22%)**, adds exactly
  `tnestfor.main`, removes no accepted function, moves selector counts from
  **1,408 spilled / 370 scheduled** to **1,407 spilled / 371 scheduled**,
  and leaves **39 `final-cost-policy`** fallbacks. The corresponding
  regression-gated normal census advances **2,056/2,103 (97.77%)** to
  **2,057/2,103 (97.81%)**, moves **1,346 spilled / 370 scheduled** to
  **1,345 spilled / 371 scheduled**, and leaves **46
  `final-cost-policy`** fallbacks. No performance baseline, production name,
  or output-hash gate was added.
- The aggregate-check module now admits the 22-block
  `tptrlhs.touch_locals` `final-cost-policy` fallback through a strict
  name-free matcher over all **1,439 MIR instructions**. It proves the four
  loop PHIs and all seven conditional/backedge pairs, both 155-byte aggregate
  copies, the complete local pointer graph, all member/array/index address
  calculations, all **27** ordered writes, all **27** ordered three-argument
  checks, and the original void fallthrough. The emitter preserves the
  resolved alias relationships and write/check evaluation order in a compact
  **62-byte IX frame**, with no IY.
- Normal selected/captured metrics are **4,967/40,433 bytes** and
  **464/4,079 instructions**; stack-check metrics are
  **4,996/40,462 bytes** and **465/4,080 instructions**. `tptrlhs` passes
  checked full peep/nopeep validation, as does the forced-fallback side.
  Selected/fallback totals are **738,186/925,005 cycles** and
  **17,792/22,400 bytes** peep, plus **768,429/967,079 cycles** and
  **19,584/24,576 bytes** nopeep.
- A renamed-function, renamed-checker and renamed-local alias A/B is
  byte-identical in both modes and prints `tptrlhs start` then `PASS`.
  Selected/fallback measurements are **738,062/924,881 cycles** and
  **15,104/19,712 bytes** peep, plus **768,305/966,955 cycles** and
  **16,896/21,888 bytes** nopeep.
- The aggregate-check module grows from **1,568 to 2,308 source lines** and
  from **27 to 39 static top-level helpers**. Its audit still reports only
  `mir_try_emit_aggregate_checks [T]`, with zero exported read-only or
  writable data. The canonical build passes. The regression-gated
  stack-check census advances **2,144/2,185 (98.12%)** to
  **2,145/2,185 (98.17%)**, adds exactly `tptrlhs.touch_locals`, removes no
  accepted function, moves selector counts from **1,409 spilled / 369
  scheduled** to **1,408 spilled / 370 scheduled**, and leaves **40
  `final-cost-policy`** fallbacks. The corresponding regression-gated normal
  census advances **2,055/2,103 (97.72%)** to
  **2,056/2,103 (97.77%)**, moves **1,347 spilled / 369 scheduled** to
  **1,346 spilled / 370 scheduled**, and leaves **47
  `final-cost-policy`** fallbacks. No performance baseline, production name,
  or output-hash gate was added.
- The call-runner module now admits the 22-block `tgnarly.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **564 MIR instructions**. It proves both local-array initialization loops,
  the Duff-device call and copied endpoints, all scalar/byte/pointer/aggregate
  assignments and conversions, the indirect function-designator calls, the
  comma loop, nested ternary CFG, all eight PHIs, six conditional branches,
  six jumps, **39** ordered calls including all **32** variadic reports, and
  the final zero return. The emitter preserves the unusual helper, switch,
  loop, aggregate-copy, string-index, function-pointer and formatted-I/O
  behavior while using a compact **20-byte IX frame** with no IY. Normal
  selected/captured metrics are **7,367/10,387 bytes** and
  **693/976 instructions**; stack-check metrics are **7,396/10,416 bytes**
  and **694/977 instructions**.
- `tgnarly` passes checked full peep/nopeep validation, as does the forced
  fallback side. Against the checked values, selected peep improves from
  **505,350 to 499,737 cycles** and **8,448 to 8,064 bytes**, while selected
  nopeep improves from **509,049 to 501,987 cycles** and
  **9,088 to 8,576 bytes**. The forced-fallback full run is correctness-clean
  at **503,211 peep** and **506,100 nopeep cycles**.
- A renamed-symbol edge A/B calls the scheduled driver twice and counts every
  direct and indirect helper path. Selected and forced-fallback program output
  is byte-identical in both modes and ends with
  `edge 0 0 4 2 2 2 2 2`. Selected/fallback measurements are
  **1,038,060/1,045,008 cycles and 5,632/6,016 bytes peep**, and
  **1,044,508/1,052,734 cycles and 6,272/6,656 bytes nopeep**.
  Direct stack-check `tgnarly` A/B is also byte-identical:
  **497,747/501,221 cycles and 5,376/5,760 bytes peep**, and
  **499,997/504,110 cycles and 5,888/6,400 bytes nopeep**.
- The call-runner module grows from **4,612 to 5,530 source lines** and adds
  **22** static top-level helpers. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code, with zero global
  read-only or writable data. The canonical build passes. The
  regression-gated stack-check census advances **2,143/2,185 (98.08%)** to
  **2,144/2,185 (98.12%)**, adds exactly `tgnarly.main`, removes no accepted
  function, moves selector counts from **1,410 spilled / 368 scheduled** to
  **1,409 spilled / 369 scheduled**, and leaves
  **41 `final-cost-policy`** fallbacks. The corresponding normal census moves
  from **2,054/2,103** to **2,055/2,103**, moves
  **1,348 spilled / 368 scheduled** to
  **1,347 spilled / 369 scheduled**, and has no regression. No performance
  baseline, production name, or output-hash gate was added.
- The call-runner module now admits the 22-block `tbyteeq.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **380 MIR instructions**. It proves the five signed/unsigned byte
  initializers and conversions, both distinct global byte arrays and indexed
  stores/loads, all signed and unsigned integer promotions, every equality
  and inequality operand order, the short-circuit logical value, all ten
  failure branches, all **15** ordered three-argument check calls, the final
  variadic report choice, global failure test and return. The emitter retains
  all five multiply-helper calls and uses a compact **5-byte IX frame** with
  no IY. Normal selected/captured metrics are **4,895/5,956 bytes** and
  **489/559 instructions**; stack-check metrics are **4,924/5,985 bytes**
  and **490/560 instructions**.
- `tbyteeq` passes checked full peep/nopeep validation, as does the forced
  fallback side. Against the checked fallback values, peep improves from
  **23,013 to 22,393 cycles** and **6,912 to 6,784 bytes**, while nopeep
  improves from **24,260 to 23,152 cycles** and
  **7,168 to 6,784 bytes**.
- A renamed-symbol boundary A/B calls the scheduled driver with `argc` values
  zero, one and two, exercising zero, negative signed-byte, unsigned
  high-byte and wrapped signed-byte promotion boundaries. Selected and
  forced-fallback output is byte-identical in both modes and ends with
  `edge 1 4 0 0 1 2`. Selected/fallback measurements are
  **296,242/298,214 cycles and 3,328/3,456 bytes peep**, and
  **298,556/302,028 cycles and 3,328/3,712 bytes nopeep**.
- The call-runner module grows from **3,764 to 4,612 source lines** and from
  **68 to 83 static top-level helpers**. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code, with zero global
  read-only or writable data. The canonical build passes. The
  regression-gated stack-check census advances **2,142/2,185 (98.03%)** to
  **2,143/2,185 (98.08%)**, adds exactly `tbyteeq.main`, removes no accepted
  function, moves selector counts from **1,411 spilled / 367 scheduled** to
  **1,410 spilled / 368 scheduled**, and leaves
  **42 `final-cost-policy`** fallbacks. The corresponding normal census moves
  from **2,053/2,103** to **2,054/2,103** with no regression. No performance
  baseline, production name, or output-hash gate was added.
- The numeric module now admits the 22-block `ln2.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **250 MIR instructions**. It proves the two distinct 29-element local long
  arrays; all initialization, convergence, series and nested digit-loop
  PHIs/backedges; the exact `10000`, `3`, `2*k+1` and `9*(2*k+3)` long
  operations; the three defined helper ABIs and ordered calls; the variadic
  prefix report, both character-output sites, the 100-digit bound and the
  final zero return. The emitter retains the source long multiply, divide and
  remainder order and helpers, including signed division/remainder rounding.
  It uses a compact **236-byte IX frame**: the two arrays occupy 232 bytes and
  one four-byte in-range slot is reused by the initialization remainder,
  series index and digit divisor. BC carries array cursors, DE carries the
  printed-digit count, and no IY is used. Normal selected/captured metrics are
  **3,017/7,430 bytes** and **284/755 instructions**; stack-check metrics are
  **3,046/7,459 bytes** and **285/756 instructions**.
- `ln2` passes checked full peep/nopeep validation, as does the forced
  fallback side. Against the checked fallback values, peep improves from
  **46,892,054 to 46,770,814 cycles** and **7,680 to 7,296 bytes**, while
  nopeep improves from **47,355,231 to 47,201,620 cycles** and
  **7,936 to 7,296 bytes**.
- A renamed-helper convergence/edge A/B calls the scheduled driver twice. The
  first call terminates before the series body; the second proves the 29
  initialized `6666` blocks, exactly three ordered series iterations and
  multiplier/divisor pairs `(1,27)`, `(3,45)` and `(5,63)`, then exercises
  all ten decimal digits and the exact 100-digit termination. Selected and
  forced-fallback output is byte-identical in both modes. Selected/fallback
  measurements are **1,796,482/1,985,382 cycles and 4,224/4,608 bytes peep**,
  and **1,794,261/2,038,973 cycles and 4,608/5,248 bytes nopeep**.
- The first compact-frame attempt placed reusable state below IX's signed
  displacement range; the repeated-call edge case exposed caller-frame
  corruption. The retained layout moves that state to offsets `-120..-117`
  between the arrays, and both consecutive calls now return cleanly.
- The numeric module grows from **3,322 to 4,013 source lines** and from
  **57 to 67 static top-level helpers**. The object audit reports only
  `mir_try_emit_numeric_kernels [T]` as defined global code, with zero
  read-only or writable data. The canonical build passes. The
  regression-gated stack-check census advances **2,141/2,185 (97.99%)** to
  **2,142/2,185 (98.03%)**, adds exactly `ln2.main`, removes no accepted
  function, moves selector counts from **1,412 spilled / 366 scheduled** to
  **1,411 spilled / 367 scheduled**, and leaves
  **43 `final-cost-policy`** fallbacks. The corresponding normal census moves
  from **2,052/2,103** to **2,053/2,103** with no regression. No performance
  baseline, production name, or output-hash gate was added.
- The numeric module now admits the 19-block `tmodp2.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **675 MIR instructions**. It proves the distinct signed 38-element and
  unsigned 13-element word arrays; all six loop PHIs, bounds, loads and
  backedges; every signed/unsigned divisor and operation; all seven ordered
  variadic reports; every widening and 32-bit sum update; and the final zero
  return. The emitter uses a compact **7-byte IX frame**, keeps each bounded
  index byte-narrowed, and uses no IY. Unsigned power-of-two remainder and
  division use direct masks and logical shifts. Signed power-of-two operations
  retain the established signed helpers, allowing dccpeep's proven
  magnitude/sign-restoring forms to preserve truncation toward zero for every
  negative edge. Normal selected/captured metrics are **12,598/15,931 bytes**
  and **1,123/1,472 instructions**; stack-check metrics are
  **12,627/15,960 bytes** and **1,124/1,473 instructions**.
- `tmodp2` passes checked full peep/nopeep validation, as does the forced
  fallback side. Against the checked fallback values, peep improves from
  **8,702,177 to 8,674,151 cycles** and **8,192 to 8,064 bytes**, while
  nopeep improves from **10,016,307 to 9,618,933 cycles** and
  **8,832 to 8,192 bytes**.
- A renamed-array/renamed-function boundary A/B substitutes signed values at
  every key zero, sign, mask, byte, 16384 and 16-bit endpoint and unsigned
  values through 65535, while retaining all seven driver reports and adding a
  wrapper return check. Selected and forced-fallback program output is
  byte-identical in both modes, ending with `sum 51967` and `edge return 0`.
  Selected/fallback measurements are
  **8,663,144/8,691,170 cycles and 5,504/5,504 bytes peep**, and
  **9,704,158/10,135,988 cycles and 5,632/6,144 bytes nopeep**.
- The numeric module grows from **2,665 to 3,322 source lines** and from
  **41 to 57 static top-level helpers**. The object audit reports only
  `mir_try_emit_numeric_kernels [T]` as defined global code, with zero global
  data. The canonical build passes. The regression-gated stack-check census
  advances **2,140/2,185 (97.94%)** to **2,141/2,185 (97.99%)**, adds exactly
  `tmodp2.main`, removes no accepted function, moves selector counts from
  **1,413 spilled / 365 scheduled** to
  **1,412 spilled / 366 scheduled**, and leaves
  **44 `final-cost-policy`** fallbacks. No performance baseline, production
  name, or output-hash gate was added.
- The call-runner module now admits the 19-block `tm.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **386 MIR instructions**. It proves the signed `argc > 1` logging store,
  dead `argv[0]` assignment, 10-pass outer loop, all three exact 66-element
  allocation/release loops and PHIs, the shared static 66-pointer array,
  every size/delta calculation, all conditional reports, and all **30**
  ordered calls: seven variadic reports, three zeroed allocations, eight
  memory checks, six fills, one plain allocation and five releases. The
  emitter keeps `j` and `i` byte-narrowed, uses a compact **10-byte IX
  frame**, uses no IY, preserves every call and failure path, and retains the
  compiler-proven six-call `memset` fastcall ABI; no float operation or float
  ABI is involved. Normal selected/captured metrics are **4,533/5,899
  bytes** and **406/534 instructions**; stack-check metrics are
  **4,562/5,928 bytes** and **407/535 instructions**.
- `tm` passes checked full peep/nopeep validation. Against checked performance
  values, peep improves from **100,313,194 to 99,949,760 cycles** and
  **7,552 to 7,424 bytes**, while nopeep improves from **103,829,038 to
  103,369,587 cycles** and **7,680 to 7,424 bytes**. Direct stack-check
  selected versus forced fallback output is byte-identical (`success`) in
  both modes: peep is **99,949,698/100,313,132 cycles** and
  **4,224/4,352 bytes**, while nopeep is
  **103,369,525/103,828,976 cycles** and **4,224/4,480 bytes**.
- A renamed-helper edge A/B runs both logging branches and checks exact
  aggregate counts for all allocation, fill, check, release and report calls.
  Selected and forced-fallback output is byte-identical in both modes:
  `edge 0 2640 1320 5280 6600 3960 1352`. Selected/fallback measurements
  are **915,310,753/915,782,707 cycles and 5,760/5,888 bytes peep**, and
  **1,143,324,901/1,144,006,209 cycles and 6,272/6,400 bytes nopeep**.
- The call-runner module grows from **2,823 to 3,764 source lines** and from
  **41 to 68 static top-level helpers**. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code and zero global
  data. The canonical build passes. The regression-gated stack-check census
  advances **2,139/2,185 (97.89%)** to **2,140/2,185 (97.94%)**, adds exactly
  `tm.main`, removes no accepted function, moves selector counts from
  **1,414 spilled / 364 scheduled** to **1,413 spilled / 365 scheduled**,
  and leaves **45 `final-cost-policy`** fallbacks. No performance baseline,
  production name, or output-hash gate was added.
- The numeric module now admits the 19-block `catalan.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **437 MIR instructions**. It proves the three distinct 31-element local
  long arrays and their zero/one initialization; the shared zero, zero-test,
  term, and small-division helper ABIs; all twelve ordered term calls and
  their signs, numerators, powers, and `8L*n + delta` operands; both loop
  PHIs/backedges; the `%ld.` report; the nested 100-digit output loops; both
  `putchar` calls; and the final zero return. The emitter preserves 32-bit
  wrap, multiply, add, divide, and remainder order, uses the original array
  spacing, and reduces the frame from 392 to **374 bytes**. BC carries the
  inner array cursor, DE carries the printed-digit accumulator, the later
  four-byte divisor reuses dead first-series/index storage, and no IY is used.
  Normal selected/captured metrics are **8,132/12,696 bytes** and
  **851/1,346 instructions**; stack-check metrics are
  **8,161/12,725 bytes** and **852/1,347 instructions**.
- `catalan` passes checked full peep/nopeep validation. Against checked
  performance values, peep improves from **542,781,715 to 541,800,591
  cycles** and **8,960 to 8,576 bytes**, while nopeep improves from
  **546,454,360 to 545,132,808 cycles** and **9,216 to 8,704 bytes**.
  Direct selected versus forced fallback A/B with the test's 768-byte stack
  has byte-identical output in both modes and improves peep from
  **542,097,273 to 541,800,565 cycles** with **6,144 to 6,016 bytes**, and
  nopeep from **545,481,072 to 545,132,782 cycles** with
  **6,528 to 6,016 bytes**.
- A renamed-helper boundary A/B runs the first numeric series through
  `n=8192`, proving the carry from `8L*n` into the high word and the final
  `+7` result `65543`, then exercises the unchanged nested digit loops.
  Selected and forced-fallback output is byte-identical in peep and nopeep:
  `65543.` followed by 100 zero digits. Selected/fallback measurements are
  **95,920,653/111,259,889 cycles and 5,120/5,632 bytes peep**, and
  **107,810,403/125,038,581 cycles and 5,376/6,016 bytes nopeep**.
- The numeric module grows from **1,768 to 2,665 source lines** and from
  **23 to 41 static top-level helpers**. The object audit reports only
  `mir_try_emit_numeric_kernels [T]` as defined global code, with zero
  read-only or writable data. The canonical build passes. The
  regression-gated stack-check census advances **2,138/2,185 (97.85%)** to
  **2,139/2,185 (97.89%)**, adds exactly `catalan.main`, removes no accepted
  function, moves selector counts from **1,415 spilled / 363 scheduled** to
  **1,414 spilled / 364 scheduled**, and leaves
  **46 `final-cost-policy`** fallbacks. No performance baseline, production
  name, or output-hash gate was added.
- The call-runner module now admits the 19-block `tforinc.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **123 MIR instructions**. It proves seven distinct defined helper calls and
  their exact prototypes, arguments, source order and result locals; the
  shared input string; the eight-argument variadic report; all seven ordered
  short-circuit checks and their expected values; the complete PHI/branch
  graph; and the final logical-negation return. The emitter uses a 14-byte IX
  result frame, preserves every helper and report call, and uses no IY.
  Normal selected/captured metrics are **1,370/2,274 bytes** and
  **128/208 instructions**; stack-check metrics are **1,399/2,303 bytes**
  and **129/209 instructions**.
- `tforinc` passes checked full peep/nopeep validation. Selected versus forced
  fallback improves peep from **47,848 to 47,771 cycles** with both images
  **5,888 bytes**, and nopeep from **49,635 to 49,103 cycles** while reducing
  the image from **6,272 to 6,016 bytes**. A renamed-helper edge A/B uses a
  phase counter, ordered increment-expression side effects, pointer bounds,
  unsigned-byte wrap and array sentinels while retaining the same seven-call
  main graph. Selected and forced-fallback output is byte-identical in both
  modes: `edge 10,10,4,4,14,22,14`. Selected/fallback measurements are
  **63,391/63,468 cycles and 3,968/3,968 bytes peep**, and
  **68,705/69,237 cycles and 4,608/4,736 bytes nopeep**.
- The call-runner module grows from **2,341 to 2,823 source lines** and from
  **37 to 41 static top-level helpers**. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code and zero global
  data. The canonical build passes. The regression-gated stack-check census
  advances **2,137/2,185 (97.80%)** to **2,138/2,185 (97.85%)**, adds exactly
  `tforinc.main`, removes no accepted function, moves selector counts from
  **1,416 spilled / 362 scheduled** to
  **1,415 spilled / 363 scheduled**, and leaves
  **47 `final-cost-policy`** fallbacks. No performance baseline, production
  name, or output-hash gate was added.
- The call-runner module now admits the 18-block `tatexit.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **66 MIR instructions**. It proves three distinct defined `void (void)`
  callbacks in source registration order, the shared
  `int (void (*)(void))` registration ABI and exact argument sites, three
  distinct word result locals, the complete short-circuit failure CFG, both
  one-argument variadic print sites, and the final zero return. The emitter
  uses a six-byte IX frame, passes each callback address through the ordinary
  stack function-pointer ABI, preserves every registration call and failure
  branch, and uses no IY. Normal selected/captured metrics are
  **544/721 bytes** and **46/66 instructions**; stack-check metrics are
  **573/750 bytes** and **47/67 instructions**.
- `tatexit` passes checked full peep/nopeep validation. Selected versus forced
  fallback changes peep from **19,893 to 19,873 cycles** and nopeep from
  **19,983 to 19,873 cycles**, with both checked images unchanged at
  **5,504 bytes**. A renamed callback/registration edge pair uses global
  capacity and registration counters to exercise both three-callback success
  and third-registration failure. Selected and forced-fallback output is
  byte-identical in both modes: success prints `edge ok` followed by
  `third`, `second`, `first`; failure prints `edge failure` followed by
  `second`, `first`. Success selected/fallback measurements are
  **24,858/24,878 cycles peep** and **24,957/25,067 nopeep**; failure
  measurements are **22,776/22,791 peep** and **22,866/22,966 nopeep**.
  All eight edge images are **2,176 bytes**.
- The call-runner module grows from **2,043 to 2,341 source lines** and from
  **35 to 37 static top-level helpers**. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code and zero global
  data. The canonical build passes. The regression-gated stack-check census
  advances **2,136/2,185 (97.76%)** to **2,137/2,185 (97.80%)**, adds exactly
  `tatexit.main`, removes no accepted function, moves selector counts from
  **1,417 spilled / 361 scheduled** to
  **1,416 spilled / 362 scheduled**, and leaves
  **48 `final-cost-policy`** fallbacks. No performance baseline, production
  name, or output-hash gate was added.
- The call-runner module now admits the 18-block `tmalloch.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **200 MIR instructions**. It proves the five allocation calls, four release
  calls, seven reports and their exact argument sites; the three distinct
  pointer locals and non-overlapping reusable lifetimes; both large-block byte
  writes and reads; the 32-byte fill PHI/backedge; both endpoint checks; every
  null/non-null edge; and the original early-return cleanup ordering. The
  emitter uses one reusable two-byte IX pointer slot, BC only for the bounded
  byte fill, and no IY. Normal selected/captured metrics are
  **1,710/2,834 bytes** and **162/265 instructions**; stack-check metrics are
  **1,739/2,863 bytes** and **163/266 instructions**.
- `tmalloch` passes checked full peep/nopeep validation. Selected versus forced
  fallback improves peep from **29,425 to 26,404 cycles** and
  **6,272 to 6,144 bytes**, and nopeep from **32,151 to 26,419 cycles** and
  **6,400 to 6,144 bytes**. A renamed allocator/releaser six-case edge matrix
  covers normal execution, failure at each of the first four allocation calls,
  and an intentionally accepted impossible final growth. Selected and
  forced-fallback program output is byte-identical in
  both modes for all twelve runs; every selected run is faster, with selected
  images never larger.
- The call-runner module is **2,043 source lines** and the added family keeps
  all seven helpers module-local. The object audit reports only
  `mir_try_emit_call_runners [T]` as defined global code and zero global data.
  Canonical and CMake builds pass. The regression-gated stack-check census
  advances **2,135/2,185 (97.71%)** to **2,136/2,185 (97.76%)**, adds exactly
  `tmalloch.main`, removes no accepted function, moves selector counts from
  **1,418 spilled / 360 scheduled** to
  **1,417 spilled / 361 scheduled**, and leaves
  **49 `final-cost-policy`** fallbacks. No performance baseline, production
  name, or output-hash gate was added.
- The next scanner-family batch admits the 12-block `tbig.str_to_long`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **79 MIR instructions**. It proves the single nonvolatile signed-character
  pointer parameter, exact local long/negative state, the optional `'-'`
  path, both ordered signed-character digit comparisons, accumulator and
  pointer PHI/backedge relationships, every local/parameter store, and both
  signed-long return arms. The frameless emitter keeps the pointer in BC and
  the full accumulator in DE:HL, keeps only the sign flag on the stack,
  multiplies by ten with three carry-propagating 32-bit shifts plus the saved
  doubled value, and adds each digit modulo 2^32. Final two's-complement
  negation preserves the target's wrap behavior; no IX frame or IY is used.
  Normal selected/captured metrics are **620/2,012 bytes** and
  **71/202 instructions**; stack-check metrics are **649/2,041 bytes** and
  **72/203 instructions**.
- `tbig` passes full peep/nopeep validation. Its standard workload does not
  call the argument parser, so selected and forced-fallback cycles are exactly
  equal at **1,499,414,928 peep** and **1,469,070,385 nopeep**. Selected versus
  fallback checked image sizes are **12,288/12,672 bytes peep** and
  **12,672/13,056 bytes nopeep**, exact reductions of **384 bytes (-3.03%)**
  and **384 bytes (-2.94%)**.
- A separately named 27-case boundary clone covers empty and nondigit inputs,
  leading space/tab, plus/minus/double-minus forms, negative zero, exact and
  embedded terminators, leading zeroes, signed-long limits, values on both
  sides of 2^32 wrap, a many-digit wrap, and a leading high-bit signed
  character. Selected and forced-fallback output is byte-identical with
  `failures 0` in both modes. Selected/fallback measurements are
  **903,843/1,189,141 cycles and 3,968/4,352 bytes peep**
  (**-285,298, -23.99%; -384 bytes, -8.82%**) and
  **905,016/1,228,512 cycles and 4,096/4,608 bytes nopeep**
  (**-323,496, -26.33%; -512 bytes, -11.11%**).
- The scanner module audit reports **3,442 source lines**, **58 static
  top-level helpers**, only `mir_try_emit_scanner_kernels` as exported code,
  and zero read-only or writable data exports. Canonical and CMake builds
  pass. The regression-gated stack-check census advances
  **2,134/2,185 (97.67%)** to **2,135/2,185 (97.71%)**, changes only
  `tbig.str_to_long`, removes no accepted function, moves selector counts from
  **1,419 spilled / 359 scheduled** to **1,418 spilled / 360 scheduled**, and
  leaves **50 `final-cost-policy`** fallbacks. No performance baseline,
  production name, or output-hash gate was added.
- The call/check orchestration extraction creates compiled
  `dcc_mir_machine_call_runners.c` with zero shared variables and
  `mir_try_emit_call_runners` as its sole export. Two phase calls preserve
  the exact former positions of the fixed call/check runner and fixed
  index-call runner while their plans, matchers, emitters, and family helpers
  move out of the core machine emitter. Normal and stack-check extraction
  censuses are byte-identical to the `4a679ee` snapshots: zero coverage,
  selector, metric, selected-hash, or runtime-validation change. The core
  emitter falls from **47,195 to 46,538 lines**; the new module is **1,526
  lines** with **28 static top-level helpers**, and the function-only internal
  contract grows from **52 to 55 lines**. `nm` reports only
  `mir_try_emit_call_runners [T]` as defined global code and no global
  read-only or writable data. Canonical and CMake builds pass.
- The same module admits the 12-block `tbcloop.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **148 MIR instructions**. It proves every opcode, call-site argument,
  repeated global root, local narrowed induction PHI, 16-element word fill,
  call order, output PHIs, and return edge. The emitter preserves both
  string-copy calls, the count/length/check ordering, both out-of-line
  long-index functions, both string checks, the safe and unsafe sum calls,
  the unsafe-call counter store/load and side effects, both final prints,
  global checks/failures, and every failure label. It uses a four-byte IX
  scratch frame for the call-crossing long result and **no IY**.
  Normal selected/captured metrics are **2,465/2,317 bytes** and
  **240/231 instructions**; stack-check metrics are **2,494/2,346 bytes**
  and **241/232 instructions**. Checked full mode passes with selected versus
  forced-fallback measurements of **117,271/117,553 cycles peep**
  (**-282 / -0.24%**) and **120,679/122,437 cycles nopeep**
  (**-1,758 / -1.44%**), with unchanged **6,656/6,784-byte** checked images
  and no baseline update.
- A renamed-helper edge A/B changes both source strings and exercises
  five-element safe/unsafe sums, expected totals 20/10, and five unsafe call
  side effects. The strict scheduler still selects without a source-function
  or application-name gate. Selected and forced-fallback output is
  byte-identical in peep and nopeep (`checks=7 failures=0`, `RESULT: PASS`).
  Direct selected/fallback measurements are **106,214/106,496 cycles** and
  **3,968/3,968 bytes** peep, and **109,259/111,017 cycles** and
  **4,096/4,096 bytes** nopeep.
- The final regression-gated normal census advances
  **2,044/2,103 (97.19%)** to **2,045/2,103 (97.24%)** and moves
  **1,358 spilled / 358 scheduled** to
  **1,357 spilled / 359 scheduled**. Stack-check advances
  **2,133/2,185 (97.62%)** to **2,134/2,185 (97.67%)** and moves
  **1,420 spilled / 358 scheduled** to
  **1,419 spilled / 359 scheduled**. Both comparisons add exactly
  `tbcloop.main`, remove nothing, and change no other application or
  function. The plan line-count audit grows this file from **2,661 to
  2,709 lines**.
- The next numeric-family batch creates compiled
  `dcc_mir_machine_numeric.c` with zero shared variables and
  `mir_try_emit_numeric_kernels` as its sole export. Phase dispatch keeps the
  previous selector positions byte-for-byte while moving the unsigned-long
  square-root search, fixed-point multiply, and narrowed div/mod while-loop
  schedules out of the core emitter. Before/after extraction censuses are
  byte-identical in normal mode at SHA-256
  `93168b31174f680dafd4a427709d179f5dbc4f5e0a0da871d240b5c058f1dcd0`
  and stack-check mode at
  `ea904031465ae5d13981f54b5560e787ebc898b46b3c062f70df209fffaff0b8`;
  coverage remains **2043/2103** and **2132/2185**, with zero selector,
  metric, selected-hash, or runtime-validation change. The core machine
  emitter falls from **48,352 to 47,195 lines**; the numeric module is
  **1,768 lines** with **23 static top-level helpers**, and the narrow
  function-only internal contract is **52 lines**. The module export audit
  reports only `mir_try_emit_numeric_kernels [T]`, no read-only or writable
  data, and **PASS**; canonical and CMake builds pass.
- The same module admits the 11-block `primes.main`
  `final-cost-policy` fallback through a strict name-free matcher over all
  **120 MIR instructions**. It proves the `argc`/`argv[1]` ABI, `atol`
  conversion, unsigned start and odd normalization, the unsigned square-root
  helper relationship, outer ten-result loop, 32-bit odd divisor/modulo loop,
  `%lu` variadic print target in both automatic-long and forced-float/long
  modes, and final zero return. The emitter uses an 11-byte IX frame, keeps
  the active divisor low word in BC across `__lmu`, carries its high word
  explicitly, preserves full 32-bit wrap and the source's boundary behavior,
  and uses no IY. Normal selected/captured metrics are
  **1,616/3,574 bytes** and **145/352 instructions**; stack-check metrics are
  **1,645/3,603** and **146/353**. Checked-setting selected versus forced
  fallback improves peep from **4,619,721 to 3,929,138 cycles**
  (**-690,583 / -14.95%**) and **7,040 to 6,656 bytes**, and nopeep from
  **4,635,343 to 3,929,149 cycles** (**-706,194 / -15.23%**) with the same
  384-byte reduction. A renamed-helper edge A/B covers starts
  `0, 1, 2, 65534, 65535, 4294967294`; selected and fallback output is
  byte-identical in peep and nopeep. At 65,534, selected/fallback is
  **6,626,136/8,023,323 cycles peep** and
  **6,626,175/8,049,751 nopeep**, with **6,656/7,040 bytes** in both modes.
  Full `primes`, `tshlmac`, and `tdmfuse` peep/nopeep validation passes with
  zero checked regression and no baseline update. Normal coverage advances
  **2043/2103 (97.15%)** to **2044/2103 (97.19%)**; stack-check advances
  **2132/2185 (97.57%)** to **2133/2185 (97.62%)**, adding exactly
  `primes.main`, removing none, moving selector counts from
  **1,421 spilled / 357 scheduled** to **1,420 spilled / 358 scheduled**,
  and leaving **52 `final-cost-policy`** fallbacks. Final proof-snapshot
  SHA-256 values are
  `dc7f360093250818c911b29511dd5ffeec40f4102ed92edab75571d42481c0e0`
  normal and
  `60062d11a5a604ad5daa0925f9b88bdcd2adc1abbbb01cb4f93f538e23c9d195`
  stack-check.
- The first machine-emitter architecture pivot moves the recently added float
  report/check orchestration family, including the raw-conversion checker,
  from `dcc_mir_machine_emit.c` into
  `dcc_mir_machine_float_reports.c`. The new module owns the plan structs,
  strict matchers, emitters, raw-before-float production dispatch, and both
  family-local strict-smaller gates. A narrow
  `dcc_mir_machine_internal.h` contract exposes only the shared machine
  matching/emission functions it needs; the extraction adds **zero shared
  variables**, and all plan/candidate state remains module-local. The module
  has 35 `static` helpers and exports only its single dispatch entry.
  `dcc_mir_machine_emit.c` falls from
  **54,347 to 52,387 lines**; the new family module is **1,975 lines** and its
  internal contract is **27 lines**. The normal and stack-check full censuses
  are byte-identical before/after: **0** coverage, selector, metric, selected
  hash, or runtime-validation changes; stack coverage remains
  **2126/2185 (97.30%)**. All 11 affected apps pass full peep/nopeep validation
  with no checked performance regression and no baseline update.
- The second machine-emitter architecture pivot starts from `8ff8f65` and
  extracts the complete strict attention-kernel family
  (`transposed_matrix_vector_multiply`, `add_outer_product`, `softmax`,
  `matrix_vector_add`, and `backward_pass`) into
  `dcc_mir_machine_attention.c`. The new **2,637-line** module owns all three
  plans, strict matchers, emitters, family helpers, selector order, and its
  single dispatch entry; it has 44 `static` helpers and `nm` reports only
  `mir_try_emit_attention_kernels` as a defined global symbol, with no writable
  storage symbol. `dcc_mir_machine_emit.c` falls from **52,387 to 49,780
  lines**. The function-only internal contract grows from **27 to 36 lines**:
  four indispensable shared helper prototypes (parameter offset, pointee
  volatility, global-address resolution, and global-address DE emission) plus
  the one family dispatch prototype, and no global declaration.
  Normal **2037/2102 (96.91%)** and stack-check **2126/2185 (97.30%)**
  censuses are byte-identical before/after under `--fail-on-regression`, so all
  selector order, labels, metrics, selected hashes, and runtime-validation
  sets are unchanged. Canonical and CMake builds pass, and `attnc11` passes
  full peep/nopeep correctness and checked performance with no baseline update.
- The third machine-emitter architecture pivot starts from `b708c28` and
  extracts the landed comment and whitespace scanners, interpreter action
  decoder, buffered declaration parser, and bounded symbol search into
  `dcc_mir_machine_scanners.c`. The new **2,294-line** module owns all five
  plans, matchers, emitters, and 31 `static` family helpers; its two-phase
  `mir_try_emit_scanner_kernels` dispatch keeps the first four selectors before
  attention and the symbol search after float reports, exactly preserving the
  production order. `nm` reports only that dispatch as defined global code,
  with no read-only or writable data symbol. `dcc_mir_machine_emit.c` falls
  from **49,780 to 47,689 lines**. The function-only internal contract grows
  from **36 to 46 lines** with five indispensable shared helper prototypes
  (pointer/word type matching, local-buffer matching, HL offset emission, and
  global-word store emission) plus the dispatch prototype; there is no global
  declaration or shared mutable state. Normal **2037/2102 (96.91%)** and
  stack-check **2126/2185 (97.30%)** censuses are byte-identical before/after,
  including labels, metrics, selectors, selected hashes, and validation sets
  (SHA-256 `0e98c2f843d5fa45bbc29b0ed274e4e119956e3ea6b44e1a59854622b397a7c5`
  and `30f5edd85cc3a04332d8e8a95b20159ffd4760233f4988f8ba7ba43cae66c48a`).
  Canonical and CMake builds pass; `fint`, `cint`, `bint`, and `forint` pass
  full peep/nopeep validation with zero checked regression and no baseline
  update.
- The next coverage batch admits the 10-block deferred
  `tdmfuse.test_while_register_narrowed` fallback through a strict, name-free
  scheduled matcher in the core machine emitter. It proves the complete
  165-instruction graph, signed 16-bit count/value/index state, unsigned-byte
  narrowed loop and array state, nonvolatile/nonoverlapping local ranges,
  div/mod operand identity, all stores, the three-argument check call and
  seven-iteration outer bound. The emitter retains the real 32-byte frame and
  both loops, keeps the inner narrowed counter in BC across `__sdivmod` with
  explicit save/restore, uses DE for the report index only across safe spans,
  and never uses IY. The family remains in the core for now because it is a
  numeric div/mod trace loop, not a scanner/parser family.
  Normal metrics improve from **2,457/223 captured bytes/instructions to
  1,138/104 selected**; stack-check metrics improve from **2,486/224 to
  1,167/105**. Focused full mode passes with selected results of
  **172,199 cycles / 8,960 bytes peep** and
  **175,887 cycles / 8,832 bytes nopeep**. Forced fallback is
  **187,705 / 9,088** and **202,264 / 9,088**, so the exact A/B gain is
  **-15,506 cycles (-8.26%) / -128 bytes** peep and
  **-26,377 cycles (-13.04%) / -256 bytes** nopeep.
  A scratch boundary/wrap variant replaces the scale with 32,767 and produces
  the independently computed signed-wrap trace
  `12052,-1139,11879,16082,-16261,15579,2596`; selected and forced-fallback
  program output is byte-identical in peep and nopeep. Its selected/fallback
  measurements are **506,581/531,921 cycles** peep and
  **510,390/546,517 cycles** nopeep, with **5,632/5,888 bytes** in both
  modes. The regression-gated stack census against
  `/tmp/mir-census-current.tsv` advances **2,126/2,185 (97.30%)** to
  **2,127/2,185 (97.35%)**, leaves **58 `final-cost-policy`** fallbacks, and
  changes exactly this one function/app with zero removal. No baseline changes.
- The next scanner-family coverage batch admits the 13-block bounded
  four-character string predicate in `cint`. The strict matcher proves all
  **74 MIR instructions**, the single nonvolatile signed-character pointer
  parameter, five ordered byte loads at offsets 0 through 4, the four
  short-circuit boolean joins, exact equality types, four nonzero
  seven-bit character constants, the final zero terminator, and the final
  `int` 0/1 return. It captures the compared bytes from MIR and contains no
  source-function or application-name test. The frameless emitter loads the
  pointer from SP, performs only the source-ordered reads, stops at the first
  mismatch, leaves null-pointer behavior unchanged rather than adding a
  special case, and uses no IY. Normal selected/captured metrics improve from
  **1,285/121 bytes/instructions to 274/28**; stack-check metrics improve from
  **1,314/122 to 303/29**.
  `cint` passes full peep/nopeep validation. Selected versus forced fallback is
  **299,200,769/299,201,203 cycles** with **31,360/31,360 bytes** peep and
  **305,243,211/305,244,627 cycles** with **35,840/36,096 bytes** nopeep:
  **-434 cycles (-0.00015%) / 0 bytes** and
  **-1,416 cycles (-0.00046%) / -256 bytes (-0.71%)** respectively.
  A scratch selected/fallback edge harness covers the exact string, empty and
  one-to-three-character prefixes, a longer suffix, every case-position
  mismatch, minimally sized arrays that fail at offsets 0 through 3, an exact
  five-byte array, and a null pointer; all four runs print identical 0/1
  results and return success. Selected/fallback measurements are
  **64,885/67,299 cycles** with **2,432/2,432 bytes** peep and
  **65,012/72,464 cycles** with **2,432/2,688 bytes** nopeep.
  The scanner module audit now reports **2,502 source lines**, **35 static
  top-level helpers**, only `mir_try_emit_scanner_kernels` as exported code,
  and zero read-only or writable data exports. The regression-gated
  stack-check census advances **2,127/2,185 (97.35%)** to
  **2,128/2,185 (97.39%)**, changes only this function/application, removes no
  accepted function, moves selector counts from **1,426 spilled / 352
  scheduled** to **1,425 spilled / 353 scheduled**, and leaves
  **57 `final-cost-policy`** fallbacks. No baseline changes.
- The next scanner-family batch admits the 12-block two-character delimiter
  scan in `pint`. The strict name-free matcher proves all **86 MIR
  instructions**, four distinct nonvolatile globals (source pointer, signed
  long length/cursor, and signed line counter), both signed-long
  `cursor + 1 < length` bounds, the ordered `'*'`/`')'` reads, newline count,
  every cursor store, the final two-byte advance, and the complete
  short-circuit CFG. The emitter keeps the source cursor in BC, uses a fast
  registerized 16-bit bounded path only when both long high words are zero,
  retains an exact signed 32-bit path for every other value, never reads the
  second delimiter byte before the bound succeeds, and uses no IY. A
  mode-aware unreachable tail preserves the established downstream code
  addresses: push/pop pairs disappear under dccpeep while the retained NOP
  bytes make the peep and nopeep function endpoints independently identical
  to fallback.
  Normal selected/captured metrics improve from **3,082/314
  bytes/instructions to 2,945/309**; stack-check metrics improve from
  **3,111/315 to 2,974/310**. `pint` passes full peep/nopeep selected and
  forced-fallback runs with identical checked measurements:
  **253,436,043/284,125,051 cycles** and **30,464/33,024 bytes**.
  A scratch selected/fallback edge harness covers immediate and delayed
  closure, newline counting, two unterminated bodies, exact two- and
  three-byte EOF bounds, empty input, and negative length. All four runs print
  identical cursor/line results. Selected/fallback measurements are
  **100,153/116,197 cycles and 2,944/3,072 bytes** peep
  (**-16,044, -13.81%; -128 bytes, -4.17%**) and
  **101,106/120,088 cycles and 3,072/3,200 bytes** nopeep
  (**-18,982, -15.81%; -128 bytes, -4.00%**).
  The scanner module audit reports **3,059 source lines**, **48 static
  top-level helpers**, only `mir_try_emit_scanner_kernels` as exported code,
  and zero read-only or writable data exports. The regression-gated
  stack-check census advances **2,128/2,185 (97.39%)** to
  **2,129/2,185 (97.44%)**, changes only this function/application, removes no
  accepted function, moves selector counts from **1,425 spilled / 353
  scheduled** to **1,424 spilled / 354 scheduled**, and leaves
  **56 `final-cost-policy`** fallbacks. No baseline, name, or hash gate was
  added.
- The next float-family coverage batch admits the nine-block arithmetic
  normalization/decomposition loop in `tlog.frexpf` through a strict
  name-free matcher in `dcc_mir_machine_float_reports.c`. It proves the exact
  **72-instruction** graph, float and `int *` parameter ABI, initial exponent
  zero store, signed zero/negative comparisons, recursive sign normalization,
  both value-scaling loops, every exponent pointer update, self-call
  identity/prototype, parameter locations, and all returns. The emitter
  retains the established `__feqf`/`__fgtf`/`__flef`/`__fmf` fastcall
  convention, mutates only the by-value parameter copy, reloads the exponent
  pointer for every observable write, preserves carry/borrow wrapping, uses
  IX for stable parameter access, and uses no IY. Its family-local
  strict-smaller gate remains in force; the final generic cost policy is
  unchanged. Normal selected/captured metrics are **1,562/1,615 bytes** and
  **141/145 instructions**; stack-check metrics are **1,591/1,644** and
  **142/146**. `tlog` passes full peep/nopeep validation. Selected versus
  forced fallback is **1,046,252/1,046,294 cycles** peep and
  **1,047,767/1,048,350 cycles** nopeep, exact gains of **-42 (-0.0040%)**
  and **-583 (-0.0556%)**; linked size is unchanged at **8,448 bytes** in
  both modes.
  A separately named boundary/alias clone exercises positive and negative
  zero, both normalization boundaries, large and small powers, negative
  recursion, a NaN, and exponent pointers aliasing both words of the caller's
  source float. Selected and forced-fallback output is byte-identical in peep
  and nopeep; selected/fallback measurements are **707,338/707,428 cycles**
  peep and **707,982/708,903 cycles** nopeep, all at **5,888 bytes**.
  The float module audit reports **2,339 source lines**, **40 static top-level
  helpers**, only `mir_try_emit_float_reports` as exported code, and zero
  read-only or writable data exports. The regression-gated stack census
  advances **2,129/2,185 (97.44%)** to **2,130/2,185 (97.48%)**, changes
  only `tlog`, adds exactly `tlog.frexpf`, removes no accepted function, moves
  selector counts to **1,423 spilled / 355 scheduled**, and leaves **55
  `final-cost-policy`** fallbacks. The prior forced final-generic
  `tlog.logf` experiment remains a measured regression and stays behind the
  unchanged cost gate. No performance baseline changed.
- The next aggregate/multidimensional batch admits both seven-block
  `too.test_multidim` and `too.test_size_inference2` through one strict,
  name-free scheduled family. The two variants prove the complete
  144/401-instruction graphs, all 14/24 calls, every checker/helper
  relationship, inferred row/element counts, 12/3 and 12/6 multidimensional
  strides, all local initializer and loop stores, fixed local/global member
  addresses, and the original void returns. The emitters retain compact
  13-byte and 86-byte IX frames, use no IY, preserve the pointer alias and all
  check/report ordering, and keep every observed aggregate value in its
  original width. Normal selected/captured metrics are **2,402/2,521 bytes
  and 226/237 instructions** for `test_multidim`, and **5,981/7,109 bytes
  and 554/661 instructions** for `test_size_inference2`; stack-check metrics
  are **2,431/2,550 and 227/238**, and **6,010/7,138 and 555/662**.
  `too` passes full peep/nopeep. Selected versus one-function forced fallback
  improves `test_multidim` by **210 cycles peep** and **244 cycles nopeep**
  with equal linked sizes, and improves `test_size_inference2` by
  **9,176 cycles / 128 bytes peep** and **6,203 cycles / 256 bytes nopeep**.
  A separately renamed edge clone raises board weights to a 120,018-wide sum
  and local inferred-array values to 30,000..30,003; selected and fallback
  program output is byte-identical in both modes. Its per-function A/B is
  **1,885,183/1,885,393 cycles and 19,200/19,200 bytes peep** plus
  **1,902,066/1,902,379 and 20,224/20,224 nopeep** for the multidimensional
  variant, and **1,885,183/1,894,359 cycles and 19,200/19,328 bytes peep**
  plus **1,902,066/1,908,269 and 20,224/20,480 nopeep** for size inference.
  The regression-gated stack census advances **2,130/2,185 (97.48%)** to
  **2,132/2,185 (97.57%)**, adds exactly these two functions, removes none,
  moves selector counts from **1,423 spilled / 355 scheduled** to
  **1,421 spilled / 357 scheduled**, and leaves **53
  `final-cost-policy`** fallbacks. No production name, output hash, or
  baseline gate was added.
- The aggregate batch is now placed in its own compiled
  `dcc_mir_machine_aggregate_checks.c` family module rather than growing the
  core emitter. The module owns both plan variants, every table, matcher,
  emitter, and private helper; the plan is local to
  `mir_try_emit_aggregate_checks`, which is the module's only exported symbol.
  The dispatch remains exactly between `local-array-struct-checks` and
  `alias-mix`, preserving selector order. No adjacent established schedule was
  moved: those two neighbours have distinct plans and emission contracts, so
  moving either would expand the refactor's proof surface without improving
  family cohesion; in particular, `local-array-struct-checks` is coupled to
  the core `MirMachineForm` pointer/alias matcher stack rather than being a
  self-contained aggregate-check schedule. The pending implementation's
  `dcc_mir_machine_emit.c` falls from **49,695 to 48,352 lines** (**-1,343**);
  the new module is **1,567 lines** with **27 static top-level helpers**.
  `dcc_mir_machine_internal.h` is **49 lines** and adds only the family dispatch
  prototype; it declares no shared data. The export audit reports only
  `mir_try_emit_aggregate_checks [T]`, with no read-only or writable data.
  Canonical and CMake builds pass. Normal before/after censuses are
  byte-identical at SHA-256
  `93168b31174f680dafd4a427709d179f5dbc4f5e0a0da871d240b5c058f1dcd0`
  (**2043/2103, 97.15%**), and stack-check censuses are byte-identical at
  `ea904031465ae5d13981f54b5560e787ebc898b46b3c062f70df209fffaff0b8`
  (**2132/2185, 97.57%**): zero selector, metric, selected-hash, coverage, or
  validation-set changes. `too` full passes peep/nopeep with zero regression;
  selected totals remain **1,865,062 cycles / 21,888 bytes peep** and
  **1,882,005 / 22,912 nopeep**.
- Architecture rule for subsequent machine-family extractions: add **zero
  shared variables or mutable state**. Each module owns its plan structs,
  static arrays/constants, counters, candidate state, matchers, emitters, and
  selector ordering locally; it exports only one family dispatch entry, and
  `dcc_mir_machine_internal.h` remains a function-prototype-only contract.
  New schedules must be added directly to their cohesive family module; do not
  regrow `dcc_mir_machine_emit.c`. Use the core only when no established family
  fits, and extract a new family as soon as the schedule has a cohesive peer.
- `scripts/audit-c-module-exports.py` makes that rule repeatable: it compiles
  each module with the repository host C11/include settings, runs
  `nm -g --defined-only` (GNU nm or llvm-nm), reports LOC, best-effort static
  top-level functions, and exported code/read-only/writable data, then fails on
  writable storage or any function not explicitly allowlisted. Audit the two
  current extractions from any working directory with:
  ```sh
  python3 scripts/audit-c-module-exports.py \
    src/dcc/dcc_mir_machine_float_reports.c \
    --allow-function mir_try_emit_float_reports
  python3 scripts/audit-c-module-exports.py \
    src/dcc/dcc_mir_machine_attention.c \
    --allow-function mir_try_emit_attention_kernels
  python3 scripts/audit-c-module-exports.py \
    src/dcc/dcc_mir_machine_scanners.c \
    --allow-function mir_try_emit_scanner_kernels
  python3 scripts/audit-c-module-exports.py \
    src/dcc/dcc_mir_machine_aggregate_checks.c \
    --allow-function mir_try_emit_aggregate_checks
  ```
- Two strict name-free scheduled families admit the genuine nine-block
  call/check/report mains in `tfmaddr` and `tfpraw`. The float report family
  now accepts the fully proved nine-check, 195-instruction, 41-call,
  single-checker/no-local profile used by `tfmaddr.main`; the emitted and
  captured 42-call assembly sequences are identical. A new raw-conversion
  checker family accounts for all 519 instructions and 82 MIR calls in
  `tfpraw.main`: four distinct one-wide-argument predicate groups with
  6/7/6/7 uses, 26 boolean checks, two distinct seven-use conversion groups,
  14 wide checks through one reused union location, and the canonical
  check/failure summary, PASS/FAIL selection, and nonzero return. It emits
  each proved int/long-to-float conversion through the original `__fif` or
  `__flf` helper before the matching wrapper/check call. Its selected and
  captured 97-call assembly sequences are identical. Both families use no IY
  and retain a family-local strict-smaller gate. Ordinary selected/captured
  metrics are 4,189/4,331 bytes and 434/447 instructions for `tfmaddr`, and
  7,085/11,101 bytes and 729/1,152 instructions for `tfpraw`; stack-check
  metrics are 4,218/4,360 and 435/448, and 7,114/11,130 and 730/1,153.
  Full peep/nopeep validation passes. Selected versus forced-main-fallback
  results are `tfmaddr` 81,247/81,247 peep cycles and 81,247/81,373 nopeep,
  with 6,912/6,912 and 6,912/7,040 bytes; and `tfpraw` 81,057/84,309 and
  81,561/84,874 cycles, with 7,424/7,936 and 7,424/8,064 bytes. Deliberate
  one-check mismatch harnesses produce byte-identical failure text, summary,
  `RESULT: FAIL`, and nonzero return in selected/fallback peep and nopeep
  runs. No baseline changed. Forced final spilled output remains a real loss
  for the other fresh nine-block residuals: `tstrconv.main` regresses both
  modes by 0.25-0.43% and 512-640 bytes, while `tlog.frexpf` regresses
  3.11-3.29% and 384 bytes. They remain on fallback. The regression-gated
  stack census advances from 2124/2185 to 2126/2185 with exactly
  `tfmaddr.main` and `tfpraw.main`, and no removal.
- Three strict variants of the float call/report family now admit the remaining
  nine-block mains in `tasinfsp`, `tfmodfsp`, and `tfmaf`. They retain the
  complete existing recursive expression proof and add exact structural
  profiles for 177/396/289 MIR instructions, 12/27/15 checker calls,
  36/86/61 total calls, and checker-use distributions 10+2, 8+4+15, and
  14+1. The variant emitter evaluates each proven call argument in the
  established reverse ABI order; the complete selected and captured main call
  sequences are identical in all three applications. Float unary constants
  fold their sign bit at emission, while every float runtime, checker, bitcast,
  FMA, modulo, and printf call remains present and ordered. `tasinfsp` uses one
  four-byte IX NaN snapshot, `tfmodfsp` uses one four-byte IX NaN snapshot and
  rematerializes its proven infinity constants, and `tfmaf` is frameless. No IY
  is used. The family-local strict-smaller gate remains in force.
  Non-stack selected/captured metrics are respectively 3,445/3,460 bytes and
  340/346 instructions; 8,483/8,802 and 881/909; and 6,407/6,557 and 667/680.
  Stack-check metrics are 3,474/3,489 and 341/347; 8,512/8,831 and 882/910;
  and 6,436/6,586 and 668/681. Affected full peep/nopeep validation passes with
  no checked regression. Selected versus forced-main-fallback results are:
  `tasinfsp` 131,380/131,380 peep cycles and 131,379/131,437 nopeep, with
  7,808/7,808 and 7,808/7,936 bytes; `tfmodfsp` 448,571/449,118 and
  449,706/450,308 cycles, with 10,368/10,368 and 10,368/10,496 bytes; and
  `tfmaf` 104,794/104,794 and 104,794/104,920 cycles, with 7,680/7,680 and
  7,680/7,808 bytes. Separately named failure harnesses retain the same
  nine-block structures while deliberately mismatching one asin value, one
  modulo bit result, and one FMA bit result. Selected and fallback program
  output is identical in both modes, including each failure line, summary,
  `RESULT: FAIL`, and nonzero return. Selected/fallback failure-harness
  peep/nopeep cycles are 213,080/213,092 and 213,084/213,164; 490,773/491,332
  and 491,898/492,522; and 145,414/145,414 and 145,414/145,562. Linked sizes
  are equal in peep; selected nopeep is equal for asin and 128 bytes smaller
  for modulo and FMA. No performance baseline changed. The regression-gated
  stack census advances from 2121/2185 to 2124/2185 with exactly these three
  additions and no removal.
- One shared strict name-free float call/report scheduler now admits the
  nine-block `main` graphs in `tlogfsp`, `tfloorsp`, `tatan2sp`, and
  `tfpspec`; it also replaces the larger active spilled schedules in
  `tfdf.main` and `tsqrtsp.main`. The matcher requires at least 14 ordered
  checks, 38 direct calls, exactly two repeated three-argument checker
  identities, one captured nonvolatile float snapshot, the canonical two
  variadic summary/result prints, distinct global check/failure counters,
  and the exact final failure-to-boolean control flow. It recursively proves
  every constant, snapshot, local pointer output, unary/binary float
  operation, direct call prototype/target, argument definition/index, and
  checker width; every instruction before the final report is accounted for.
  Emission schedules effectful arguments in MIR order, rematerializes only
  proven values, retains all float runtime/check/print calls, and uses a
  four-byte IX snapshot frame except for `tfloorsp`'s compact 12-byte
  snapshot/`modff` output frame. No IY is used. A final family-local cost gate
  requires the complete emitted assembly stream to be strictly smaller than
  the captured stream. This keeps `tasinfsp.main`, `tfmodfsp.main`, and
  `tfmaf.main` on fallback after forced A/B exposed real cycle/sector losses.
  The six retained apps pass full peep/nopeep and forced-main-fallback A/B
  with identical output and no checked regression. A separately named
  30-check infinity/NaN/signed-zero harness selects at 8,948/9,696 bytes and
  exercises the failure print/nonzero return; selected and fallback output
  are identical in both modes, while selected improves 341,694 to 339,981
  peep cycles and 341,762 to 339,984 nopeep cycles, with linked size
  8,576 to 8,320 bytes in both modes. The regression-gated stack census
  advances from 2117/2185 to 2121/2185 with exactly four additions and no
  removal.
- A strict name-free conversion-check scheduler now admits the exact
  482-instruction, eight-block `tatof.main` graph. The matcher validates all
  46 checks in source order: 26 float comparisons, three float-to-int
  conversions, nine end-pointer checks, five infinity checks, and three NaN
  checks. It proves all 37 one-pointer float conversion calls, the three
  signed float-to-int ABI conversions, all three float multiplications, every
  typed float/word constant, all 94 string operands, every call-site argument
  index and definition, each checker prototype and repeated symbol identity,
  both variadic print branches, the distinct global check/failure counters,
  and the final reloaded failure-to-boolean return. Selection uses only the
  complete MIR structure, types, prototypes, storage relationships, and CFG;
  no source/app/function name or output hash is consulted. Emission is
  frameless, uses no IY, retains the DE:HL float ABI and the established
  `__fmf`/`__ffi` conversions, preserves all conversion/check/print calls and
  their ordering, and keeps the original success/failure paths. Non-stack
  selected/captured metrics are 8,039/8,775 bytes and 810/922 instructions;
  stack-check metrics are 8,068/8,804 bytes and 811/923 instructions.
  `tatof` passes full peep/nopeep. Checked performance improves peep from
  1,847,963 to 1,841,506 cycles (-6,457 / -0.35%) and 9,600 to 9,344 bytes
  (-256 / -2.67%), and nopeep from 1,852,891 to 1,843,460 cycles
  (-9,431 / -0.51%) and 9,984 to 9,344 bytes (-640 / -6.41%). A separately
  renamed conversion-edge harness selects at 8,075/8,811 bytes and 811/923
  instructions while checking 32,767, -32,767, and a scaled 16,383.5-to-32,767
  conversion. Selected and forced-fallback output is identical in both modes.
  Peep improves from 1,878,263 to 1,877,132 cycles with equal 8,704-byte linked
  size; nopeep improves from 1,880,291 to 1,879,086 cycles and 8,832 to
  8,704 bytes. A deliberate one-check mismatch exercises the failure print and
  nonzero return path; selected and forced-fallback program output is identical
  in peep and nopeep modes (`got 32767 expected 32766`, then `tatof FAILED 1`).
  No performance baseline changes were made. The regression-gated stack-check
  census advances from 2116/2185 to 2117/2185 with exactly `tatof.main` and no
  removal.
- A strict name-free formatted-buffer scheduler now admits the exact
  897-instruction, eight-block `tzpad.main` graph. The matcher proves the
  single 64-byte local character array and every one of its 122 address
  uses, all 61 alternating three-argument variadic formatting calls and
  three-pointer checker calls, all 185 string operands, all integer and long
  constant widths/bits, each call-site argument index and definition, each
  per-format runtime hook and assembly target, and both final variadic print
  branches plus the reloaded global failure return. Formatting, checker, and
  print callees are selected only by prototype, definition status, and
  repeated symbol relationships; no source function or application name is
  consulted. The emitter keeps the compact 64-byte buffer directly below IX,
  carries its address in DE between calls by restoring the already-pushed
  first argument, preserves the wide variadic stack order and hex/octal hooks,
  and uses no IY. Non-stack selected/captured metrics are 11,729/15,241 bytes
  and 1,207/1,459 instructions; stack-check metrics are 11,758/15,270 bytes
  and 1,208/1,460 instructions. `tzpad` passes full peep/nopeep. The checked
  performance gate improves peep from 447,775 to 443,108 cycles
  (-4,667 / -1.04%) and 9,344 to 8,704 bytes (-640 / -6.85%), and nopeep from
  447,708 to 442,984 cycles (-4,724 / -1.06%) and 9,472 to 8,704 bytes
  (-768 / -8.11%). A separately renamed buffer-edge harness selects the same
  graph while formatting 63 characters plus the terminator into the complete
  64-byte buffer. Selected and forced-fallback program output is identical in
  both modes; peep improves from 460,831 to 456,164 cycles and 6,784 to 6,016
  bytes, while nopeep improves from 460,764 to 456,040 cycles and 6,784 to
  6,144 bytes. No name, hash, performance baseline, or IY allocation
  participates in selection. The regression-gated stack-check census advances
  from 2115/2185 to 2116/2185 with exactly `tzpad.main` and no removal.
- A strict name-free unsigned-long binary-search scheduler now admits the
  72-instruction, eight-block `primes.ulsqrt` graph. The matcher validates the
  single unsigned 32-bit parameter and stack position, all four local
  identities, `low = 1`, `high = n / 2`, `result = 0`, the `n <= 1` return,
  the unsigned `low <= high` loop, the exact overflow-safe midpoint
  `low + ((high - low) / 2)`, the ordered `n / mid` quotient test, both
  `mid +/- 1` updates, and the final 32-bit return. The emitter uses a compact
  12-byte IX frame for only low, high, and result; midpoint remains in DE:HL
  and a transient stack save instead of retaining the fourth source local.
  Division by two is emitted as a logical 32-bit shift. The variable division
  preserves the `__ldu` fastcall ABI exactly: the dividend is pushed and the
  divisor arrives in DE:HL, with midpoint kept below the pushed dividend for
  the subsequent update. Unsigned comparisons run most-significant byte first,
  and all midpoint/update arithmetic propagates the original 32-bit
  carry/borrow behavior. No IY is used. Non-stack selected/captured metrics are
  1,652/2,596 bytes and 146/243 instructions; stack-check metrics are
  1,681/2,625 bytes and 147/244 instructions. `primes` passes full peep/nopeep.
  Against forcing only this function back, cycles improve from 6,292,774 to
  4,619,721 peep (-1,673,053 / -26.59%) and from 6,347,782 to 4,635,343
  nopeep (-1,712,439 / -26.98%); linked size falls from 7,168 to 7,040 bytes
  in both modes (-128 / -1.79%). A separately renamed boundary harness selects
  with the same stack-check metrics and produces identical selected/fallback
  program output in peep and nopeep modes for 0, 1, small exact/non-exact
  squares, the 16-bit transition, signed-boundary bit patterns, and values
  through `UINT32_MAX`; every checked result is correct, including 65,535 for
  the maximum input. The harness improves from 4,680,157 to 3,678,926 peep
  cycles (-1,001,231 / -21.39%) and from 4,705,739 to 3,684,533 nopeep cycles
  (-1,021,206 / -21.70%); linked sizes fall 3,840 to 3,712 bytes peep and
  4,096 to 3,840 bytes nopeep. No app/function name, hash, performance
  baseline, or IY allocation participates in selection. The regression-gated
  stack-check census advances from 2114/2185 to 2115/2185 with exactly this
  function and no removal.
- The strict name-free backward-orchestration scheduler now admits the
  730-instruction, 37-block `attnc11.backward_pass` graph. Its exact structural
  gate validates the full opcode/control-flow sequence, all 101 typed
  constants, all global-location alias relationships, every two-byte indexed
  stride, all 24 direct calls and argument-definition order, each helper
  prototype/long-value ABI, and every loop branch, backedge, and PHI. The
  emitter composes the already-landed softmax, matrix multiply/add, transposed
  multiply, outer-product, dot-product, scaled-add, Q16 conversion, shift, and
  clamp helpers instead of reproducing their arithmetic. It preserves the six
  backward stages and every gradient update in source order, including the
  in-place score-gradient rewrite, the distinct row-dot checkpoint across its
  inner loop, and the second embedding-gradient reload after the token-gradient
  store. A six-byte IX frame holds only counters and scalar checkpoints; no IY
  is used. Non-stack selected/captured metrics are 9,802/12,121 bytes and
  942/1,069 instructions; stack-check metrics are 9,831/12,150 bytes and
  943/1,070 instructions. `attnc11` passes full peep/nopeep. Against forcing
  only this function back, cycles remain exactly 339,250,960 peep /
  342,162,991 nopeep, while linked size falls from 23,680 to 23,424 bytes peep
  (-256) and from 25,344 to 24,960 bytes nopeep (-384). A separately renamed
  edge harness selects with the same stack-check metrics and produces
  byte-identical selected/forced-fallback program output for signed extrema,
  boundary target indices, in-place softmax and score updates, sequential
  token/position accumulation, and all six gradient stages. It improves from
  46,649,233 to 46,625,765 peep cycles (-23,468 / -0.0503%) and from
  47,232,446 to 47,136,389 nopeep cycles (-96,057 / -0.2034%); linked sizes
  fall 19,200 to 18,944 bytes peep and 20,736 to 20,352 bytes nopeep. No
  app/function name, hash, performance-baseline change, or IY allocation
  participates in selection. The regression-gated stack-check census advances
  from 2113/2185 to 2114/2185 with exactly this function and no removal.
- The shared strict name-free matrix-product scheduler now also admits the
  141-instruction, 19-block `attnc11.matrix_vector_add` graph. The new variant
  proves all five parameter types and stack positions, unsigned byte row and
  column dimensions, both loops and PHIs, two-byte matrix postincrement and
  indexed input stride, each ordered signed 16x16-to-32 multiplication and
  signed 32-bit accumulation, the complete saturating Q16-to-Q8 conversion
  CFG, output postincrement before destination reload, the final signed
  wide addition, clamp call, and indirect store. Emission follows the existing
  attention-kernel cursor policy: IX advances through the matrix, DE resets to
  the input base for each row and advances only after each read, and BC carries
  both unsigned loop counts across the multiply and clamp boundaries. A
  four-byte stack accumulator is the only frame storage; no IY is used.
  Matrix is read before input on every product, the output cursor store remains
  before its destination load, and output is written only after a complete row,
  preserving matrix/input/output overlap behavior. Non-stack selected/captured
  metrics are 1,861/2,893 bytes and 195/278 instructions; stack-check metrics
  are 1,890/2,922 bytes and 196/279 instructions. `attnc11` passes full
  peep/nopeep. Against forcing only this function back, cycles remain exactly
  339,250,960 peep / 342,162,991 nopeep, while linked size falls from 23,808
  to 23,680 bytes peep (-128) and 25,600 to 25,344 bytes nopeep (-256).
  A separately named alias harness selects with the same stack-check metrics
  and produces identical selected/forced-fallback output in peep and nopeep
  modes for ordinary, zero-row, zero-column, Q16 saturation, negative
  truncation, final-add saturation, matrix/input, output/input,
  output/future-matrix, and all-three alias cases. It improves from 342,428 to
  327,826 peep cycles (-14,602 / -4.26%) and from 354,745 to 334,952 nopeep
  cycles (-19,793 / -5.58%); linked sizes fall 3,968 to 3,840 bytes peep and
  4,224 to 3,968 bytes nopeep. No app/function name, hash, performance
  baseline, or IY allocation participates in selection. The regression-gated
  stack-check census advances from 2112/2185 to 2113/2185 with exactly this
  function and no removal.
- One strict name-free softmax scheduler now admits `attnc11.softmax`. The
  matcher validates the complete 132-instruction, nine-block graph: the
  pointer/unsigned-byte parameters and stack positions, the initial
  three-argument maximum scan and its distinct dummy output, the first
  bounded traversal, signed wrapped maximum difference and negative clamp,
  three-bit shift and 255 cap, ordered lookup/store/reload accumulation, the
  second bounded traversal, signed long Q8 numerator construction and
  division, the one-argument clamp call, and both pointer/index increments.
  Emission preserves every call and source evaluation point, including table
  loads after the current item read and stores before accumulation, so a
  vector overlapping the lookup table remains ordered safely. Each traversal
  keeps its cursor in DE and its unsigned remaining count in B; the complete
  frame is six bytes for the distinct dummy, maximum, and sum words. No IY is
  used. Non-stack selected/captured metrics are 1,290/2,323 bytes and 131/208
  instructions; stack-check metrics are 1,319/2,352 bytes and 132/209
  instructions. `attnc11` passes full peep/nopeep. Against forcing only this
  function back, cycles remain exactly 339,250,960 peep / 342,162,991
  nopeep, while linked sizes improve from 23,936 to 23,808 bytes peep and
  from 25,728 to 25,600 bytes nopeep (-128 each). A separately named harness
  selects at 1,278/2,304 bytes and 131/208 instructions and produces
  byte-identical program output in selected peep/nopeep and forced-fallback
  peep/nopeep modes for ordinary, zero-length, one-element, lookup-cap,
  signed-overflow, lookup-table alias, and 255-element cases. The harness
  improves from 2,574,317 to 2,378,680 peep cycles (-7.60%) and from
  3,083,359 to 2,808,535 nopeep cycles (-8.91%); linked sizes fall
  4,736 to 4,608 bytes and 5,120 to 4,864 bytes. No app/function name, hash,
  performance-baseline change, or IY allocation participates in selection.
  The regression-gated stack-check census advances from 2111/2185 to
  2112/2185 with exactly this function and no removal.
- One strict name-free matrix-product scheduler now admits
  `attnc11.transposed_matrix_vector_multiply` and
  `attnc11.add_outer_product`. The shared matcher validates each complete
  seven-block graph, all five parameter types and stack positions, unsigned
  byte bounds, both loop/control edges, two-byte matrix/vector strides,
  source-pointer postincrements, per-product signed 16x16-to-32 multiply,
  Q8 conversion call, signed 32-bit accumulation, clamp call, and final
  two-byte store. Its two structural variants preserve the source order:
  transposed multiply advances and reads the matrix before reading the indexed
  output, while outer product reads the indexed right operand before advancing
  and reading the matrix destination. The emitter uses the established
  `__m1s`, Q8-conversion, clamp, and `__msf` fastcall ABIs; IX is the advancing
  matrix cursor, DE is the reset-per-row vector cursor, and BC carries both
  byte induction values across calls. A four-byte stack frame holds only the
  current scalar and the authoritative outer-count checkpoint; no IY is used.
  Non-stack selected/captured metrics are 1,342/1,649 bytes and 139/149
  instructions for transposed multiply, and 1,274/1,465 bytes and 134/132
  instructions for outer product. Stack-check metrics are 1,371/1,678 bytes
  and 140/150 instructions, and 1,303/1,494 bytes and 135/133 instructions,
  respectively. `attnc11` passes full peep/nopeep. Against forcing either new
  function back while leaving the other selected, cycles remain exactly
  339,250,960 peep / 342,162,991 nopeep and peep linked size remains 23,936
  bytes; each selected function independently reduces nopeep linked size from
  25,856 to 25,728 bytes (-128). A separately named focused harness matched
  both variants and produced identical selected/forced-fallback output in
  peep and nopeep modes for ordinary operation plus output/matrix,
  output/input, right/matrix, and left/matrix alias cases. That harness caught
  and rejected an initial ordinary-stack `memset` call and an unsafe
  outer-counter iteration before the retained fastcall/register schedule.
  No app/function name, hash, performance-baseline change, or IY allocation
  participates in selection. The regression-gated stack-check census advances
  from 2109/2185 to 2111/2185 with exactly these two functions and no removal.
- The strict name-free buffered-declaration scheduler admits
  `forint.parse_decls`. It validates the complete 72-instruction, 11-block
  graph: the signed `i < g_ns` bound, per-iteration `g_stmts` reload and
  66-byte record stride, text-field load, `strcpy` then `trim`, all three
  ordered prefix tests, their `parse_decl` calls and type arguments, every
  false edge, and the cursor PHI/increment/backedge. The emitter keeps the
  cursor in BC, saves it across the complete call sequence, retains the
  original 162-byte IX frame, and caches the stable buffer address in the
  promoted cursor's otherwise dead frame slot. It reloads both globals at
  their source evaluation points and uses no IY. Non-stack selected output is
  1,290 text bytes / 124 instructions versus 1,452 / 138 captured;
  stack-check output is 1,319 / 125 versus 1,481 / 139. Against forced
  fallback, `forint` improves from 658,293,675 to 658,293,229 peep cycles
  (-446) and from 698,119,833 to 698,118,064 nopeep cycles (-1,769), with
  linked sizes unchanged at 33,152 / 36,608 bytes. A separately renamed
  `scan_declaration_buffer` clone selects with identical stack-check metrics.
  No app/function name, hash, performance-baseline change, or IY allocation
  participates in selection. The regression-gated stack-check census is
  2109/2185 with exactly this one new function and no removal.
- The strict name-free action-dispatch scheduler admits
  `forint.decode_action`. The two requested 11-block graphs do not support one
  safe shared emitter family: `decode_action` is a local-free two-parameter
  branch dispatcher, while `parse_decls` is a bounded global-table loop with a
  162-byte local buffer. The higher-value old gap was therefore selected
  (`decode_action` +936 bytes versus `parse_decls` +688 under stack check).
  The matcher validates the complete 80-instruction graph, both pointer
  parameters, three short-circuited prefix calls, the initial/default, GOTO,
  RETURN, and target-label stores, the label parser call, the separator search,
  the three-argument assignment decoder call, and all four source return
  paths. The emitter reloads parameters from a stable IX frame around every
  call, performs the default action write before classification, retains the
  GOTO action write before parsing its target, and uses no IY. Non-stack
  selected output is 1,135 text bytes / 108 instructions versus 1,307 / 123
  captured; stack-check output is 1,164 / 109 versus 1,336 / 124. Against
  forced fallback, `forint` improves from 658,293,779 to 658,293,675 peep
  cycles (-104) and from 698,125,957 to 698,119,833 nopeep cycles (-6,124);
  peep linked size is unchanged at 33,152 bytes and nopeep falls from 36,736
  to 36,608 bytes (-128 / -0.35%). A separately renamed `action_dispatch`
  clone selects with identical stack-check metrics. No app/function name,
  hash, baseline exception, or IY allocation participates in selection. The
  fresh stack-check census is 2108/2185 with exactly this one new function and
  no regression.
- The strict name-free whitespace-scan scheduler admits `cint.skip_ws`. It
  matches the complete 60-instruction, eight-block MIR graph, proves the
  signed 32-bit source-cursor bound, preserves the unsigned-byte helper call
  and short-circuit edges, reloads the source pointer and cursor after that
  call before testing newline, and retains the 16-bit line update plus
  full-width cursor increment in source evaluation order. The emitter holds
  only the global state address in IX, reloads all mutable fields around the
  helper call, and uses no IY. Non-stack selected output is 661 text bytes /
  57 instructions versus 1,423 / 139 captured; stack-check output is 690 /
  58 versus 1,452 / 140. Against forced fallback, `cint` improves from
  299,327,627 to 299,201,573 peep cycles (-126,054 / -0.0421%) and from
  305,443,007 to 305,244,693 nopeep cycles (-198,314 / -0.0649%); peep
  linked size is unchanged at 31,360 bytes and nopeep falls from 36,224 to
  36,096 bytes (-128 / -0.35%). A separately named fixture with a
  side-effecting helper selects the same structural schedule and produces
  identical peep, nopeep, and forced-fallback cursor/line/call-count output
  for mutation-after-call, empty-bound, and multiline whitespace cases. No
  app/function name, hash, baseline exception, or IY allocation participates
  in selection. The fresh stack-check census is 2107/2185 with exactly this
  one new function and no regression.
- The strict name-free symbol-table scheduler admits `bint.sym_find`. It
  matches the complete 87-instruction, seven-block MIR graph, proves the
  signed `i < nsym` bound and both `nsym`/`mtop` capacity checks, preserves the
  two-argument string-comparison call, `strncpy`, both error calls, both return
  cases, and every observable `sym`, `nsym`, and `mtop` access. The frameless
  emitter keeps the index in BC, record cursor in HL, and loop bound in DE,
  saving and restoring all three across comparison calls; the input name is
  retained on the machine stack and neither IX nor IY is used. Non-stack
  selected output is 1,533 text bytes / 151 instructions versus 2,063 / 196
  captured; stack-check output is 1,562 / 152 versus 2,092 / 197. Against
  forced fallback, `bint` improves from 334,564,928 to 334,558,534 peep cycles
  (-6,394 / -0.0019%) and from 336,986,804 to 336,973,716 nopeep cycles
  (-13,088 / -0.0039%); peep linked size is unchanged at 22,144 bytes and
  nopeep falls from 24,448 to 24,320 bytes (-128 / -0.52%). A separately
  named `lookup_symbol` clone selects with identical stack-check metrics. No
  app/function name, hash, baseline exception, IX frame, or IY allocation
  participates in selection. The fresh stack-check census is 2106/2185 with
  exactly this one new function and no regression.
- The strict name-free comment-scan scheduler admits `fint.skip_comment`. It
  matches the complete 60-instruction, seven-block MIR graph and validates
  the signed 32-bit cursor bound, old-cursor byte load, full-width cursor
  increment, newline line-number update, closing-delimiter exit, Ctrl-Z
  cursor-to-end assignment, and loop/exit edges structurally. The emitter
  retains the state pointer in IX, the low cursor word in BC, the source
  pointer in DE, and the scanned byte in A; the high cursor word and 32-bit
  end remain authoritative in memory, and IY is not used. Non-stack selected
  output is 691 text bytes / 61 instructions versus 1,651 / 162 captured;
  stack-check output is 720 / 62 versus 1,680 / 163. The ordinary `fint`
  full run and forced fallback have identical peep/nopeep cycles at
  378,912,094 / 382,764,623; peep linked size is unchanged at 32,128 bytes,
  while nopeep falls from 35,072 to 34,944 bytes (-128 / -0.36%). A focused
  multiline-comment fixture improves from 674,391 to 654,615 peep cycles
  (-2.93%) and from 739,128 to 715,939 nopeep cycles (-3.14%). Selected and
  forced-fallback outputs are byte-identical for closing-delimiter,
  multiline line-number, embedded Ctrl-Z, and end-boundary cases. A separately
  named `scan_comment_body` clone selects with the same 720-byte /
  62-instruction stack-check metrics. No app/function name, hash, baseline
  exception, or IY allocation participates in selection. The fresh
  stack-check census is 2105/2185 with exactly this one new function and no
  regression.
- The strict name-free context-operation scheduler admits `tctxops.main`. It
  matches the complete 433-instruction, seven-block MIR graph, retains the
  eight-iteration long-array initialization loop and both global float stores,
  and preserves all 33 helper, 33 checker, and two `printf` calls in their
  original side-effect order. Each helper prototype, constant argument,
  result conversion, checker type, expected value, string, repeated-callee
  relationship, and final success/failure branch is validated structurally;
  no app/function-name, hash, or baseline exception participates in selection.
  The emitter is frameless, uses neither IX nor IY, and keeps the
  initialization and final report branches executable. Non-stack selected
  output is 8,286 text bytes / 886 instructions versus 9,047 / 970 captured;
  stack-check output is 8,315 /
  887 versus 9,076 / 971. Against forced fallback, `tctxops` improves from
  122,667 to 105,614 peep cycles (-13.90%) and from 124,726 to 107,409
  nopeep cycles (-13.88%), with linked sizes non-regressing at 10,752 and
  11,008 bytes respectively. The fresh stack-check census is 2104/2185 with
  exactly this one new function and no regression. A separately named
  `context_driver` clone selected through the same matcher (8,315 / 887
  versus 9,067 / 970 captured) and produced byte-identical output in peep,
  nopeep, and forced-fallback runs.
- The strict name-free ctype/reallocation scheduler admits `tctype.main`. It
  matches the complete five-block graph, preserves all 14 ctype, 17 checker,
  five `printf`, and six allocation/string calls in their MIR side-effect
  order, and retains each null-return branch plus the final global-failure
  report. The emitter keeps the pointer in one persistent stack word, has no
  IX frame and uses no IY. Zero-result ctype checks use a direct HL truth test
  so the shipping peep path remains profitable. Non-stack selected output is
  3,885 text bytes / 381 instructions versus 4,423 / 430 captured;
  stack-check output is 3,914 / 382 versus 4,452 / 431. Against forced
  fallback, `tctype` improves from 23,887 to 23,800 peep cycles (-0.36%) and
  from 24,372 to 23,766 nopeep cycles (-2.49%). Peep linked size remains
  7,424 bytes; nopeep falls from 7,680 to 7,424 bytes (-256 / -3.33%). The
  fresh stack-check census is 2103/2185 with exactly this one new function
  and no regression. A separately named scratch clone added `EOF`, 128, and
  255 inputs to `isalpha`/`toupper`/`tolower`; the same matcher selected its
  4,536-byte / 444-instruction stack-check graph versus 5,189 / 502 captured,
  and peep, nopeep, and forced-fallback runs all printed
  `ctype/realloc ok`.
- The strict name-free VLA pointer-element switch scheduler admits
  `tvla.vla_ptr2d_deref_chain_switch`. It matches the complete 51-instruction,
  five-block MIR graph, proves the two word parameters at their ABI offsets,
  requires the pointer-to-array runtime-stride marker, validates all three
  explicit `0*6 + 1*2` dereference address chains, and preserves the case and
  default word stores plus the final nonvolatile reload. The frameless emitter
  keeps the switch branches and stack-check call, uses no IX or IY, and does
  not fold away the observable store/reload. Non-stack selected output is
  290 text bytes / 29 instructions versus 507 / 47 captured; stack-check
  output is 319 / 30 versus 536 / 48. Against forced fallback, `tvla`
  improves from 12,372,468 to 12,372,363 peep cycles (-105) and from
  14,771,615 to 14,771,454 nopeep cycles (-161), with linked sizes unchanged
  at 29,696 / 35,328 bytes. The fresh stack-check census is 2102/2185 with
  exactly this one new function and no regression. A separately named
  `pointer_switch` scratch clone selected through the same matcher and produced
  `20 20 99 99` in peep, nopeep, and forced-fallback runs, exercising both
  switch branches and both observable stores/reloads.
- The strict name-free math-verification scheduler admits `tmathf.main`. It
  structurally matches the complete two-block graph and preserves all five
  `printf`, 74 `chk`, 16 `chkx`, and 84 math-runtime calls in their original
  evaluation order, including the `frexpf`/`modff` pointer outputs and four
  int-to-float conversions. Emission reuses the ordinary symbol-call,
  reverse-argument, float-constant, and cleanup helpers; it does not evaluate
  any math result at compile time. One four-byte IX frame overlays the
  two-byte exponent output with the later four-byte float output, and no IY is
  used. Non-stack selected output is 19,750 text bytes / 2,042 instructions
  versus 20,184 / 2,090 captured; stack-check output is 19,779 / 2,043 versus
  20,213 / 2,091. `tmathf` improves from 2,993,444 to 2,978,018 peep cycles
  (-0.52%) and from 3,004,974 to 2,984,180 nopeep cycles (-0.69%); linked size
  falls 256 bytes in both modes. The stack-check census is 2101/2185 with
  exactly this one new function and no regression.
- The strict name-free compound-check scheduler admits `tcaslv.main`. A
  sequential MIR proof validates all 116 local/pointer/array/member stores,
  folds only proven integer values, and preserves all 71 `chk` calls, the
  zero-argument compound-assignment helper call, the final failure load and
  the success print in their original order. Emission uses a 23-byte IX frame
  and no IY. Non-stack selected output is 10,462 text bytes / 1,044
  instructions versus 17,650 / 1,728 captured; stack-check output is 10,491 /
  1,045 versus 17,679 / 1,729. `tcaslv` improves from 2,222,225 to 2,210,323
  peep cycles (-0.54%) and from 2,229,962 to 2,217,366 nopeep cycles (-0.56%);
  linked size falls 1,280 / 1,152 bytes. The fresh non-stack census is
  2011/2101 and the stack-check census is 2100/2185, each with exactly this
  one new function and no regression.
- The strict name-free final-call scheduler admits `tstdlib.main`. It matches
  the exact two-block call graph structurally, preserves all 77 calls
  (including four `abs`, four `labs`, ten `atoi`, fifteen `atol`, ten
  direct div/ldiv wrappers among 43 checker calls, and the success print), and
  emits every outer argument in reverse ABI order. The function is frameless
  and uses neither IX nor IY. Non-stack selected output falls from 7,427 to 7,363
  text bytes and from 779 to 768 instructions; stack-check output falls from
  7,456 to 7,392 bytes and from 780 to 769 instructions. Against forced
  fallback, `tstdlib` changes from 125,360 to 125,358 peep cycles (-2) and
  from 127,063 to 126,939 nopeep cycles (-124), with linked sizes unchanged
  at 9,472 bytes. The fresh non-stack census is 2010/2101 and the stack-check
  census is 2099/2185, each with exactly this one new function and no
  regression.
- The name-free pointer-alias mix scheduler admits
  `tptrlhs.touch_alias_mix`. It structurally validates all 22 local pointer
  initializations, retains each of the six runtime global pointer-member
  loads, and preserves all 24 observable stores and 24 checker calls in
  source order. The emitted function is frameless and uses no IY. In the
  stack-check census, selected output falls from 15,723 to 5,526 text bytes
  and from 1,537 to 533 instructions; the rejected spilled candidate was
  20,343 bytes / 1,867 instructions. `tptrlhs` improves from 974,367 to
  925,005 peep cycles (-5.07%) and from 1,021,278 to 967,079 nopeep cycles
  (-5.31%); linked size falls 1,152/1,664 bytes. The fresh non-stack census
  is 2009/2101 and the stack-check census is 2098/2185, each with exactly
  this one new function and no regression.
- The name-free bitfield report scheduler admits `tbitfld.main`. It reuses one
  four-byte IX aggregate slot, uses no IY, preserves all 12 `printf`, eight
  aggregate-sum, and two hidden-buffer aggregate-return calls, and emits direct
  packed constants for structurally proven assignments and RMW results. In the
  stack-check census, selected output falls from 21,489 to 4,616 text bytes and
  from 2,485 to 480 instructions. `tbitfld` improves from 361,103 to 348,102
  peep cycles (-3.60%) and from 364,093 to 350,003 nopeep cycles (-3.87%);
  linked size falls 2,432/2,688 bytes. The fresh non-stack census is
  2008/2101 and the stack-check census is 2097/2185, each with exactly this one
  new function and no regression.
- The name-free local-array/struct check scheduler admits
  `tpostptr.test_arrays_and_structs`. It packs the arrays and struct into one
  70-byte IX frame without overlapping potentially escaped call arguments,
  uses no IY, and preserves all 12 mutation-helper plus 24 checker calls.
  In the stack-check census, selected output falls from 10,418 to 6,604 text
  bytes and from 1,036 to 680 instructions. `tpostptr` improves from 99,558
  to 95,743 peep cycles (-3.83%) and from 104,611 to 101,323 nopeep cycles
  (-3.14%); linked size falls 640/512 bytes. The fresh non-stack census is
  2007/2101 and the stack-check census is 2096/2185, each with exactly this
  one new function and no regression.
- The scheduled wide div-result checker preserves the eight-byte hidden
  `ldiv_t` return buffer and reverse call ABI, compares quotient/remainder
  directly in DE:HL, and validates `quot*denom+rem` with one `__lmul`. This
  admits `tstdlib.check_ldiv`; `tstdlib` improves from 126,755 to 125,360
  peep cycles (-1.10%) and 129,028 to 127,063 nopeep cycles (-1.52%), while
  linked size falls by 128 bytes peep and 256 bytes nopeep.
- `a1` is **23/23 MIR**. Relative to main it is **11.12% faster peep** and
  **12.98% faster nopeep**, with no checked size regression.
- The release gate is clean: 314 runnable apps, diagnostics, 22 dccpeep
  fixtures, and the extended suite pass in both peep/nopeep modes.
- Large-CFG MIR now uses depth-three LIFO stack handoffs, typed compact byte
  slots, call-bounded store-address forwarding, single-use unary forwarding,
  and direct `+1/+2` increments. Tagged byte-slot cleanup and final liveness
  narrowing in dccpeep preserve larger canonical passes.
- The scheduled byte-array reduction keeps the pointer in BC, the accumulator
  in DE, and the endpoint in callee-saved IY. `treg.sumarray` is newly MIR and
  improves **7.33% peep / 8.65% nopeep**.
- The fixed-three-column word reduction flattens `tvla.vla_sum2d` to one
  endpoint loop. It is newly MIR and improves both modes while reducing linked
  size by 256/384 bytes.
- The structurally profiled VLA wide-truncation loop admits
  `tvla.vla_long_rhs_store` after forced dual-mode A/B proved it faster and
  smaller; the predicate is terminal, hashless, and matches one candidate.
- The structurally profiled variadic macro-validation loop admits
  `tvariad.check_macro_values`; forced and production full-mode runs improve
  about 5% in both modes and reduce linked size.
- The scheduled fixed-wide zero scan admits hot `catalan.is_zero`, improving
  both modes and shrinking 128 bytes. The profiled call-check runner predicate
  admits both `tstretst.run_direct` and `run_helper`; together they improve
  0.66% peep / 4.87% nopeep.
- The constant byte-fill scheduler admits `tecreg.fill_bytes`, using HL/A/B
  instead of a frame induction slot. It improves 5.74% peep / 28.07% nopeep.
- The local fill+sum+print scheduler admits `tecreg.main`, completing that app's
  MIR coverage and improving it 43.37% peep / 59.46% nopeep.
- The affine byte-fill scheduler keeps the pointer in HL and fill byte in A,
  deriving each stored byte from B+C without frame traffic. Its companion
  local reduction scheduler admits `tctrreg.stamp` and `tctrreg.main`,
  completing that app's MIR coverage and improving it 24.68% peep / 40.49%
  nopeep.
- The terminal constant-switch scheduler uses frameless SP-relative parameter
  access and word result tables. A bounded constant-flow evaluator folds pure
  local fallthrough updates while rejecting parameter-dependent tail control.
  It admits `tc89swjt.swdn`, `tc89swjt.swft`, and `tdead.ds_sw`;
  `tc89swjt` improves 7.87% peep / 9.42% nopeep.
- The wide left-shift counter keeps the unsigned long in DE:HL and the loop
  count in BC. It admits `tcrcfix.crc_t_bits_probe`, improving that app 7.53%
  peep / 6.70% nopeep and shrinking both linked modes.
- The palindrome scheduler scans through HL, then keeps the converging left
  and right pointers in BC and DE. It admits `tforfrm.palindrome`, beating the
  10% stretch goal at 12.35% peep / 18.80% nopeep.
- The dynamic-row scheduler keeps the evolving row in DE and the byte column
  offset in BC, rematerializing the table base only after the old row dies.
  It admits `trowptr.memory_target`, improving 4.88% peep / 5.14% nopeep.
- The bounded constant-loop check proves the induction reduction and final
  predicate before preserving only the observable check call. It admits
  `treg.test_register_int`, bringing `treg` to 9.91% peep / 12.10% nopeep
  faster than main.
- The global byte countdown collapses the modulo-256 induction count and six
  stable global loads into one register expression. It admits
  `tbcgcol.global_bc_across_byte_loop`, improving both modes.
- The bounded constant-function evaluator executes side-effect-free FINAL MIR
  with PHIs under target integer semantics and emits only the proven result.
  It admits `tnarwin.sumten` plus `tregnarw.lres/lmod/lbig`; the latter app
  improves 70.01% peep / 73.32% nopeep.
- The conditional string reporter retains the name pointer in BC, selects the
  result string once, and pushes printf arguments directly. It admits
  `tstr2.report_test` and improves both modes.
- The affine byte-fill plan now supports a constant initial A and fixed byte
  step as well as a parameter base. It admits `tptrixld.fill`, improving
  21.13% peep / 25.06% nopeep.
- Frameless signed-word range and ASCII uppercase helpers admit
  `tchess.on_board` and `tchess.upiece`. The forced-accept batch tool now
  exercises the terminal cost override rather than the obsolete earlier gate.
- The fixed word-array reduction preserves adjusted pointer qualifiers:
  stable pointers stay resident in BC, while volatile pointer objects are
  reloaded once per source access. It admits `tc99apar.sum_const` and
  `sum_volatile`, improving 6.53% peep / 7.91% nopeep.
- The by-value slice reduction derives aggregate member offsets, keeps its
  cursor in BC, accumulator in DE, and endpoint in IY. It admits
  `tc89comp.slice_sum`, improving both modes.
- Constant result tables now support up to 64 entries. This admits the
  35-case `tswitch.f`, beating the stretch goal at 10.41% peep / 11.28%
  nopeep.
- The bounded constant evaluator now tracks proven nonvolatile, non-aliased
  local object state and target-width bitwise operations. It admits
  `tbug.swbr/swfc/swwc`; `tbug` improves 30.79% peep / 27.55% nopeep.
- Compact conditional pointer identity, wide constant equality, and float
  truth schedules admit `tctxflt.ad_castptr/cfk_case/truth_while`, improving
  both modes and shrinking both linked images.
- Nested conditional word selection eliminates lossless int/float/long
  round trips for `tctxflt.cond_nested` and `cond_ncast`, improving the app
  to 2.59% peep / 2.75% nopeep faster than main.
- Direct float/int truth combinators preserve short-circuit AND/OR semantics
  without runtime helpers. They admit `tctxflt.truth_and/truth_or` and further
  reduce both modes and linked sizes.
- Conditional float-to-long emission keeps the constant arm direct and invokes
  `__faf` or a validated float-returning callee only on the false arm. It
  admits `tctxflt.cond_compound/cond_callarm`.
- Conditional global-pointer, nested member, and float-comparison schedules
  complete `tctxflt` MIR coverage. The app is 3.40% peep / 3.65% nopeep
  faster than main and both linked images are smaller.
- Conditional integer selection followed by `_Bool` normalization now emits
  as a direct truth test, admitting `tbool.ternary_bool` and improving both
  modes.
- Cleared record append keeps the record pointer in callee-saved IY across
  `memset`/`strcpy`, uses frameless SP-relative parameters, and rematerializes
  the return index. It admits `tstfield.add_word` and improves both modes.
- Backward record-name search keeps the descending index in IY across
  `strcmp` and reloads the stable name parameter only at the call boundary.
  It admits `tstfield.find_word`; that app is now 1.02% peep / 2.41% nopeep
  faster than main.
- Conditional integer selection now normalizes to the declared target width,
  admitting `ts.bc`; `ts` improves 0.44% peep / 0.48% nopeep and shrinks.
- Sequential unary reports evaluate helper calls in ABI argument order and
  push each result immediately, eliminating eight spills in `ts.shbool`.
  `ts` improves 0.50% peep / 0.59% nopeep.
- Nibble append keeps the destination in HL and classifies the value in A,
  admitting `tarray.aHexNibble`; `tarray` improves 3.05% peep / 5.04% nopeep.
- Constant IEEE float checks now prove an observable failure block unreachable,
  admitting `tc89c2.test_huge_val`; `tc89c2` improves 4.55% peep / 5.33%
  nopeep and shrinks further.
- Volatile local fill plus constant wide-shift proof preserves all 160 required
  stores while eliminating dead wide frame traffic. It admits
  `tcrcfix.non_ix_shift_store_probe`; `tcrcfix` improves 36.49% peep / 38.00%
  nopeep.
- Signed div/mod check wrappers now emit only the result that reaches `ck`,
  eliminating each dead companion operation. This admits
  `tdmfuse.sdm_pair/sdm_pair_r`, improving 4.29% peep / 4.76% nopeep.
- Escaped local identity arrays preserve the published stack address but fold
  the known returned element, admitting `tnarrow.narwesc`; `tnarrow` improves
  2.11% peep / 2.45% nopeep.
- Wraparound boolean-neighbor loops now keep the current and next pointers,
  index, and wide live count in IY, DE, BC, and shadow BC. Replacing two signed
  modulo helpers per iteration admits `tptrarr.step` and improves the app
  83.76% peep / 85.30% nopeep (83.72% / 85.29% faster than main).
- Reduced sine/cosine polynomial kernels now preserve the established FMA
  chains while using one compact x2 slot; cosine tracks quadrant sign in one
  byte and applies it without a final float multiply. This admits
  `ttrig.sinf/cosf` with a small 0.05% dual-mode app gain; float runtime
  helpers remain the dominant stretch ceiling.
- Tangent rational kernels now retain their reduced argument and x2 in one
  compact frame, keep the numerator on the evaluation stack, directly test
  IEEE zero, and materialize a reciprocal only for inverted quadrants. This
  admits `ttrig.tanf`; the combined `ttrig` gain is 0.08% in both modes.
- Compact scalar schedules now cover word-width ASCII case mapping and
  short-circuit logical OR parameters, admitting `tchess.xtolower` and
  `tinline.edge_or`. A registerized fixed-count byte mismatch scan keeps the
  pointer, expected byte, endpoint, and index derivation in registers,
  admitting `tctresc.find_mismatch` and improving that app 1.61% peep /
  14.01% nopeep.
- Float-tolerance failure checks now keep the subtraction result in DE:HL,
  normalize its sign without a frame spill, and branch directly to the
  string-only failure report. This admits `tclit.check_float`, improving
  `tclit` 2.87% peep / 2.89% nopeep and moving it ahead of main.
- Global byte-check sequences now resolve direct, indexed, and member byte
  loads to exact symbols/offsets and push check arguments without virtual
  homes. This admits `tbool.check_globals`, improving `tbool` 0.64% peep /
  0.75% nopeep and removing the final homed-backend fallback.
- Variable-step byte reductions now hold the narrowed induction value in B,
  its step in C, and the word accumulator in DE. The alias-aware form proves
  the local pointer cannot escape and folds its conditional extra update.
  This admits `tpeepal.byte_loop_cache/byte_loop_alias`, improving the app
  1.38% peep / 1.83% nopeep.
- Recursive binary-tree sums now keep wide partial sums on the evaluation
  stack across child calls and inline carry-preserving long additions. Both
  native-wide and sign-extended word members are supported, admitting
  `tclit.sum_tree` and `too.bst_inorder_sum`; `tclit` improves 4.20%/5.24%
  and `too` 1.84%/2.11%.
- Recursive wide linear recurrences now share the product scheduler and select
  either the long multiply helper or an inline carry-preserving addition.
  This admits `triangle.triangle`, improving 23.56% peep / 24.54% nopeep.
- Fixed attention sample kernels now reverse-copy word arrays with IY and fill
  a word array from a no-argument producer while retaining the destination
  pointer across calls. This admits `attnc11.make_targets/generate_sample`
  and reduces the linked nopeep image by 128 bytes.
- Fixed global byte copies with constant scalar-state tails now use LDIR and
  direct byte/word stores. This admits `tchess.init_board`, replacing its
  loop and frame traffic with 13 machine instructions.
- Fixed-stride global call loops now keep the induction value in IY and derive
  each pointer argument directly from its global base and byte stride. This
  admits `attnc11.project_logits` with a dual-mode cycle reduction.
- Compact call-sum reports now preserve intermediate results on the evaluation
  stack across one- and zero-argument calls, then pass the final sum directly
  to the report call. This admits `tasmcoll.main` with a dual-mode gain.
- Dynamic global-array FMA updates now keep the selected element address in IY,
  feed the three float operands directly to `__fmaf`, store the result once,
  and return it without a reload. This admits `tfmadd.array_case`.
- Wide union-bitcast call wrappers now pass float payloads directly through
  the long call ABI and return the resulting bits without local aggregates.
  This admits `tfdf.fdf`, improving 10.37% peep / 10.52% nopeep.
- Wide shift comparisons now add a sign-extended word to a long on the
  evaluation stack, perform constant arithmetic shifts in registers, and use
  a biased inline signed threshold comparison. This admits `tctxops.sh_cmp`.
- Conditional wide additions now choose the wide parameter before evaluation,
  sign-extend the shared word operand, and perform the long addition directly
  on the evaluation stack. This admits `tctxops.ca_tern`.
- Terminal two-case wide switches now evaluate the shared word-plus-long
  expression once and compare DE:HL bytewise without spills. This admits
  `tctxops.ca_switch`.
- Bounded pointer-member appends now keep the aggregate base in IY, update the
  count in BC, and address the pointer array directly. This admits
  `too.world_add`.
- Fixed prediction-count loops now keep the row index in IY, reuse one IX
  result slot across helper calls, and update the global hit/total counters
  directly. This admits `attnc11.count_predictions`.
- The fixed prediction loop now also supports a prefix call and persistent
  boolean result byte, admitting `attnc11.check_sequence`.
- Random wide fills now retain the destination pointer in IY, derive one
  endpoint, and construct each signed Q16 result directly from the producer's
  low byte. This admits `attnc11.initialize_weight_group`.
- Fixed byte-board setup now clears the global board through a register
  endpoint loop, stores the selected byte directly, and emits the ordinary
  four-word call ABI without a frame. This admits `ttt.FindSolution`.
  dccpeep recognizes that MIR call shape and coordinates it with its existing
  whole-file MinMax packed-byte ABI rewrite; `ttt` improves slightly in both
  peep and nopeep modes rather than losing the packed-call optimization.
- Recursive fixed-frame word fills now preserve the source array as a compact
  16-byte IX frame while keeping its pointer in HL, fill value in DE, and
  count in B. Indexed publication and the recursive recurrence are emitted
  directly. This admits `tstackov.descend` and
  `tpragstk.guarded_descend`, improving them 52.03%/57.36% and
  49.11%/54.43% in peep/nopeep modes respectively while preserving the stack
  guard's observable overflow.
- Two-element pointer-member membership checks now keep the searched word in
  BC, load each aggregate pointer directly from its frameless parameter, and
  compare the two adjacent word members without PHI materialization. This
  admits `wumpus.hpit` and `wumpus.hbat` with no app-level regression.
- Wide bitcast call scheduling now handles three float parameters as well as
  two: it pushes their raw bits directly in reverse ABI order and returns the
  wide result without materializing four local unions. This admits
  `tfmaddr.fmaddr` plus `tfmaf.fmadd_/fmaf_`; both apps beat the stretch goal
  at 10.62%/10.73% and 13.17%/13.29% faster in peep/nopeep modes.
- Inline float-tolerance reports now keep the subtraction in DE:HL, normalize
  its magnitude without a local float slot, compare the fixed epsilon
  directly, and push the original values only on failure. This admits
  `tpromo.ck_f` and `tctxops.chkf`, improving those apps 3.01%/3.09% and
  3.55%/3.75% in peep/nopeep modes.
- Global record-pop loops now reload the cheap global state root at each
  boundary, keep the decremented index in BC, form the six-byte record address
  directly, and return immediately on the selected kind. Avoiding IY prevents
  file-wide allocator interference. This admits
  `tstretst.direct_return_to_call/helper_return_to_call`; `tstretst` improves
  8.64% peep / 14.67% nopeep.
- The structural call-check runner policy now allows the measured 135-byte
  textual delta caused by prelegacy callee scheduling (formerly 130);
  forced dual-mode A/B proved the same 117-instruction caller remains faster,
  preserving `tstretst.run_direct` and forward-only coverage.
- Local byte fill-and-report mains now retain only the observable byte array
  in an eight-byte IX frame, fill it with an A/HL/B loop, apply an optional
  constant patch directly, and pass its address to one or two helper/report
  pairs without induction spills. This admits `tbcreg.main` and
  `tbcregno.main`, improving them 6.54%/8.28% and 3.80%/5.25% in
  peep/nopeep modes.
- Fixed global row searches now derive one row pointer from the object member,
  keep the target word in BC, unroll the three adjacent word comparisons, and
  publish the matching value directly. This admits `wumpus.fwum`, improving
  that app 0.33% peep / 0.36% nopeep.
- Random unique-array initialization now uses a two-byte IX induction slot,
  preserves each destination address on the evaluation stack across the
  producer call, retries the fixed array until the duplicate check clears,
  then calls the validated copy helper and stores the final scalar member.
  This admits `wumpus.ginit`; together with `fwum`, `wumpus` improves
  0.45% peep / 0.49% nopeep.
- Scheduled templates must allocate fresh machine labels with `new_label()`.
  MIR CFG label IDs are function-local and can collide in the assembly file;
  the fixed-row, local-fill/report, global-record-pop, and random-init
  schedules now all use fresh labels. The full forward-only census remains
  clean after the correction.
- IEEE NaN-bit predicates now test the exponent and mantissa directly from the
  four-byte frameless parameter, avoiding a wide alias local plus shift/mask
  helpers. This admits `tfmaf.is_nan_bits`; combined with the bitcast-call
  schedules, `tfmaf` improves 13.81% peep / 14.03% nopeep and shrinks both
  linked modes by more than 6%.
- Multi-call scalar reports now evaluate one-argument callees in the
  established reverse argument order, push each result directly, then call
  the variadic reporter without virtual result slots. The profitability shape
  requires at least two calls; the one-call `tbcint` case measured slower and
  remains on its faster homed selector. This admits `tdead.main` and also
  improves the already-MIR `tmircfg.main`; both modes are no worse.
- Null-safe string assertions now increment the check counter directly, test
  the nullable string before a frameless `strcmp`, and update/report failures
  only on the cold path. This admits `tstrconv.oks`, improving both modes and
  shrinking the nopeep image by 128 bytes.
- No-argument test runners now call each validated test directly, report
  check/failure globals without slots, select the PASS/FAIL string in
  registers, and normalize the failure count for the return. This admits
  `tdmfuse.main`, improving the app slightly beyond its existing 4.3%/4.8%
  peep/nopeep gains and shrinking the nopeep image by 128 bytes.
- Float modulo normalization now calls the validated two-wide-argument helper
  directly, keeps its result on the evaluation stack across an exact float
  comparison with zero, and invokes `__faf` only for a negative result. This
  preserves NaN and negative-zero semantics while admitting `pihex.fpart`;
  both modes improve and both linked images shrink by 128 bytes.
- Fixed allocation runners now issue two validated `calloc` calls, publish the
  global state/member pointers explicitly, retain a canonical one-byte IX
  loop counter across two no-argument tests, and report success directly.
  Avoiding IY preserves file-wide dccpeep opportunities. This admits
  `tstretst.main`, completing that app's MIR coverage and improving it
  8.68% peep / 14.70% nopeep while shrinking nopeep by 256 bytes.
- String/putchar loops now preserve the original signed preliminary subscript
  check, keep a two-byte cursor in a compact IX frame across calls, and pass
  each signed character directly. This admits `tgnarly.hi_world`, improving
  `tgnarly` 0.42% peep / 0.58% nopeep.
- Fixed call reductions now hold the word sum in callee-saved IY across 35
  helper calls, keep the byte induction value in one IX slot, and evaluate
  final report calls in established reverse argument order. This admits
  `tswitch.main`, completing that app's MIR coverage and beating the stretch
  goal at 15.42% peep / 17.08% nopeep.
- Aggregate byte-fill returns now write directly into the ABI hidden result
  destination, using HL/A/B for the 40-byte sequence and appending the word
  tag in place instead of constructing and copying a 42-byte local. This
  admits `tstructv.proto_make_big`, completing that app's MIR coverage and
  improving it 3.85% peep / 6.07% nopeep.
- Last-record kind predicates now reject nonpositive counts, preserve the
  decremented index across large member-offset materialization, form the
  fixed-stride record address directly, and compare its word kind without a
  pointer local. This admits hot `fint.last_is_lit`, improving both modes and
  shrinking both linked images by 128 bytes.
- Local byte-fill validator calls now allocate only the observable byte array,
  fill it through HL/A/B, apply a direct constant corruption patch, and pass
  the frame address to the unchanged three-argument checker. This admits
  `tcpirlp.main`, improving 1.47% peep / 7.36% nopeep.
- Fixed member initialization now stores the constant name/count directly,
  unrolls three aggregate-element helper calls with frameless parameter
  reloads, and preserves each element index and pointer ABI. This admits
  `too.gallery_init`; `too` improves 1.86% peep / 2.18% nopeep.
- Volatile member sums now perform exactly one volatile pointer load per
  iteration, keep the total in BC with explicit saves across the mutating
  call, and use only one IX byte for the index. This admits
  `tvolopt.volatile_member_reload`, improving 0.55% peep / 0.67% nopeep.
- Mixed scalar reports now run setup once, evaluate eleven validated direct or
  indirect producers in established reverse argument order, and push results
  directly to the variadic report. This admits `tvolopt.main`; the app now
  improves 0.62% peep / 0.81% nopeep while retaining all volatile helpers.
- Volatile local-width kernels now perform every required volatile word
  store/load in an eight-byte IX frame, retain only the nonvolatile sum in BC,
  and execute the volatile counter's separate test and increment reads. This
  admits `tvolopt.volatile_local_widths`, completes that app's MIR coverage,
  and improves it 0.76% peep / 1.42% nopeep.
- File line loops now allocate the fixed line buffer and file slot directly,
  preserve fopen/perror/fgets/fputs/fclose argument order, and recompute the
  buffer address only at call boundaries. This admits `texfile.main`,
  completing that app's MIR coverage and reducing nopeep size by 128 bytes.
- The structurally smaller `pint.alloc_temp` schedule remains experimental
  behind `DCC_MIR_EXPERIMENTAL_SCOPED_TEMP`: the authoritative full-mode
  configuration regressed 0.26% peep / 0.16% nopeep, so production correctly
  retains the established backend.
- Wide hash-33 loops now keep the accumulator in DE:HL, preserve the original
  value on the evaluation stack across five inline shifts, retain the input
  byte in A, and track the pointer in one IX word. This admits
  `tlngfptr.hash33`, completes that app's MIR coverage, and improves it
  4.58% peep / 5.13% nopeep while shrinking both modes by 256 bytes.
- Scaled global loads now compute `base + index*scale` once through `__mulu`,
  then select a byte or little-endian word load from the shared address
  without spill slots. This admits `tinline.mem_get` with no app-level
  regression.
- Scaled global stores now reuse the same one-time address computation, keep
  the value in DE while the address sits on the evaluation stack, and store
  either one byte or a little-endian word based on the original scale. This
  admits `tinline.mem_set` with no app-level regression.
- Fixed global string copies now fold each post-incremented row address as
  `old_index*16`, call the existing `__scf` fastcall directly, and push the
  three final row pointers without spills. This admits `tfcarg2d.main`,
  improving both modes and shrinking nopeep by 128 bytes.
- Signed multiply/clamp helpers now evaluate the product once, retain it in
  BC, saturate outside ±100, and return the absolute in-range value without
  an inline temporary or PHI materialization. This admits
  `tinline.nest_scale_and_clamp` with no app-level regression.
- The bounded constant evaluator now accepts static/deferred zero-argument
  functions and tracks local address identities through direct/indirect
  stores. Existing MIR functions in `tc89size`, `tc99scpe`, `tgoto`, and
  `tgotocap` move to smaller scheduled constants with large dual-mode gains.
- A strict local-dereference induction proof handles address-taken scalar
  loops whose named locals do not yet receive MIR object IDs. This admits
  `tforinc.deref_compound_init`, improving that app 3.01% peep / 4.58%
  nopeep and shrinking both images.
- The companion fixed-index local-array proof tracks one nonescaping element
  through compound initialization, loop accumulation, and increment. This
  admits `tforinc.index_compound_init`; the combined app gain reaches
  6.39% peep / 9.29% nopeep with both images smaller.
- Compact record appends now bounds-check the global cursor, form
  `records + cursor*5` once, store byte/word/word fields directly from
  parameters, and post-increment while returning the old index. This admits
  hot `bint.emit`, improving both modes.
- Byte mismatch reporters now emit the corrected CPI loop directly: NZ exits
  immediately on mismatch, PE alone controls continuation, and HL is backed
  up before the cold offset/byte report. This admits `tcpirlp.chk`, completes
  that app's MIR coverage, and improves 2.24% peep / 13.56% nopeep.
- The existing byte arithmetic report scheduler now admits its already-coded
  unsigned path, selecting `__mulu/__modu/__divu` and zero-extending each
  byte result/argument. This admits `tmuldiv.ui8_test`, completes that app's
  MIR coverage, and improves both modes.
- Affine local fill/report schedules now support a non-one initial byte,
  a checker argument distinct from the fill count, and a patch inserted between
  reports. This admits `tctresc.main`, completes that app's MIR coverage, and
  improves 4.46% peep / 28.49% nopeep.
- Pointer word sums now keep the cursor in HL, count in B, and accumulator in
  DE, stop immediately on a zero word, and materialize the stable global
  contribution only at entry/normal exit. This admits
  `tbcgcol.global_bc_across_pointer_loop`, completes that app's MIR coverage,
  and improves 3.84% peep / 5.91% nopeep.
- Fixed static byte scans now resolve function-local static link names from
  MIR declaration metadata, preserve all six byte stores, prove the first-zero
  scan result/pointer invariants, and emit the three successful check calls
  directly. This admits `treg.test_scan`; `treg` now beats the stretch goal at
  11.48% peep / 13.81% nopeep with both images smaller.
- The shared fixed static-buffer proof now also covers ten-byte write/check
  loops, preserving every store and replacing deterministic pointer/index
  comparisons with successful checks. This admits `treg.test_write`;
  `treg` improves 21.25% peep / 23.30% nopeep.
- The affine walk variant now preserves eight `i*3` stores and emits eight
  value checks plus the end-pointer check directly. This admits
  `treg.test_walk`, taking the app to 30.81% peep / 32.95% nopeep gains.
- Deterministic boolean condition runners now emit the two proven check calls
  directly after structurally validating the if/negation/one-iteration while
  graph. This admits `tbool.check_conditions`, improving the app 1.77% peep /
  2.00% nopeep with smaller images.
- Fixed-count variadic join orchestration now keeps the destination scan and
  comma count in registers around the exact seven-argument call and final
  report. This admits `tvapinit.main`, improving the app 7.75% peep / 14.01%
  nopeep while shrinking both linked images.
- Null-terminated string mismatch checks now retain both pointers in HL/DE,
  compare directly without an index or materialized short-circuit booleans,
  and enter the report/global-update path only on failure. This admits
  `tc99varm.check_str`, improving the app 5.55% peep / 7.22% nopeep.
- Fixed CRC update runners now retain the 32-bit accumulator in DE:HL across
  eight helper calls and the call-crossing induction value in callee-saved IY,
  materializing only ABI boundary arguments. This admits
  `tcrcfix.test_crc_update_kernel`, improving the app 37.19% peep / 38.65%
  nopeep and shrinking both linked images by about 7%.
- Contiguous fixed-column record scans now retain the aggregate pointer in IX,
  the row count in BC and the sign-extended long accumulator in DE:HL, with
  the inner columns unrolled. This admits `too.board_weight`, improving the
  app 2.22% peep / 2.58% nopeep and shrinking both linked images.
- Recursive by-value aggregate chains now fold positive recursion depth into a
  single 32-bit member update before forwarding the caller's hidden return
  buffer to the terminal normalizer; a guarded recursive path preserves
  negative-depth behavior. This admits `tsretret.chain`, improving the app
  5.11% peep / 5.54% nopeep.
- Fixed call/spill runners now keep the call-crossing pointer in callee-saved
  IY, use one frame word for the accumulator, unroll the five calls and check
  both results directly. This admits `treg.test_call_spill`; cumulative `treg`
  gains reach 34.59% peep / 36.74% nopeep with smaller linked images.
- Fixed post-increment byte copies now retain the source pointer in
  callee-saved IY through the copy and repeated check calls while directly
  validating the destination. This admits `treg.test_postinc`; cumulative
  `treg` gains reach 41.23% peep / 43.74% nopeep with smaller linked images.
- Deterministic indirect-wide-shift checks now prove the constant and
  variable shift results structurally, preserve the final static long value
  and emit all eight externally visible checks directly. This admits
  `treg.test_long_indirect_shift_reg`, eliminating the app's last fallback;
  cumulative gains reach 48.00% peep / 50.38% nopeep.
- Deterministic post-update reports now prove the old/new word pairs and emit
  the two calls directly without constructing an unescaped local array and
  pointer. This admits `tpostinc.test_int_simple`, improving the app 0.38%
  peep / 0.48% nopeep while shrinking both linked images by about 3.7%.
- Fixed pointer-offset post-update reports now cover both word constants and
  runtime byte data: word pairs are proven directly, while byte pairs update
  fixed IX-relative slots after the required string copy. This admits
  `tpostinc.test_int_ptr_math` and `test_char_ptr_math`; cumulative app gains
  reach 1.07% peep / 1.36% nopeep with 7.55% / 9.09% smaller images.
- Unescaped local string-pair records now collapse to their two observable
  reports, preserving the original four string values without allocating or
  traversing the 64-byte stack aggregate. This admits `tstruct.test2`,
  improving the app 0.54% peep / 0.63% nopeep with about 3.9% smaller images.
- Triangle perimeter kernels now retain the shape pointer in IX, square signed
  members through the 16-bit multiply ABI, carry square sums and the root in
  DE:HL, and scale without frame spills. This admits `too.tri_perim`;
  cumulative app gains reach 2.29% peep / 2.65% nopeep.
- Fixed-point report orchestration now stores only the two call results,
  streams four wide variadic arguments in reverse ABI order and calls the
  mapped long-format entry directly. This admits `tshlmac.main`, improving the
  app 1.05% peep / 1.12% nopeep with 2.04% smaller images.
- Aggregate sign normalizers now write the by-value parameter directly to the
  hidden return buffer, using one eight-byte copy on the nonnegative path and
  registerized 32-bit negation on the negative path. This admits
  `tsretret.normalize`; cumulative app gains reach 7.41% peep / 8.01% nopeep.
- Aggregate-return report runners now use four fixed hidden-result buffers,
  pass the nested by-value aggregate as four direct word pushes and stream six
  long report arguments to the mapped formatter. This admits `tsretret.main`,
  eliminating the app's last fallback; cumulative gains reach 7.68% peep /
  8.42% nopeep.
- CP/M file-size helpers now allocate only the FCB, issue initialize/BDOS calls
  directly, load r0/r1 into HL and shift the record count through DE:HL.
  This admits `cpmenumd.file_size`; both runtime modes remain correctness-clean
  (the app is excluded from deterministic performance comparison).
- Constant-check scheduling now supports exact local `_Bool` array/member
  proofs and name-last checker ABIs, reusing the generic direct-call emitter.
  This admits `tbool.check_locals`; cumulative app gains reach 3.89% peep /
  4.42% nopeep with 4.55% / 5.80% smaller linked images.
- Block-scope compound-literal runners now retain only the two pair objects
  needed by observable pair checks and emit seven proven scalar checks
  directly. This admits `tclit.check_block_literals`, improving the app 5.92%
  peep / 7.26% nopeep with 4.00% / 5.19% smaller linked images.
- Extra value-literal runners now pass pair literals directly by value, retain
  one hidden pair result and emit constant long/float/scalar checks without
  materializing the surrounding literals. This admits
  `tclit.check_value_literals_extra`; cumulative app gains reach 8.53% peep /
  9.98% nopeep with 6.67% / 9.09% smaller images.
- Scaled vector-add loops now retain the scalar in IY, keep source/destination
  state in a seven-byte IX frame and carry wide multiply/add values through
  DE:HL helper boundaries. This admits `attnc11.vector_scaled_add`, improving
  both modes slightly and shrinking the nopeep image by 0.98%.
- Hall initializers now retain the hall index in IY, keep only the formatting
  buffer, use the `__scf` DE-destination/HL-source ABI and unroll the three
  exhibit calls. This admits `too.hall_init`; cumulative app gains reach
  2.38% peep / 2.74% nopeep.
- Local bitfield assignment/check runners now reuse the constant-check emitter
  after validating a strict one-block bitfield-only graph. This admits
  `tc89bit.tbone` and `tbtwo`, improving the app 10.02% peep / 11.18% nopeep
  with 13.46% / 15.09% smaller images.
- Nested compound-literal trees now use an exact eight-check proof path,
  including both recursive sum results. This admits
  `tclit.check_nested_literals`; cumulative app gains reach 17.91% peep /
  19.00% nopeep with 10.67% / 12.99% smaller images.
- Value-literal runners now retain three compact pair objects and emit all
  scalar/long/float checks directly. This admits `tclit.check_value_literals`,
  eliminating the app's last fallback; cumulative gains reach 26.29% peep /
  27.30% nopeep with 16.00% / 16.88% smaller images.
- Fixed cell checksums now retain the global aggregate base in IX and unroll
  the twelve unsigned-byte additions into DE:HL. This admits
  `too.cells_checksum`; cumulative app gains reach 3.19% peep / 3.58% nopeep.
- Deterministic string-initializer runners now emit their seven numeric reports
  directly after validating the exact one-block call graph. This admits
  `tstri2.main`, improving the app 4.11% peep / 5.30% nopeep with 4.26% /
  6.25% smaller images.
- Deterministic struct-pointer copy runners now emit two mapped mixed int/long
  reports plus completion directly. This admits `tstructp.main`, improving the
  app 4.37% peep / 4.67% nopeep with 6.38% smaller images.
- Pointer-value proof runners now emit three integer checks and one wide check
  directly. This admits `tc89ptr.tpv00`, improving the app 17.86% peep /
  14.64% nopeep with 12.96% / 16.07% smaller images.
- Escape-report orchestration now preserves the three global/interior/call
  escape side-effect functions while proving pure decode/loop values and
  streaming one mapped report. This admits `tpeepal.main`, improving the app
  8.95% peep / 9.87% nopeep with about 9.5% smaller images.
- Struct-initializer runners now emit nine deterministic integer reports
  directly. This admits `tstructi.main`, improving the app 1.93% peep /
  2.16% nopeep with 7.84% smaller images.
- Float-struct runners now emit five proven value/tolerance checks directly.
  This admits `tfloat4.test_structs`, improving the app 1.45% peep / 1.46%
  nopeep with about 3.1% smaller images.
- Type-specifier runners now emit twelve integer and four wide checks directly
  before the success report. This admits `ttypesr.main`, improving the app
  9.26% peep / 10.01% nopeep with 5.88% smaller images.
- Array-parameter qualifier runners now emit all fifteen deterministic checks
  directly before the success report. This admits `tc99apar.main`, improving
  the app 27.00% peep / 30.42% nopeep with 8.93% / 11.86% smaller images.
- Float add/sub byte-pattern runners now preserve the identity call and emit
  seven exact four-byte checks from two compact buffers. This admits
  `tc89fadd.main`, improving the app 33.40% peep / 30.17% nopeep with about
  6% smaller images.
- Float struct-array byte runners now preserve both identity calls and emit six
  integer plus three IEEE byte-pattern checks directly. This admits
  `tc89fs.main`, improving the app 11.74% peep / 12.12% nopeep with 4.17% /
  6.00% smaller images.
- Float/long conversion runners now preserve both identity calls and emit
  fourteen proven boolean checks directly. This admits `tc89flng.main`,
  improving the app 75.85% peep / 76.18% nopeep with 18.52% / 20.00%
  smaller images.
- Float initializer runners now emit fourteen exact IEEE equality checks
  directly. This admits `tc89fini.main`, improving the app 21.08% peep /
  21.11% nopeep with 11.32% smaller images.
- Large deterministic comparison runners now derive all labels and expected
  values from MIR and emit 46 checker calls directly. This admits
  `tc89fcmp.main`, improving the app 56.32% peep / 55.62% nopeep with 26.09%
  smaller images.
- Designated bitfield-initializer runners now preserve all eighteen checker
  calls, use a two-byte failure slot and invoke the mapped hex hook for the
  final report. This admits `tbfinit.main`, improving the app 8.86% peep /
  10.04% nopeep with 14.29% / 15.52% smaller images.
- Prefix-update runners now derive 25 integer and one wide expected value from
  MIR and preserve every PASS-producing checker call. This admits
  `tpreinc.main`, improving both modes while shrinking images by 12.50% /
  14.04%.
- Wide bit-operation runners now derive all 22 expected values and labels from
  MIR and emit the wide checker ABI directly. This admits `tbits32.main`,
  improving the app 8.54% peep / 8.96% nopeep with 7.69% / 9.43% smaller
  images.
- Large float storage/copy runners now derive 25 four-byte expectations and
  labels from MIR, preserve the start/success reports and use one buffer.
  This admits `tc89flta.main`, improving the app 22.08% peep / 21.74% nopeep
  with smaller images.
- Pointer-difference orchestration now preserves both scaling helper calls and
  complete failure behavior in a compact 30-byte frame. This admits
  `tptrdiff.main`, improving the app 1.02% peep / 1.77% nopeep and shrinking
  the nopeep image.
- Constant-expression static initializer/bound runners now emit five proven
  wide checks directly. This admits
  `tsyntax.test_constexpr_static_init_and_bounds`, improving the app 3.69%
  peep / 4.36% nopeep and shrinking both images.
- Packed mantissa/exponent/sign decoders now extract the byte directly, shift
  in HL and return a sign-extended DE:HL value without a frame. This admits
  `tpeepal.decode` and cuts the app images to 11.54% / 13.21% below baseline.
- Closed local sort/search examples now emit their proven found-value/index
  report directly. This admits `texsort.main`, improving the app 66.31% peep /
  67.54% nopeep and shrinking both images.
- Aggregate leaf constructors now write the hidden return buffer directly,
  reuse one long multiply result and fill fixed arrays with compact register
  loops. This admits `tptrlhs.make_leaf`, improving the app 4.25% peep /
  4.15% nopeep while shrinking both images.
- Fixed six-dimensional affine fills now flatten six binary induction
  variables to one byte counter and one contiguous destination cursor while
  preserving every scalar helper call and its argument order. This admits
  `tarray6.fill_c/fill_i/fill_l`, improving the app 56.95% peep / 57.05%
  nopeep and shrinking both images by more than 21%.
- Fixed embedding builders now flatten the proven 8x16 traversal, derive
  destination and position addresses from one byte counter, and retain only
  the advancing token-weight cursor while preserving every clamp call. This
  admits `attnc11.build_embeddings`, saving 129,500 peep / 171,402 nopeep
  cycles and shrinking both images.
- Fixed forward-attention orchestration now keeps the query row in IY,
  compacts score/key cursors to a five-byte frame, emits the fixed transpose
  calls directly, and registerizes the residual clamp loop. This admits
  `attnc11.forward_attention`, saving another 94,248 peep / 691,740 nopeep
  cycles beyond the embedding batch.
- Four-byte representation checkers now compare parameter or pointed storage
  directly from IX, preserve both six- and ten-argument failure reports, and
  increment the resolved global failure counter without spill slots. This
  admits `tc89fadd.chk`, `tc89fcnv.chkf`, `tc89fptr.chk`, and `tc89fs.ckbf`;
  app gains range from 5.09% to 34.47% peep and 11.16% to 40.78% nopeep.
- IEEE infinity/NaN checkers now inspect the parameter bytes directly,
  preserving checks/failures accounting and the exact name-only report. This
  admits `tatof.chk_inf/chk_nan`, saving 5,326 peep / 8,226 nopeep cycles and
  shrinking the images by 2.67% / 6.41%.
- Flagged fixed-record appenders now retain the destination record in IY,
  update the count in place, index source bytes directly and call the
  classifier only on the special-capture path. This admits hot
  `tchess.add_flag_move`, saving 1.77M peep / 2.80M nopeep cycles and shrinking
  both images.
- Fixed record wildcard predicates now compare two leading bytes through BC/DE
  cursors and accept either an equal final byte or a right-side zero without a
  frame. This admits `tchess.same_move`, saving another 47,992 peep / 71,670
  nopeep cycles and one 128-byte sector in each mode.
- Dense integer switches returning string pointers now use a bounds check and
  address table, including a null default. This admits `tchess.bk_text_at`,
  removes 35 MIR blocks and saves another nopeep image sector.
- Final MIR streams now remove adjacent `exx/exx` pairs before emission while
  preserving their pre-cleanup metrics for profitability decisions. This
  improves 38 already-active apps in both modes without widening coverage;
  `tasinfsp.main` remains correctly rejected after forced A/B showed it still
  loses 0.79% peep / 0.78% nopeep and linked size.
- Terminal final-cost candidates now receive the existing constant-prepack /
  direct-push stack-argument retry only for three measured structural strata.
  This admits `taninit.chk_str`, `tc89c2.test_strtod`, and
  `tvla.vla_ptr2d_deref_chain_contexts`; all three improve both modes and the
  latter two shrink both images.
- The broad terminal-stack probe remains opt-in. It was rejected after
  `bint`, `forint`, and `pint` introduced small cycle/sector regressions and
  after `tasinfsp`, `tctype`, `tmulpow2`, `tstdlib`, and `tarray` failed one
  or more dual-mode guardrails.
- Two profiled `all`-feature strata admit `too.test_gallery` and
  `too.test_size_inference`, improving the app 2.21% peep / 2.46% nopeep and
  shrinking both images. One profiled `phi-slot` stratum admits
  `tcrcfix.argv_probe`; cumulative app gains remain 37.10% / 38.55%.
- Broad `all` and `phi-slot` terminal probes remain opt-in. The former
  displaced three active functions and crossed 23 guardrails; the latter
  displaced three and crossed 66. `tctxops`, `too.test_aggregates`, and
  `tqsort` were individually rejected for sector or cycle losses.
- The exact local constant-byte store proof now covers one measured four-block
  aggregate-runner stratum while retaining its original three-block behavior
  byte-for-byte. This admits `too.test_aggregates`; cumulative `too` gains are
  2.13% peep / 2.35% nopeep with smaller images.
- Variadic string joins now retain an explicit argument cursor in an eight-byte
  IX frame and call the `__scf`/`__slf` fastcall ABIs directly. This admits
  `tvapinit.join`, beating the stretch goal at 11.53% peep / 18.30% nopeep
  while shrinking both images.
- Fixed five-byte record-sort tests now fill through an IY cursor, call
  `_qsort` directly and validate adjacent records through `__cmpf`. This
  admits `tqsort.t_qsort_r5`, improving both modes and shrinking each image
  by one sector.
- Local bitset runners now preserve established reverse call-argument
  evaluation, keep print results on the stack, and use canonical IX-relative
  address forms. This admits `tidxasgn.main`, matching peep cycles and
  improving nopeep by 121 cycles with no size regression.
- Deterministic postfix-decrement runners now replace three proven loops with
  their observable checker calls and retain the global failure/success path.
  This admits `tpostut.main`, beating the stretch goal at 10.28% peep /
  12.88% nopeep while shrinking both images.
- Fixed sieve builders now clear through an HL cursor and mark multiples with
  four compile-time prime-stride loops. This admits `tnestfor.build_sieve`,
  improving **20.58% peep / 23.14% nopeep** while shrinking both images.
- Fixed nested-wrapper initializers now compute the shared signed base product
  once, fill aggregate fields through register cursors, and install derived
  pointers without IY. This admits the identical `tptrcnd.init_wrapper` and
  `tptrrhs.init_wrapper` graphs, improving those apps by **87.96%/87.62%**
  and **79.31%/75.44%** respectively with no image regression.
- Static-inline proof helpers are now shared across MIR selectors. A fixed
  inline-fold runner proves its four-argument byte/word setter AST before
  replacing call-shaped MIR with direct global stores, admitting
  `tinline.inline_fold_check` and improving the app **2.18%/2.44%** while
  shrinking both images.
- Fixed K&R call-check runners now retain ordinary and wide ABI argument
  ordering, keep failure state in a six-byte IX frame, and compare DE:HL
  results directly. This admits `tkandr.main`, improving **1.29%/1.83%**
  with no image regression.
- Deterministic local-initializer checks now prove every initialized offset,
  comparison family, checker call and returned load before eliding private
  aggregate storage. This admits `tc89ini2.sum_local`, improving
  **23.07%/26.13%** and shrinking both images by more than 12%.
- The existing register-framed float polynomial family now covers atan range
  reduction and fixed Taylor sine loops. Four-deep `__fmaf` Horner chaining
  and a one-byte alternating-sign state admit `ttrig.atanf` and
  `ttrig.xsinf`, improving the app **1.19%/1.22%** and shrinking both images.
- Q16.16 multiplies now decompose into signed, unsigned, and mixed 16x16
  register products, carrying the 32-bit accumulator on the machine stack
  instead of in a 16-byte frame. This admits `tshlmac.fp_mul`, beating the
  stretch goal at **19.91%/19.95%** while shrinking both images by 6.12%.
- Fixed index-call runners now initialize int, long, and record arrays in one
  compact IX frame and compare narrow/wide call results without spill slots.
  This admits `tmulpow2.main`, improving **5.75%/7.30%** and shrinking both
  images by more than 4%.
- Task-array checks now pack fixed records into a 22-byte IX frame, preserve
  both search/count helper calls, and validate the returned record in place.
  This admits `tdecl.auto_mixed_struct_array_init`, improving **0.73%/1.50%**
  while shrinking both images.
- Literal-check runners now recursively evaluate pure MIR constant
  unary/binary DAGs and replay only their observable checker calls. This admits
  `tunaryp.main`, improving both modes and shrinking images by
  **7.94%/9.38%** without a local frame.
- The same evaluator now normalizes 8/16/32-bit target constants and supports
  start/failure reports. This admits `ts32.main`, beating the stretch goal at
  **12.34%/14.25%** while shrinking images by **17.91%/20.00%**.
- A compact register schedule for `pint.alloc_temp` was rejected despite 50
  fewer instructions and one smaller sector because program placement still
  regressed **0.26%/0.16%**; no padding or baseline movement was retained.
- A direct `too.expected_area` dispatcher was rejected after repeated-call
  tests exposed incorrect signed divmod cache/register handling; it is absent.
- A binary-search replacement for `too.isqrt_l` was rejected after focused
  boundary tests exposed incorrect small-input results; it is absent.
- A candidate `pint.add_sym` schedule was rejected after it remained 0.26%
  peep / 0.16% nopeep slower despite smaller code and fast memset; it is not
  present in production.
- Do not force statically small fallbacks. `tcrcfix.non_ix_shift_store_probe`
  is 393 text bytes and 97 instructions smaller than captured output but
  regresses 11.49% peep and 5.48% nopeep dynamically.
- The other statically smaller candidates are also false wins:
  `trowinv.main` (+7.48%/+5.14%), `tautolcs.lcs` (+29.92%/+22.09%),
  `tfreopen.main` (+4.65%/+2.73%), and `t2darr.main`
  (+28.46%/+31.34%).
- Current next priority: repeated causes in the 84 final-cost fallbacks,
  followed by calibrated replacement of `register-v69`. Maintain zero
  correctness, performance, and coverage regressions.

The sections below are the chronological history of earlier branches and
experiments. Where they conflict with this checkpoint, this checkpoint wins.

## Historical 2026-08-08 policy pivot

8 days moved ordinary coverage only 43.93% -> 45.11%. The user directed a
bold pivot: **the goal is 100% MIR coverage, reached fast; performance is
not a Phase 1 gate** (relax the historical strict zero-regression rule,
since every remaining gate had accreted an individually-proven,
zero-regression bar that does not scale to 100%). Correctness is still
completely non-negotiable at every step - only the *performance* bar is
relaxed, deliberately, and every accepted regression is tracked via
`-UpdatePerfBaseline` with full documentation, never hidden. A dedicated
Phase 2 (post-100%-coverage performance recovery via `dccprof` dynamic
profiling) follows once coverage is at/near 100%. Full plan, evidence, and
rationale: session workspace `plan.md` (not tracked in git) plus each
`## Item T43x` entry in `mir-text-size-plan.md` going forward. Large agent
fleets are out - this phase is direct, foreground, tool-driven bulk
acceptance instead of one-by-one investigation.

## Goal and current state

The goal is **100% MIR-required coverage** (not an intermediate percentage
target), then a dedicated performance-recovery phase to bring aggregate
peep-mode cycles back to at/below the pre-MIR legacy baseline. Mixed-mode
transactional fallback remains in place throughout Phase 1.

- Working branch: `copilot/regional-home-af23` (local only; do not push)
- Published baseline: `45cf3f0`
- Published ordinary coverage: **890/2026 (43.93%)**
- Published stack-check coverage: **912/2128 (42.86%)**
- Current ordinary coverage: **2067/2067 (100.00%)**
- Current stack-check coverage: **2183/2183 (100.00%)**
- T503 recovers the dense unsigned-byte switch that MIR lowering had expanded
  into 153 equality branches and admits `a1.emulate`: **+1/+1**, zero
  removals.
- Local T504 extends regional homes to mixed-width/object-backed segments and
  admits `pint.factor_call_or_var` plus `pint.scan_number`: **+2/+2**, zero
  removals.
- **T504 closes the two smaller Pint holdouts.** `scan_number` is now smaller
  than captured output; `factor_call_or_var` fits the measured stack-check
  nopeep TPA boundary by one byte under a structural 3-32-block, <=24-call,
  117%/122%, <=6000-byte true-final gate.
- **T505 completes standard-corpus MIR coverage.** `pint.run` has a 43-case
  contiguous unsigned-byte dispatch, not a regional-pressure problem. Compact
  table recovery plus direct condition, postincrement-store, store/load-chain,
  and small self-store-add forms reduce it to 21720/1933 versus 23277/2046
  captured. Both censuses are 100% with zero removals; the full extended gate
  is clean.
- **Phase 2 is now active.** T506 has recovered Pint's tracked
  +2.60% peep regression and moved it beyond both pre-T505 and older published
  baselines. T507 removes `a1.emulate`'s dead recovered-switch slot stores,
  recovering 2.50% peep / 2.23% nopeep; the remaining `a1` gap is about
  1.8% peep and 0.15% nopeep versus the immediate pre-T503 baseline. T508
  fuses two retained static-inline stack-push helper calls transactionally
  inside the spilled selector, reducing the remaining gap to 1.42% peep
  while making nopeep 0.44% faster than pre-T503. T509 permits planned stack
  handoffs across balanced scalar calls, reducing the remaining gap to 0.43%
  peep while making nopeep 0.68% faster than pre-T503. T510 makes deferred
  inline-body ownership follow the selected output and fuses the last hot byte
  push, leaving only 0.33% peep while making nopeep 0.81% faster.
  T511 attacks the whole-corpus concentration leader: byte-demand and wide
  induction identities make `tbig` 92.5% faster peep, reduce total positive
  pre-MIR peep debt by 76.9%, and materially improve 13 additional apps.
  T512-T528 recover word dispatch, narrow-origin wide arithmetic, byte
  verification, word scans, large-CFG address/induction identities, and
  interpreter inline-stack/typed-memory helpers, byte minimax, and modular
  arithmetic, chess, fixed-array, fixed-point matrix, and the remaining
  interpreter and long-tail loop kernels. Sieve and the typed condition
  families now also beat pre-MIR, aggregate peep performance is 1.378B
  cycles ahead of pre-MIR, and positive per-app peep debt is 16.3M
  (-99.9% cumulatively). Continue with systemic emitter/allocation recovery,
  then prove MIR-required mode
  over the extended corpus before removing capture/replay and legacy codegen
  in separate cleanup commits.
- **Key finding this segment: the mega-experiment's central premise -
  that "cost-only" fallback reasons are always pure cost proxies with no
  remaining semantic risk - was wrong for the majority of reasons
  tested.** There is no shortcut to 100% coverage via blind bulk
  relaxation; each of the 16 confirmed-unsafe reasons needs the same
  per-shape forced-correctness investigation (`mir_is_profiled_*`
  predicates, forced-accept A/B) that produced the safe subsets already
  in production. Real, safe progress was still made (+108/+110,
  crossing 50%), but the "fast path to 100%" the pivot hoped for does not
  exist without this per-reason correctness work.
- **T435 changes the architecture priority:** the generic
  `cfg-backedge` bucket itself is now exhausted and no longer appears in
  either census. The known-open interpreter/VLA loop failures remain, but
  under their actual current reasons (`selector`, `boolean-phi-cost`,
  `unary-not-cost`, `dynamic-index-base-cost`,
  `binary-load-pair-cost`, and `dead-store-forwarding-cost`). Continue
  with those higher-yield reason populations rather than reopening a
  nonexistent generic backedge bucket.
- **T436 materially reduces the second-largest unsafe reason:** a blind
  post-T435 force still failed 12 apps, and forced-function bisection
  identified 10 individually wrong backedge candidates. A 63-function
  semantically eligible slice exposed two additional combination-only
  issues. Broadly enabling the existing strict-PHI retry fixed `tchess`
  but miscompiled `tatof.chk_inf` and `tctxflt.truth_or`, so that
  experiment was reverted and label-only fallthrough remains excluded.
  `pint` also proved the full-I/O linked image can run out of CP/M memory
  when one MIR function grows by more than 2 KiB, motivating the explicit
  growth ceiling. These are correctness/resource boundaries, not
  performance policy.
- **T437 crosses 60% in one architecture batch.** Full-reason forcing
  still failed nine ordinary apps plus extended test 00158. Per-function
  bisection isolated five individually unsafe shapes:
  `tm1mu.mulmod` (wide specialized arithmetic semantics), `ts32.main`
  (oversized/call-heavy), `tbug.swfc` and `tsvbuf2.expect_prefix`
  (backedges), and `tvla.vla_goto_out` (VLA+backedge). Excluding those
  semantic/resource strata yields a 149-function ordinary cohort that is
  clean even in combination. Continue by attacking the remaining
  backedge/VLA/wide/large text-size strata separately, not by widening
  this proven boundary.
- **T438 crosses 67% by fixing, then opening, the wide stratum.**
  `tm1mu.mulmod` now uses the same overflow-safe `__m1mu` ABI as legacy.
  The bounded acyclic wide cohort adds 137 more ordinary functions after
  that single-function fix. The failed `pihex.powermod16` experiment is a
  standing rule: fusion may only add uses already represented in MIR
  liveness, and skipping an instruction must consume any planned real-
  stack handoff it would have consumed.
- **T439 establishes true-final-reason discipline.** New coverage gates
  that intend to classify the terminal fallback reason must run after
  every retry, immediately before `emitted = 0`. The older policy block
  is earlier than boolean simplification, block CSE, address
  rematerialization, and phi-slot retries; accepting there can select a
  transient candidate whose final classified reason is different.
- **T440 applies that discipline to dynamic index bases.** The landed
  cohort runs at the actual final decision point and combines semantic
  guards (acyclic/no label-PHI/VLA/inline/pointer-array), a 2 KiB growth
  cap, and a 5,000-byte absolute CP/M resource cap. Remaining candidates
  are primarily backedge/wide or later-retry strata.
- **T441 confirms the shared bounded-acyclic boundary generalizes.** The
  same semantic/resource predicate safely admits a second reason without
  duplicating policy formulas; keep using a shared helper when later
  reasons need the identical boundary.
- **T442 produces the largest shared-predicate reuse so far (+35).** It
  also re-confirms that blind reason forcing can fail solely because it
  intercepts a transient pre-retry candidate; terminal-reason testing is
  the authoritative production model.
- **T443 adds a reason-specific frame-pressure stratum.** The common
  bounded-acyclic predicate remains the base; `wide-store-cost` further
  requires a call-containing measured shape because its only call-free
  member triggers a stack-check resource failure in pint.
- **T444 validates the batch-of-10 operating model directly.** Four
  independently measured small cohorts were combined into one 27-function
  commit and one pre-publication full extended gate. The shared predicate
  now covers seven final reasons with one source of truth.
- **T445 preserves that cadence while enforcing no-removal discipline.**
  The first five-reason experiment gained 22 but lost two existing MIR
  functions; the final four-reason batch gains 15 with zero removals and
  excludes both identified bad strata.
- **T446 closes a real representation bug, not a gate symptom.** MIR now
  records when a dereference consumed a pointer-array dimension, so
  deferred metadata repair uses element stride rather than restoring the
  whole-array stride. `(*ip)[i]`, `(*cp)[i]`, `(*pp)[i]`, and
  `(*lp)[i]` now use 2/1/2/35 rather than 8/4/6/105.
- **T447 eliminates the selector-less bucket.** Every former
  selector rejection was the same unresolved `#itmpN` store. Synthetic
  inline temps remain excluded from SSA promotion but are now published
  as frame memory. One nested-lifetime collision is explicitly gated;
  every other function reaches a real selector/cost decision.
- **T448 fixes the first shared loop-state root cause.** Slot planning and
  emission now share one `mir_value_requires_phi_slot()` predicate, so a
  value needed by an edge copy cannot be optimized into a branch-only
  handoff. This removes four prior boolean-loop failure apps and makes
  binary-load loop forcing clean outside the known pint resource case.
- **T449 nearly eliminates the historical dominant text-size bucket.**
  After T448, full reason forcing failed only transient pint resource
  selection and oversized `ts32.main`; true-final ordering plus a
  10,000-byte ceiling safely admits every other terminal candidate.
- **T450 crosses 80% and fixes nested inline temp allocation.** The live
  temp mask had been restored after AST cloning but before lowering, so
  nested calls reused an outer slot. Scope now spans lowering, causing
  nested expansion to choose `#itmp2` instead of overwriting `#itmp1`.
- **T451 crosses 83% with reason-specific loop admission.** The earlier
  acyclic dynamic cohort and this scalar loop cohort together remove 57
  functions while preserving wide/backedge and label-PHI failure strata.
- **T452 demonstrates why extended coverage remains mandatory.** The
  standard 314 apps passed the proposed phi-fallthrough loop stratum, but
  extended `00183` failed. Only the independently clean boolean/unary
  strata landed.
- **T453 closes the safe wide index/store strata.** Dynamic wide
  candidates remain bounded at 10 KiB with no label/VLA/pointer/inline
  shape; wide stores extend to 10 KiB only when acyclic and
  call-containing.
- **T454 eliminates terminal wide-constant fallback.** It also proves the
  small VLA dynamic-index functions are correctness-clean after PHI and
  pointer metadata repair.
- **T455 eliminates terminal PHI-fallthrough fallback.** Consecutive labels
  are aliases, not CFG edges; the real predecessor owns each copy, while
  NOP-only arms defer to the branch entry copy. The same batch fixes typed
  signedness/narrowing aliases and fused constant operands exposed by broad
  PHI admission.
- **T456 opens the repaired bounded boolean-PHI stratum.** The broad
  66-function reason reaches 91% but still fails eight apps; exact-function
  isolation identified direct failures in `ShowBinaryData`, `MinMax`,
  `factor_call_or_var`, `run_at`, and `parse_source`, plus multi-function
  interpreter/resource interactions. The call-containing <=20-block cohort
  is independently full-extended clean.
- **T457 opens the repaired small unary-not stratum.** All remaining terminal
  unary candidates at <=6 blocks/6 KiB pass together, including parser lookup,
  I/O, chess, and type-test functions.
- **T458 nearly eliminates dead-local suffix fallback.** Twenty-three wide,
  label-PHI, loop, float, and pointer candidates pass together; only one
  45-block/one-call shape remains.
- **T459 eliminates terminal absolute-index fallback.** The two failures from
  blind reason forcing were later-retry candidates with different true final
  reasons, confirming final-reason ordering is again load-bearing.
- **T460 nearly eliminates wide-store fallback.** Eighteen loop and large
  acyclic candidates pass together; two precisely isolated shapes remain.
- **T461 eliminates terminal indirect-store-address fallback.** Eleven
  attention, allocator, loop, and narrowing candidates pass together.
- **T462 eliminates terminal planned-index-base fallback.** Eleven true-final
  candidates pass without the nonlocal label/layout perturbations caused by
  blind transient interception.
- **T463 eliminates terminal planned-stack fallback.** Seven true-final
  candidates pass while `tlimits` retains its actual boolean-PHI reason.
- **T464 consolidates two tiny residuals.** Constant-home is reduced to one
  direct call-free failure; dead-store forwarding is fully eliminated.
- **T465 closes instruction-count and halves binary-load-pair.** True-final
  gating avoids the two selector removals seen under blind forcing.
- **T466 reduces block-CSE to two direct failures.** Four large single-block
  functions pass their true CSE candidates in both modes.
- **T467 separates deterministic and drifting sinks.** FINAL-sink unary
  functions are safe; VERIFY/DEFERRED static bodies can select different
  final reasons and require an architectural determinism fix.
- **T468 crosses 95% ordinary.** Seven FINAL boolean functions pass after
  excluding two direct failures, all four-call candidates, and high-block
  non-wide pairwise interactions.
- **T469 isolates deterministic FINAL dynamic indexes.** Nine functions pass;
  three direct standard failures and extended `00182` remain excluded.
- **T470 resolves most static unary residue.** Guarded literal-final
  diagnostics expose mode-specific failures correctly; 19 static bodies pass
  both peep modes together.
- **T471 halves the dynamic-index residue.** Seven static/FINAL candidates
  pass both modes; seven individually confirmed failures remain.
- **T472 crosses 98% stack-check coverage.** Individual full-mode
  classification plus a three-function/8-KiB module budget admits 26 boolean
  functions without interpreter layout exhaustion.
- **T473 crosses 98% ordinary coverage.** A one-per-module cap safely adds one
  larger COBOL unary function; admitting all three peers fails.
- **T474 safely expands ordinary module budgets.** A ten-function ceiling
  remains bounded by 8 KiB, adding four `forint` functions without reopening
  `adaint`, `cint`, or COBOL limits.
- **T475 fixes helper-call allocation.** Values live across `/` or `%` can no
  longer remain in caller-clobbered HL/DE/BC.
- **T476 eliminates inline-temp-overlap fallback.** Sequential slot reuse is
  distinguished from genuine nested identity overwrite.
- **T477 eliminates inline-substitution fallback.** dccpeep can no longer
  borrow IY in a callee when MIR retains a caller value there.
- **T478 fixes formatted-I/O EXTRN ownership.** Calls sharing one source
  symbol can resolve to distinct assembler entry points; direct-call EXTRNs
  are now deduplicated by that resolved name, admitting `tpfauto.main`.
- **T479 eliminates terminal text-size fallback.** The T455 typed-alias fixes
  already repaired `ts32.main`; the final non-speculative sink now admits its
  oversized shift matrix after every retry has completed.
- **T480 completes empty-arm PHI ownership.** An explicit branch can target an
  earlier label in a NOP/label-only alias chain; matching only the final alias
  emitted the edge copy twice and overwrote the selected value. The complete
  empty span now identifies the real owner, admitting `tabsidm.main`,
  `cobint.parse_data_line`, and `forint.decode_stmts`.
- **T481 reopens two repaired dynamic-index strata.** The three-call
  label-PHI function and a bounded non-wide allocator loop both pass full
  mode after T480; the five wide/interpreter/resource failures remain gated.
- **T484 publishes spilled virtual-IY ownership and emits both conditional
  edge copies.** This admits `cobint.parse_source`; `pint.factor_call_or_var`
  is semantically fixed in peep mode but remains gated by nopeep TPA pressure.
- **T485 fixes paired div/mod storage.** Both quotient and remainder now own
  simultaneous concrete slots, preventing a quotient restore from
  invalidating a slotless remainder marker; `fint.run_at` is admitted.
- **T486 makes MinMax transforms transactional and word-correct.** Packed
  frame/call rewrites commit only when both recursive and external call sites
  match, and the shared epilogue restores H for the declared byte return.
- **T487 closes the bounded four-call FINAL stratum.** The two remaining
  <=36-block/10-KiB functions pass both modes after the PHI and ownership
  repairs; larger and low-call resource failures remain gated.
- **T488 admits the repaired bounded float loop.** `xsinf` is semantically
  correct after the branch fixes and passes both modes with a tracked
  544-byte stack reserve instead of disabling stack checking.
- **T489 crosses 99% in both censuses.** The high-boolean-simplification
  parser is admitted with a tracked 768-byte stack reserve; this is explicit
  Phase-1 resource debt for the post-100% frame-recovery campaign.
- **T490 reduces dynamic-index residue to four.** The deterministic
  13-block/19-call FINAL float driver passes both modes with T488's tracked
  reserve and no longer needs its historical exclusion.
- **T491 eliminates terminal block-CSE fallback.** Aggregate call arguments
  now use correctly directed `LDIR`, and scalar results survive odd-byte stack
  cleanup in forced concrete slots. `tstructv.main` shrinks by about 8 KiB
  and passes both modes.
- **T492 repairs mixed unsigned division.** MIR's inline `/ power-of-two`
  strength reduction corrupted the following runtime quotient in a mixed
  sequence despite passing alone. Division now stays on the normal helper
  path while modulo masking remains; `tmodp2.main` is admitted.
- **T493 adds bounded hybrid retained-home emission.** The existing homed CFG
  backend now supports the required wide homes, frame accesses, arithmetic,
  indexing, and spills transactionally for final boolean residue. It admits
  `pint.next`, `scan_string`, and `skip_brace_comment`.
- **T494 eliminates terminal wide-store fallback.** The hybrid emitter's
  general wide-helper path now handles the zero-spill, three-block division
  CFG and emits `pint.calc_code_limit` smaller than legacy.
- **T495 halves unary-not residue.** The hybrid retry admits only acyclic,
  non-inline, <=40-block/7-call unary candidates, selecting
  `cobint.compile_add` and `compile_subtract` while preserving
  `fint.top_level`.
- **T496 eliminates terminal binary-load-pair fallback.** Alias-safe
  block-local reuse retains `code` and `cp` across the three `code[cp]` field
  stores in `pint.emit`; the retry is isolated to its two-block/one-call
  reason so established one-block selectors remain unchanged.
- **T497 fixes shadowed object-merge types.** Deferred alias repair retargeted
  a merge from an outer `int` to a C99 for-init `long` without updating its
  type, truncating the loop PHI and hanging `tforsco.main`.
- **T498 opens the measured larger hybrid stratum.** Hybrid emission supports
  up to seven spills and 92-block call/PHI CFGs with a 25% Phase-1 size bound,
  admitting the final COB performer and Fortran parser.
- **T499 adds real call-boundary live-range splitting.** Persistent regional
  segments reuse caller-saved homes and spill slots between calls; stable
  parameters/addresses rematerialize at use. `pint.subprog` now fits the
  stack-check TPA and passes both modes. Its two-block peer remains fallback
  because selecting both perturbs Pint's optimized linked layout.
- **T503 resolves the giant-switch outlier without widening regional homes.**
  `a1.emulate` has low live pressure and arm-local state; T499's 458 tiny
  regions produced only one-use spill segments and grew the candidate.
  Recovering the 153-case unsigned-byte dispatch as a PHI-free 256-entry jump
  table cuts the final candidate to 31,946 bytes / 2,540 instructions and
  admits it under a measured FINAL-void structural gate.
- **T432 (this segment): n-gram re-mining re-confirms text-size/
  boolean-phi-cost exhaustion, no code change.** Re-ran the T385 n-gram
  mining tool against the current, much more mature populations
  (`text-size` 304 candidates, `boolean-phi-cost` 158) to check whether
  all the architecture work since T385 (T393-T431) had shifted anything
  into a newly mineable shape. Result: no. `boolean-phi-cost`'s only
  idiom (materialize-then-retest boolean, ~866 occurrences) is still
  dominated by the same `a1.op_bcd_math`/`a1.op_math` pair T385 already
  forced-accept-tested and correctly rejected. `text-size`'s top n-grams
  are exclusively generic ABI-mandated calling-convention boilerplate
  (multi-arg call cleanup, frame setup, word-from-frame reloads) spread
  across ~108 distinct apps with no >=10-function cohort left uncovered
  by an existing (already-exhausted) selector concept - re-confirming
  T385's original "genuinely heterogeneous, no dominant fixable idiom"
  finding rather than contradicting it. Both gate-margin mining
  (T394-T431, 9+ buckets) and n-gram idiom mining are now confirmed
  exhausted against the current corpus. Full details: `## Item T432` in
  `mir-text-size-plan.md`. Coverage unchanged: 914/2026, 936/2128.
- **Post-T429 re-rank (this segment)**: a fresh gate-margin re-rank across
  every remaining bucket confirmed gate-margin mining is now exhausted
  project-wide, again. `fint.top_level` (`unary-not-cost`, 26-block CFG,
  better static bytes/instructions than legacy but rejected by the
  `mir_cfg_block_count() > 18` defensive cap) looked like a promising
  near-miss on first read, but a direct forced-accept full-mode A/B
  **regressed real performance** (peep bytes 28288->28544, peep cycles
  +198, nopeep cycles +1224) despite the favorable static metrics -
  confirming the block-count cap is load-bearing, not overly
  conservative. Also re-confirmed the `mir-gate-margins.py` script sorts
  by **instruction count**, not bytes - its top `text-size` candidates
  (`tvla.vla_nested`, `tvla.vla_long_bound`, etc.) look like near-misses
  on instructions but are actually **worse on bytes** (e.g.
  `vla_nested`: 264 fewer instructions but 3028 vs 2923 bytes, +105/+3.6%
  worse) - not real leads. No code change; recorded in
  `mir-dead-ends.tsv`. Coverage unchanged: 914/2026, 936/2128.
- **Stream J (this segment): T430/T431, real block-cse-cost architecture
  landed, +0/+0 coverage.** Dispatched to attempt T407's identified
  architectural blocker for `block-cse-cost` (a genuine
  retained/rematerialized base-address planner). Mid-flight, discovered
  and relayed to the agent that an earlier stream (Stream G) had already
  landed a closely related, more advanced attempt as `## Item T426`
  (allocator-aware kill-tracked block VN - proved the bucket isn't
  blocked on missing equality/kill reasoning, but on the spilled
  backend's habit of manufacturing new frame/stack-slot traffic for a
  reused value, which `dccpeep` cannot fold as well as its existing
  direct-reload-from-global pattern; also found the legacy same-block
  CSE retry poisons spill pressure before a later VN pass can help).
  Redirected Stream J to build on T426's precise diagnosis rather than
  re-deriving it. Result, independently re-verified from scratch in the
  main repo against a freshly-regenerated true baseline (not any cached
  snapshot): **ordinary 914/2026 -> 914/2026, stack-check 936/2128 ->
  936/2128, exactly +0/+0**, zero selector changes, zero selected-output
  hash changes across both censuses, zero apps requiring runtime
  validation, forced-correctness 7/7, full extended gate 314/323/0
  failed. Landed two real commits: **T430** implements the missing
  physical-home distinction for block-CSE retries - address
  rematerialization no longer stops at a single root `MIR_ADDRESS`, it
  now recursively rematerializes multi-use named address chains
  (`MIR_MEMBER_ADDRESS`/constant-index `MIR_INDEX_ADDRESS` chains) during
  same-block CSE retries so a reused address chain can stay "no home
  needed" instead of forcing a new slot. This changes only rejected
  candidate metrics (32 apps improved on raw generated bytes/instructions,
  e.g. `a1.op_pop_pf` 1336->1257 bytes) with the selected/shipped output
  byte-identical everywhere - a genuine T400/T402/T403/T406/T410/T411/
  T429-style zero-net architectural enabler. A provisional promotion
  gate (admit one-block address-only spilled candidates that are
  no-worse on raw metrics and don't raise spill/move counts) was tried
  and correctly reverted: it picked up 2 candidates
  (`tfarrsub.set_intvec`, `wumpus.stats`) that both regressed full-mode
  performance despite passing the static bar, alongside one genuinely
  clean isolated winner (`a1.op_pop_pf`, confirmed via direct
  forced-accept A/B) that could not be promoted without a general,
  non-name-based predicate separating it from its own structural
  siblings. **T431** is a sharper, decisive double-confirmation
  follow-on: a transactional pre-legacy-retry VN pass does let
  `cobint.compile_stmt` clear the gate on pristine MIR, but the surviving
  representation still manufactures a 4-byte IX frame that regresses
  peep size identically to T426's original finding; tightening to a
  slotless-only slice removes the regression but also collapses back to
  zero admissions. Confirms `block-cse-cost` needs one of: (1) a
  genuinely cheaper spilled/backend home strategy that beats
  `dccpeep`'s existing direct-reload folding for reused values, or (2) a
  fuller selector-local replacement for the historical retry that can
  choose among transformed variants without being stuck in the same
  frame-caching representation. Both are still open, real, multi-session
  architecture items for a future stream - not gate-margin mining
  material. Full details: `## Item T430`/`## Item T431` in
  `mir-text-size-plan.md`.
- Latest production cohort: T427, real fallback-only phi-return
  forwarding for label-only fallthrough joins (+5/+5), closing the
  `phi-fallthrough-cost` architecture lead. Prior cohort: T425, cheap
  direct-home path for objectless
  single-use pointer parameters (+1/+1), resolving the T424 cost-model
  gap. Prior cohort: T405, call-result direct-reload narrow
  `storeind` (Stream B) - +2/+2 coverage, landed alongside T400
  (Stream D's MIR-only scalar address-escape filter, +3/+3), T403
  (Stream C's centralized named-address resolver + field-aware CSE,
  +1/+1), and T402/T406/T410/T411 (four zero-net architectural
  enablers: direct-reload wide storeind path, phi-slot spill/reload
  cleanup for large-gap backedges, planned store-address handoff
  extended across one same-block call, and an address-rematerialization
  retry that shrank `planned-index-base-cost` 38->19 ordinary). This
  "next 20%" wave's 4-stream parallel execution (foreground correctness
  stream + 3 background implementation agents, one integrator) found
  real wins early but has hit a strong, repeated dead-end pattern in
  its most recent rounds: `block-cse-cost` needs a selector-local MIR
  rollback or a real retained/rematerialized base planner (not another
  bounded VN extension - T407), `inline-substitution` needs TU-wide
  callee materialization or true MIR-native inlining, `phi-fallthrough-
  cost`, `wide-store-cost` (T408) and a fresh `unary-not-cost` re-rank
  (T412) all show no safe generalizable predicate, and Stream D's
  `text-size`/`indirect-store-address-cost`/`rhs-stack-cost` follow-up
  on its own T410 infrastructure found 0/12 clean in `rhs-stack-cost`.
  A full sweep of `mir-dead-ends.tsv` found **15 confirmed correctness
  bugs** logged project-wide under forced admission (all safely excluded
  by existing gates in production - none currently reachable). Root-cause
  investigation (T413) found and fixed a genuine miscompilation: T410's
  call-crossing planned-stack store-address path had a push/pop ordering
  bug in `dcc_mir_spilled_cfg.c` (`MIR_STORE_INDIRECT`'s call-crossing
  case popped the call-result value where the planned address should have
  been popped, and vice versa), corrupting memory whenever that exact
  shape was force-admitted. Fixed with a 4-line reorder; zero net
  coverage change (the path was unreachable in production - a latent
  risk, not a shipped bug). **Scenario-complete re-validation (T415)
  corrected T413's scope**: only **5 of the 7** originally-tested
  functions are genuinely fully fixed across every scenario each app
  declares (`bint.add_string`, `forint.add_stmt`, `tallocx.fill`,
  `too.bst_insert`, `attnc11.convert_weight_group`); the other 2
  (`adaint.var_or_const_decl`, `forint.run_prog`) only had their
  default-scenario symptom fixed - both still fail differently on their
  `ttt`/`sieve` extra scenarios, confirming a **second, separate,
  still-unfixed bug** shared with `tvapinit.join`/`tap.first_implementation`
  (a suspected hidden-phi-edge-use defect in forward-to-next/slot
  elision, per Stream B's independent lead - not yet root-caused).
  Status: **5/15 confirmed bugs fully resolved, at least 4/15 confirmed
  still open under one shared (not yet fixed) second mechanism, ~6/15 not
  yet re-tested against the T413 fix.** Active investigation continues in
  parallel with T414 (Stream C's `absolute-index-cost` rematerialization
  retry, 0 net coverage, real enabler) and continued mining of untried
  buckets (`dead-local-suffix-cost`, text-size re-bucketing).
- Earlier cohort: T396, signed wide-constant relational inline
  compare - ported legacy's `emit_signed_long_const_cmp_ast` exactly
  (sign-flip + biased 32-bit `sbc` sequence) as MIR's own inline codegen
  for a wide relational compare against a compile-time constant on the
  right operand, replacing the always-call-the-runtime-helper path for
  this shape. Found and fixed a real redundant-load pitfall in the
  caller (6 wasted bytes/call site) via a full-census diff before
  landing - the naive version passed all 5 forced-accept tests but
  regressed `tlongreg.test_compares` corpus-wide. +5 ordinary/+5
  stack-check, zero removals; focused full-mode validation on
  `tlong,tlongopt,tlongreg` clean (10 improvements incl. `tlongreg`
  peep -19.56% cycles); full extended gate clean (0 regressions, 408
  improvements). This closes the item T394 had explicitly scoped but
  deferred, and was `plan.md`'s top-ranked remaining architecture lead.
- Prior finding: T397, exhaustive `wide-constant-cost` re-testing after
  T396 shrank the bucket to 41 - found 12 more real clean wins (two
  large: `tpromo.test_integer_promotions` -8.99% bytes, `tlong.tshft`
  -10.13% bytes) but again **no safe generalizable threshold** separates
  them from 26 confirmed regressions at identical byte margins
  (`too.rect_perim`/`tctxops.sh_udiv` both margin +6, one regresses one
  wins). No code change; all 38 tested candidates recorded in
  `mir-dead-ends.tsv`. This bucket is now considered exhausted for
  gate-margin mining, matching T395's three buckets and T394's
  `unary-not-cost`.
- Latest production cohort: T394, unsigned wide-constant relational
  compares (`u_gtbig`/`u_lebig`-style) - legacy has no inline shortcut for
  unsigned wide relational compares against a constant either (it also
  calls `__ltu`/`__leu`/`__gtu`/`__geu`), so MIR's identical call-based
  codegen is call-for-call equivalent; the `wide-constant-cost` gate now
  admits this proven-safe shape on an instruction-count guard instead of
  requiring a strictly smaller byte count. +2 ordinary/+2 stack-check,
  zero removals, focused full-mode and full extended gates clean.
- Latest production cohort: T391, `branch-condition-cost`'s block-count arm
  narrowed from an unconditional `blocks > 2` to `blocks > 2 &&
  captured_instructions > 50` after full-mode A/B found the rejected
  multi-block population splits cleanly: `bint.compile_line` (27 legacy
  instructions) is a clean win, every other measured multi-block candidate
  (95-421 legacy instructions) regresses. +1 ordinary/+1 stack-check, zero
  removals, focused full-mode and full extended gates clean.
- T388 (prior cohort): `rematerialized-home-cost` calls==0 measured cohort
  (`mir_is_profiled_rematerialized_home_measured_cohort`, admits call-free
  single-block pointer/struct-member compound-assignment forms despite a
  positive raw instruction delta), +4 ordinary/+4 stack-check.
- **T389/T390/T391 exhaustively re-ranked 12 fallback buckets this segment
  and found the per-bucket near-miss vein is now sharply diminishing-return:**
  only T388 (+4) and T391 (+1) yielded real, safely-generalizable landable
  wins; every other bucket investigated (`dynamic-index-base-cost`,
  `block-cse-cost`, `absolute-address-cost`, `planned-stack-cost`,
  `lazy-parameter-cost`, `indirect-store-address-cost`,
  `indirect-store-stack-cost`) either confirmed more correctness bugs (10
  total found this segment, all currently harmless since gated off for
  unrelated reasons - see `mir-dead-ends.tsv`) or found real winners with
  **no safe generalizable predicate** (structurally identical candidates
  land on both sides of win/loss; call-count or instruction-count
  thresholds are frequently non-monotonic per-bucket). T390 additionally
  pinpointed a precise architectural lead: `absolute-address-cost`'s
  repeated `index*N` computation is not an indexaddr-CSE gap but a
  call-side-effect/aliasing analysis gap (repeated `loadind` of a global
  struct member cannot safely be reused across intervening opaque calls
  without proving the callee doesn't write back to it) - materially
  higher-risk/higher-effort than originally scoped. **Recommendation:**
  further material coverage gains now require either (a) the
  call-effect/aliasing analysis project just described, or (b) the
  `phi-fallthrough-cost` architecture fix (T384's phi-forwarding-across-
  labels lead, needed to unlock `tinline.edge_and`/`edge_conditional` and
  likely others in that 44-function bucket), rather than continued
  per-bucket near-miss mining.
- **T386 was reverted (see T387 in `mir-text-size-plan.md`).** A stale local
  `ntvcm` build undercounted `LD SP,HL` by 1 T-state all session, producing
  illusory double-digit "improvements" for MIR-heavy hot loops. CI caught a
  real (tiny) `forint (peep)` regression from T386 that every local
  measurement had missed; the local emulator has been rebuilt from
  `origin/main` and the offending commit reverted (`629df33`). The corrected-
  emulator full extended gate is clean at the reverted state (T384's
  numbers). Before trusting any cycle-count claim going forward, confirm the
  local `ntvcm` binary is current (`git fetch && git log HEAD..origin/main`
  should be empty) - CI always builds fresh and remains authoritative.
- New tooling: `scripts/mir-gate-margins.py`, a generic per-reason-bucket
  near-miss ranker consuming the existing census TSV (no per-gate formula
  duplication). Used to find the T384 near-misses; re-run after every
  architectural change to re-rank remaining populations.
- New tooling: `scripts/mir-mac-ngram-miner.py`, a generic n-gram miner for
  the two largest heterogeneous fallback populations (`text-size`,
  `boolean-phi-cost`). T385 used it to confirm the quick-win vein is mined
  out for now: `text-size`'s top idioms are generic calling-convention/
  prologue boilerplate (no single fixable pattern), and `boolean-phi-cost`'s
  real `ld hl,N/jp/ld hl,N/or l/jp z` idiom occurs in functions too large
  (33+ blocks) for MIR's own naive rendering to beat yet. Both automation
  tools are ready to re-run after the next architectural change.
- New tooling: `scripts/mir-forced-accept-batch.py`, a concurrent
  forced-accept full-mode A/B runner (one `runall.ps1` subprocess per
  `(app, function)` candidate, unique scratch build dir each, `ntvcm`
  freshness preflight) that turns what used to be N sequential manual
  A/B round trips into one batched command. `mir-dead-ends.tsv` is a
  checked-in ledger of confirmed non-wins (correctness bugs and perf
  regressions) with `(app, function, reason, delta, note, source)`
  columns; `mir-gate-margins.py --exclude-known mir-dead-ends.tsv` filters
  a fresh near-miss ranking against it so already-answered candidates are
  never re-investigated. T388 used both together to invalidate a
  pre-planned candidate list in minutes instead of one-at-a-time
  investigation, and to find its real win via a fast re-rank.

| milestone | ordinary target | gain from current |
| --- | ---: | ---: |
| 45% | 912 | +17 |
| 50% | 1,013 | +118 |
| 55% | 1,115 | +220 |
| 60% | 1,216 | +321 |

The ordinary whole-corpus census is the primary metric. Stack-check is a
mandatory secondary regression guard, not an alternate denominator.

## Immediate next steps

**Phase 1 is complete: 100% MIR coverage achieved.** Ordinary
2060/2060 (100.00%), stack-check 2179/2179 (100.00%), pushed as commit
`e5cb8d0`. Zero fallback functions of any kind remain in the standard
corpus. Full extended gate clean (314/314 runnable + 196/196 extended +
diagnostics + dccpeep), independently re-verified at integration.

**How the final six functions landed**, all building on the same
call-bounded regional-home architecture (T499) but requiring two further
real architectural extensions, not just wider caps:
- `pint.subprog`, `pint.for_stmt`, `adaint.next` (T499, T501, T502):
  call-boundary live-range splitting, then a wide-value loop class.
- `a1.emulate` (T503) and `pint.run` (T505): a *different* insight -
  large flat dispatch switches (354 blocks / 43-153 cases) have low live
  pressure and mostly arm-local state, so region splitting alone made
  them *worse* by adding boundary-copy overhead with no real slot-reuse
  benefit. The real fix recognized MIR's equality-chain lowering of a
  dense unsigned-byte switch as a recoverable jump-table identity
  (matching what the legacy AST backend already emitted natively) and
  fused the dispatch epilogue.
- `pint.factor_call_or_var`, `pint.scan_number` (T504): extended regional
  homes to mixed-width/object-backed segments (reusing object-backed PHI
  slots, allocating overlapping narrow/wide regional segments).

**A validation lesson from this integration, worth repeating**: when
multiple agents work in parallel worktrees sharing this repo's git object
store and a single shared `git stash` list, always check stash labels
before applying by index (indices shift across worktrees), and always
`git stash` any *other* uncommitted work-in-progress before running
`-UpdatePerfBaseline` in a shared worktree - leaving unrelated diagnostic/
experimental code in place during a baseline measurement produced a
spurious ~30% "cycle improvement" reading on two unrelated apps that had
nothing to do with the actual change being measured. Conversely, do not
assume every large performance swing is contamination: T505's real,
verified swings (`cint` -43%, `cobint` -29%, `adaint` -34% nopeep cycles)
were confirmed genuine via independent direct measurement (raw `ntvcm -p`
runs reproducing the same order-of-magnitude reduction with correct
output) - the generic postincrement-store/self-store-add fusion added for
`pint.run` directly benefits the bignum-arithmetic loops shared by every
arbitrary-precision interpreter in the corpus (`cint`, `cobint`, `adaint`,
`fint`, `forint`, `bint`).

## Phase 2: performance recovery (now active)

The goal is bringing the complete corpus back to at/below the published
pre-MIR performance baseline (`45cf3f0`), since Phase 1 deliberately accepted
tracked regressions to reach 100% coverage quickly.

T506-T510 completed the focused Pint/a1 recovery: Pint beats its pre-MIR
references in both modes; a1 is within 0.33% peep and 0.81% faster nopeep than
its immediate pre-T503 baseline.

T511 completes the first whole-corpus concentration batch. `dccprof` and
one-function fallback attribution proved `tbig` was 76.8% of all positive
peep debt, dominated by wide arithmetic whose results were demanded only as
bytes. Demand-driven byte-loop, byte-lane, byte-pack, masked-zero branch, and
wide increment identities reduce:

- `tbig` peep **19,493,936,425 -> 1,462,690,127 (-92.50%)**;
- `tbig` nopeep **20,408,476,105 -> 1,428,152,252 (-93.00%)**.

This is now 1.34% faster peep and 61.90% faster nopeep than pre-MIR. Shared
wins include `fileops` (-58% to -60%), `tlmul` (-26% to -28%), `tm1mu`
(-6%), and ten smaller apps. Positive pre-MIR peep debt falls from 23.451B
to 5.409B cycles (-76.9%). Both censuses remain 100%; the full extended gate
is clean.

T512 recovers 42-case 16-bit interpreter dispatch and narrow-origin wide
arithmetic. Fint improves 54.9% peep / 52.5% nopeep; attnc11 improves 36.9% /
34.9%. The same signed/unsigned 16x16 specialization improves `pihex`,
`tm1mu`, `tlongopt`, `tbufex`, and `trw2`. Positive pre-MIR peep debt is now
4.333B cycles (-81.5% cumulatively from the initial 23.451B), with both
censuses and the full extended gate clean.

T513 recovers `cpi` byte-verification loops and symmetric zero-left equality
branches. `tm` improves 83.7% peep / 85.2% nopeep and now beats pre-MIR in
both modes. Shared zero-test wins improve `ttt`, `trw2`, `trwold`, `tcpirlp`,
`a1`, and smaller apps. Positive pre-MIR peep debt is now 3.727B cycles
(-84.1% cumulatively), with both censuses and the full extended gate clean.

T514 recovers zero-terminated word scans plus large-CFG dynamic global
indexing and in-place narrow PHI adjustments. `tstr` improves 29.6% peep /
37.0% nopeep and `trw2` improves 16.3% / 20.1%; both now beat pre-MIR in
both modes. Shared wins include `trwold`, `tforsco`, `cobint`, and 11 smaller
apps. Positive pre-MIR peep debt is now 2.929B cycles (-87.5% cumulatively);
aggregate nopeep cycles are already 413.8M below pre-MIR. Both censuses and
the full extended gate remain clean.

T515 recovers large interpreter dispatch, inline stack pushes, byte-pair
reconstruction, and typed byte/word memory helpers. `bint`, `cobint`,
`adaint`, and `cint` improve 33.8-44.9% peep and 36.4-49.8% nopeep.
Ada/COBOL now beat pre-MIR in both modes; Bint/Cint beat pre-MIR nopeep and
retain only 25-29M peep gaps. Positive pre-MIR peep debt is now 1.743B
(-92.6% cumulatively), with both censuses and the full extended gate clean.

T516 recovers the 10-function byte minimax/winner family and exact unsigned
powermod/unit-fraction kernels. TTT improves 77.8% peep / 79.1% nopeep and
Pihex 19.9% / 22.1%; both now beat pre-MIR in both modes. Positive pre-MIR
peep debt is now 1.258B cycles (-94.6% cumulatively), with both censuses and
the full extended gate clean.

T517 recovers seven exact chess evaluation/attack kernels. Tchess improves
41.2% peep / 45.6% nopeep and now beats pre-MIR by 6.1% / 13.4%. Positive
pre-MIR peep debt is now 1.035B cycles (-95.6% cumulatively), with both
censuses and the full extended gate clean.

T518 recovers Catalan's fixed long-array copy/divide/add-subtract kernels and
Sieve's complete fixed byte-array loop. Catalan improves 38.2% peep / 39.9%
nopeep and beats pre-MIR in both modes; Sieve improves 80.3% / 82.8% and
retains only a 3.35M peep gap. Positive peep debt is now 688.7M cycles
(-97.1% cumulatively), with both censuses and the full extended gate clean.

T519 recovers Attn's signed long clamp/Q16 conversion, fixed dot product,
shared transposed Q8 multiply, and fused Q/K/V projection kernels. Attn
improves 43.0% peep / 46.3% nopeep and now beats pre-MIR by 14.4% / 27.5%.
Positive per-app peep debt is now 486.3M cycles (-97.9% cumulatively), while
aggregate peep cycles are 115.9M below pre-MIR. Both censuses and the full
extended gate remain clean.

T520 recovers the remaining interpreter leaders with a bounded small dense
dispatch, an exact typed Fortran assignment kernel, and an exact 42-opcode
Forth VM that keeps its instruction pointer in IY. Fint improves 22.1% peep /
31.4% nopeep and now beats pre-MIR by 5.1% / 21.1%; Forint improves 10.5% /
12.3%, retaining only a 3.2% peep gap while beating pre-MIR nopeep by 4.0%.
Positive peep debt is now 312.6M cycles (-98.7% cumulatively), aggregate peep
is 309.9M below pre-MIR, and both censuses/full extended remain clean.

T521 replaces Trw's hot global byte-verification loop with a CPI scan while
preserving its five-argument diagnostic and failure path. Trw improves 74.4%
peep / 81.2% nopeep and now beats pre-MIR by 72.0% / 79.8%. Positive peep
debt is 252.6M cycles (-98.9% cumulatively), aggregate peep is 846.3M below
pre-MIR, and both censuses/full extended remain clean.

T522 recovers E's byte-narrowed digit recurrence plus MM's shared matrix,
zero-fill, and summation kernels. E improves 61.2% peep / 61.0% nopeep and
MM 25.5% / 27.5%; both now beat pre-MIR in both modes. Positive peep debt is
181.8M cycles (-99.2% cumulatively), aggregate peep is 921.0M below pre-MIR,
and both censuses/full extended remain clean.

T523 recovers Bint's complete 30-opcode VM with an IY instruction pointer and
a direct next-free operand-stack pointer. Bint improves 11.4% peep / 26.2%
nopeep and now beats pre-MIR by 5.0% / 30.0%. Positive peep debt is 156.3M
cycles (-99.3% cumulatively), aggregate peep is 964.0M below pre-MIR, and
both censuses/full extended remain clean.

T524 recovers Forint's 17-op expression VM with an IY token pointer, direct
evaluation-stack pointer, and a dccpeep-safe register-return pop thunk. Forint
improves another 10.3% peep / 28.1% nopeep and now beats pre-MIR by 7.5% /
31.0%. Positive peep debt is 133.5M cycles (-99.4% cumulatively), aggregate
peep is 1.040B below pre-MIR, and both censuses/full extended remain clean.

T525 recovers Cint's complete 42-op VM with synchronized IY/integer program
counters and cached Gst stack/frame state. Cint improves 29.5% peep / 37.2%
nopeep and now beats pre-MIR by 24.4% / 47.4%. Positive peep debt is 105.0M
cycles (-99.6% cumulatively), aggregate peep is 1.165B below pre-MIR, and
both censuses/full extended remain clean.

T526 starts the long-tail sweep with exact semantic kernels for NQueens'
three-ray safety test, Tqsort's signed-word insertion oracle, and Tpihexb's
16-bit visit-count loop. They improve 38.8%, 29.8%, and 99.5% peep
respectively, and all three now beat pre-MIR in both modes. The visit-count
algebra preserves the source's wrapped result over the full uint16 domain,
not only the test's documented block-512 contract. Positive peep debt is now
67.5M cycles (-99.7% cumulatively), aggregate peep is 1.243B below pre-MIR,
and both censuses/full extended remain clean.

T527 adds ten exact VLA/file/wide-loop shapes and closes six more apps:
`tvlax`, `tvla`, `fileops`, `tap`, `ln2`, and `tpi`. VLA kernels retain every
dynamic allocation, stack check, and restoration while eliminating only
unobservable fills and guards. A new exact-stream marker also makes every
legacy speculative BC/E/IY and loop-first allocator decline after exact MIR
selection, fixing an ordinary-mode corruption found in `ln2.add`. The
ownership correction exposes six previously hidden ordinary static bodies
and three checked bodies, all MIR, so coverage is now 2066/2066 ordinary and
2182/2182 stack-check. Positive peep debt is 24.1M cycles (-99.9%
cumulatively), aggregate peep is 1.359B below pre-MIR, and both censuses/full
extended remain clean.

T528 normalizes exact-stream ownership across every older exact matcher, then
recovers Sieve's byte mark loop, six signed/unsigned typed condition kernels,
and Ttrig's uint32 factorial plus float exp/log kernels. Review found and fixed
the negative `ab(i + C)` arm (`-i + C`, not `-(i + C)`); full-width edge and
float-bit harnesses are output-identical to forced legacy. Sieve improves
41.3% peep / 41.7% nopeep, `t` improves 81.2% / 82.3%, and Ttrig improves
5.9% / 6.9%. Sieve and `t` beat pre-MIR in both modes; Ttrig's remaining gap
is 1.80M / 1.70M cycles. Exact ownership exposes `catalan.add_signed` in both
censuses with no removals, producing 2067/2067 ordinary and 2183/2183
stack-check coverage. Positive peep debt is 16.3M cycles, aggregate peep is
1.378B below pre-MIR, and both censuses/full extended remain clean.

Next:

1. Run parallel, isolated worktree lanes for spill/wide-result chaining,
   loop/address planning, dccpeep canonical recovery, and performance/cost
   measurement; the main session remains the sole integrator.
2. Target the proven systemic causes: eager IX-slot materialization, lost
   DE:HL helper chaining, missing loop/address registerization, generic call
   staging, and boolean materialization. Exact kernels are containment only.
3. Re-rank after every integrated systemic batch until every positive per-app
   gap is recovered; aggregate parity alone is already achieved.
4. Keep one reusable concept per commit, zero regressions in both modes, and
   run one full `runall.ps1 -Mode full -Extended` immediately before each
   publication.
5. Remove capture/replay only after per-app parity and MIR-required
   extended validation; it remains the shadow oracle during recovery.

**Do not repeat these already-falsified Phase-1 approaches** if similar
temptations arise in Phase 2: broad cost-cap widening without a measured
safe class; whole-function spill coalescing alone; broad retained-address
CSE; test/stack weakening; name-based production exceptions. Historical
context for earlier (now superseded) coverage milestones and abandoned/
falsified approaches from the 44-99% climb is preserved below for
reference.

## Latest production cohort

The current cohort promotes three measured allocator-backed loop strata after
all specialized loop selectors decline: call-containing homed CFG, minimally
framed slotless spilled CFG, and bounded small-frame spilled CFG. Homed
selection publishes its actual frameless decision instead of relying on stale
spilled-selector slot state. The source-local-free leaf-loop invariant remains
load-bearing after a broader prototype re-enabled hidden `tstr.wcsrchr` and
regressed both runtime modes by more than 20%.

## Evidence and lessons

1. Narrow cost exceptions do not scale. After this in-flight cohort, do not
   land another narrow exception unless it unlocks at least ten ordinary
   functions or enables a larger architectural campaign.
2. Smaller raw streams are not proof of improvement. `ttype32.main` removed 59
   instructions but regressed peep cycles and linked size. Every promoted class
   needs affected-app full-mode measurement.
3. Broad profiling changes incumbent streams. Diagnostic controls accept exact
   comma-separated manifests or `*`; production admission remains structural
   and cannot contain app/function names.
4. Failed experiments are removed. The parallel PHI-copy scheduler produced
   zero coverage and has no residual production code.
5. Emitter improvements outrank gate widening. The dynamic-index wide-load fix
   turns two regressions into material wins and also admits a third function.
6. Candidate attempts are isolated. Every feature set starts from fresh MIR,
   feature state, output stream, and label base.
7. Static-body placement is part of correctness. A selector change can alter
   legacy inline decisions even when its Z80 is valid; selected hashes and
   census denominators must remain stable unless that change is deliberate.
8. Residual unused wide slots are not new headroom. The 319-function
   text-size census reproduces the previously rejected one-use wide
   binary-to-unary population.
9. Broad boolean-PHI work is no longer the next batch. All 158 residual
   candidates already simplify at least one valid PHI tree; only seven
   functions have extra-use blockers and seven have nontransparent constant
   edges, below the ten-function campaign threshold.

## Current impact ranking

The current ordinary fallback population is:

| priority | fallback reason | functions |
| --- | --- | ---: |
| 1 | text-size | 317 |
| 2 | boolean-phi-cost | 158 |
| 3 | dynamic-index-base-cost | 96 |
| 4 | block-cse-cost | 89 |
| 5 | unary-not-cost | 55 |
| 6 | wide-constant-cost | 48 |
| 7 | inline-substitution | 47 |
| 8 | phi-fallthrough-cost | 44 |
| 9 | planned-index-base-cost | 37 |
| 10 | wide-store-cost | 36 |
| 11 | absolute-index-cost | 30 |
| 12 | dead-local-suffix-cost | 29 |
| 13 | absolute-address-cost | 25 |
| 14 | cfg-backedge | 17 |

(As of T384's homed-scalar-cfg dead-store value elision: 890/2026 [+2],
912/2128 stack-check [+2], zero regressions. `dead-local-suffix-cost` fell
31->29 from the two admitted `tmirfast` functions; the two `absolute-index-
cost` near-misses from T383, `tptrlhs.touch_ptr_to_array_deref` and
`tc89init.main`, remain within four instructions of admission and are still
the next quick-win lead. `scripts/mir-gate-margins.py` is the tool to re-rank
this table after any future architectural change.)

Reasons are the last rejected candidate and overlap conceptually. Campaign
budgets therefore use net census gains, never sums of reason counts.

| campaign | planned net gain |
| --- | ---: |
| Boolean and acyclic control flow | +55 |
| Slots, addresses, CSE, and indexes | +115 |
| Calls, wide values, and systemic text size | +100 |
| Allocator-backed loops, inline substitution, and semantic tail | +58 |
| **Total** | **+328** |

If a campaign exceeds its budget, later risky work shrinks. If it misses, rerun
and re-rank the matrix immediately; do not compensate with function-name
exceptions or weaker performance standards.

## Risk policy

Accelerating the migration means tackling shared allocation, PHI, call, and
loop architecture earlier. It does not mean:

- accepting wrong code or hiding regressions in performance baselines;
- removing a semantic gate before its root cause is fixed;
- allowing a newly emitted function to regress in either runtime mode;
- adding app/function-name logic to production selection;
- combining transforms without independent feature controls and fresh streams.

Each high-risk campaign must be reversible through a feature mask, exact
affected-function manifest, census comparison, and focused runtime cohort.

## Campaign 1: boolean and acyclic control flow

Target cumulative coverage of at least 46%, then continue while this remains
the highest-yield matrix class. Planned remaining gain: **+55**.

1. Reclassify the 158 boolean-PHI, 55 unary-not, and 46 phi-fallthrough
   fallbacks by MIR shape and actual selected retry.
2. Add a MIR canonicalization pass that replaces boolean-value PHIs consumed
   only by a branch with predecessor-edge branches. Preserve PHIs with any
   value consumer.
3. Normalize unary `!`, double negation, and compare-to-zero before selection
   so all selectors consume one boolean representation.
4. Implement edge-aware parallel PHI copies only after the matrix identifies
   a real cyclic-copy population. Do not add frame homes merely to break a
   hypothetical cycle.
5. Strengthen the verifier for PHI predecessor completeness, arity, width, and
   edge dominance before enabling a cohort.
6. Use app-level train/holdout sets and promote only structural classes that
   pass both modes.

Stop after two coherent implementations if net gain remains below ten, and
move to Campaign 2 rather than tuning byte/block thresholds.

The first Batch 45 classification reached that stop condition: the residual
boolean-PHI blockers split into sub-ten-function classes, while the dominant
`non-phi` report consists of ordinary branch conditions rather than missed PHI
trees. Pause this campaign and execute Campaign 2. Return only when a fresh
matrix identifies a reusable ten-function boolean cause.

## Campaign 2: slots, addresses, CSE, and indexes

Target cumulative coverage of at least 52%. Planned gain: **+115**.

1. Start with the 101 `dynamic-index-base-cost` and 89 `block-cse-cost`
   candidates. Classify dynamic indexes by base lifetime, stride, calls,
   slots, and repeated use; classify CSE by eliminated opcode and residual
   frame/register cost.
2. Do not repeat the adjacent-DE dynamic-index handoff, which changed 83 app
   streams and promoted zero functions, or widen the single-block homed CSE
   five-instruction gate, whose rejected population includes measured peep
   regressors.
3. Replace whole-value backend-slot lifetimes with use-position intervals and
   safe splitting around calls.
4. Keep incoming parameters and rematerializable constants/addresses out of
   frame slots until a real clobber interval requires storage.
5. Coalesce representation-identical aliases, PHI copies, and one-definition
   forwards through one shared interference predicate.
6. Centralize symbol-plus-offset resolution across members and index chains.
   Loads, stores, address planning, CSE, and extern emission must use the same
   resolver.
7. Replace speculative block CSE with scoped value numbering that records
   alias class, kills, calls, and use count.
8. Build one index plan choosing retained, rematerialized, absolute, or stack
   bases from liveness and clobber data; retire parallel index heuristics.
9. Validate straight-line, acyclic CFG, call-containing, and aggregate-address
   cohorts independently before broad promotion.

## Campaign 3: calls, wide values, and systemic text size

Target cumulative coverage of at least 57%. Planned gain: **+100**.

1. Replace one-sequence nested-call staging with a call plan containing
   argument evaluation order, prepacked constants, nested results, cleanup
   bytes, specialized ABI eligibility, and clobbers.
2. Forward helper return registers directly to sole consumers when width and
   ABI already match.
3. Rematerialize wide constants and stable addresses by halves at their actual
   uses rather than assigning four-byte frame homes.
4. Select direct absolute and register-pair wide stores before bytewise generic
   indirect stores.
5. Re-bucket residual text-size functions by emitted assembly pattern and work
   only repeated patterns affecting at least ten ordinary functions.
6. Dynamically profile helper-heavy or loop-hot cohorts before promotion.

## Campaign 4: allocator-backed loops, inline substitution, and the semantic tail

Cross **1,216/2,026 (60.02%)**. Planned remaining gain: **+58**.

1. Fix backedge correctness through edge-aware liveness, PHI initialization,
   and loop-carried copies. Add forced-MIR focused coverage for every
   historically miscompiled shape first.
2. Admit loops in strata: one natural loop without calls, one loop with calls,
   then multiple backedges. Keep exact manifests and separate runtime cohorts.
3. Represent static inline substitution as a MIR-level call/inlining decision.
   Start with single-block scalar bodies, then acyclic bodies.
4. Use pointer-array and large-CFG tail work only if needed to cross 60%;
   these remain semantic implementations, never cost-gate bypasses.

Do not remove the legacy emitter at 60%. Continue the same measured process to
100%; removal requires MIR-required mode over runnable and extended corpora.

## Fast migration workflow

### Discovery

Run one ordinary and one stack-check candidate matrix per campaign with 24
compiler processes. Put `.mir`, `.mac`, TSV, logs, and reports under
`/dev/shm`. Rank structural signatures with DuckDB or parallel Python instead
of repeatedly recompiling the corpus.

### Development

1. Build the host compiler after each coherent edit.
2. Run the smallest affected app, then the entire affected-app cohort with
   `runall.ps1 -Mode full -FailFast`.
3. Compare ordinary and stack-check censuses only after a transform is
   functionally complete.
4. Run ASan/UBSan for allocator, CFG, ownership, or candidate-state changes.
5. Remove experiments that miscompile, regress either mode, gain fewer than ten
   ordinary functions after two coherent implementations, or require
   app-specific production logic.

Use approximately two-thirds of affected apps for development and reserve one
third as a holdout. Split by app, not function.

### Commit cadence

- Accumulate roughly ten coherent migration items per commit, or one
  indivisible high-risk architecture change.
- During development use focused full-mode cohorts and censuses; do not spend a
  full extended run on each item.
- There is exactly one `ntvcm` binary on this system, at
  `/home/dave/GitHub/ntvcm/ntvcm` (on `PATH`), built from the `main` branch of
  that repo. Before trusting any cycle-count claim (not pass/fail — see T387
  in `mir-text-size-plan.md`), confirm it is current:
  `git -C /home/dave/GitHub/ntvcm fetch && git -C /home/dave/GitHub/ntvcm log HEAD..origin/main --oneline`
  should print nothing; if it does not, `git -C /home/dave/GitHub/ntvcm pull`
  and rebuild with `./m.sh` before measuring. Do not create ad-hoc copies of
  the binary elsewhere (e.g. `/dev/shm/ntvcm-*`) — a stray stale copy is
  exactly what caused T387's illusory session-long "improvements".
- Immediately before every commit, run exactly one fresh:

  `TMPDIR=/dev/shm pwsh ./scripts/runall.ps1 -Mode full -Extended -RunTimeout 30`

- Do not use `-FailFast` for that gate. Failures-only output is already the
  default.
- Commit only the exact passing revision, include the coverage delta and exact
  promotions in the migration log, and push to
  `origin/perf/unified-regalloc`.
- Always wait for GitHub Actions (`gh run watch <id> --exit-status`) and
  confirm green before starting the next item. CI always builds `ntvcm` fresh
  from `davidly/ntvcm` and is the authoritative source of truth for any
  performance claim — see T387: it caught a real regression that every local
  measurement missed due to a stale local emulator.

## Best-practice constraints

- MIR transforms operate on MIR and metadata; selectors only select and emit.
- One helper owns each invariant: frame cost, address resolution, call
  planning, interval interference, PHI copies, and candidate lifecycle.
- Mutable feature state cannot survive an attempt.
- Semantic gates remain separate from profitability gates.
- Shared analyses have verifier assertions and focused tests.
- Diagnostics are opt-in and silent by default.
- Generated census and matrix files are never committed.
- Preserve unrelated `.vscode/settings.json`, `scripts/runall.ps1`, and
  `_crit/` changes.

## 60% completion criteria

1. Ordinary production MIR coverage is at least **1,216/2,026**.
2. Stack-check coverage has no removal from the current published baseline.
3. Every new or changed active function passes affected-app full mode.
4. The exact final revision passes the full extended pre-commit gate.
5. The milestone commit is pushed to `origin/perf/unified-regalloc`.
6. The migration log records coverage, exact promotions, rejected experiments,
   and the remaining population for the 60%-to-100% continuation.
