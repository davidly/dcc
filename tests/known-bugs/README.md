# Known-bug reproducers

Each `bugNNN.c` here reproduces one open entry in
[compiler-bugs.md](../../compiler-bugs.md) and is expected to **fail** with the
current compiler: it is rejected, hangs, or prints the wrong output. They are
kept out of `tests/*.c` so the regression suite (`scripts/runall.py`) stays green; those runners only look at `tests/*.c`.

`baselines/bugNNN.txt` is the output the program should print. It is the host
compiler's output, not dcc's.

Diagnostic-masker BUG-022 was closed by restoring the compiler error, not by
fixing the unsupported function that triggers it. Its re-established source
and host baseline are in `tests/diagnostics/fixtures/`, checked by
`scripts/tests/test_mir_diagnostics.py` with normal/strict and debug variants.
After H fixed the initial BUG-020 supplier, the current negative source
uses a valid local aggregate initializer from va_arg. It is host-proven and
rejects in every debug mode; only a copy with the old M-b guards masks it.
The historical runnable program was already fixed by BUG-008/019 and is kept
as `tests/tlbreuse.c` alongside that negative regression.

BUG-041 (float `++`/`--` value rejected) and the two latent defects behind
it, BUG-056 (negated update treated as an integer) and BUG-059 (missing
implicit int→float operand conversion), are closed by D2e-2's admission and
promoted as `tests/tfincval.c`, `tests/tfnegupd.c` and `tests/tfupdcnv.c`;
`tests/tblfcons.c` covers block integer operands feeding float arithmetic.
`tests/tupdprov.c` separately covers the already-admitted wide update
provenance repaired by D2e-1.

BUG-060 was promoted as `tfpround`: exact-bit `%f` nearest-even rounding,
decimal carries, signed zero and subnormal fractions. `%e`/`%g` remain
unsupported; `scripts/printf-float-proof.py` checks their pre-existing behavior.

BUG-061 was promoted as `tests/tsizhigh.c` (unsigned typing for compile-time
`sizeof` values at or above `0x8000`). BUG-062, the residual small/runtime
`sizeof` signedness, is closed by the full unsigned `size_t` typing with an
AST proven-signedness demotion pass and promoted as `tests/tsizesgn.c`; the
integer generator now emits `sizeof` forms. See
[fix-plan.md](../../fix-plan.md#s1-high-bit-constant-sizeof-result-typing).

BUG-065 (va_list frame offset not shifted when a variadic function takes an
IY register home, corrupting the caller's saved IY) was found while measuring
that trial and is promoted as `tests/tvaiyhom.c`. BUG-066 (`sizeof` of a
nested-block scalar local reporting 2) was found by the first fuzz run with
the generator's `sizeof` leaf and is promoted as `tests/tszblock.c`.

BUG-067 (`sizeof` of an element, member or dereference of a nested-block
local) and BUG-068 (`op=` and indexed reads through a nested-block pointer)
are closed by T3 and promoted as `tests/tszderiv.c` and `tests/tptrcasg.c`;
the `blockcassign` steer is removed. BUG-069 (`sizeof` of a
two-dimensional or partially indexed array struct member) is closed by T4 and
promoted as `tests/tszmdim.c`.

Open reproducers: none. The extended seeds 15001–22000 findings are promoted as:

- BUG-070: `tests/tbfrhs.c`, compound division/remainder by bit-fields.
- BUG-071: `tests/tfcmexpr.c`, integer compounds consuming float expressions.
- BUG-072: `tests/tpebool.c`, Boolean suffix paths with equal constants.
- BUG-073: `tests/twalksc.c`, block probe homes beside live pointer walks.
- BUG-074: `tests/twalkend.c`, the separate pointer-walk hang reduction.

## Checking them

```sh
python3 scripts/run-known-bugs.py            # every reproducer
python3 scripts/run-known-bugs.py bug003     # one
```

For each reproducer the script first proves the baseline: the program must
build with the host compiler (the flags `validate-unit-test.py` uses), print
exactly the baseline, and print the same under ASan and UBSan. It then builds
and runs the program with dcc, with and without `dccpeep`, and reports
`fails` with the reason, or `FIXED`.

`--dcc PATH` checks another compiler build, for example an older one when
dating a regression.

## When a bug is fixed

A diagnostic-masking reproducer can declare ` * DCC-EXPECT-ERROR: message`
in its header. The host baseline must still be proven, but dcc passes only
when it exits nonzero with that actual primary diagnostic; successful output
or incidental message text is not a fix. Promote such a source and its host
baseline to `tests/diagnostics/fixtures/` with an explicit Python matrix, and
retain a runnable neighbouring control with a new measured performance row.
This avoids requiring a separate unsupported-emission defect to be fixed.

A runnable reproducer reported `FIXED` passes in both modes. Promote it to a
regular test:

1. Move `bugNNN.c` to `tests/<name>.c` and `baselines/bugNNN.txt` to
   `tests/baselines/<name>.txt`, with a name that says what it covers, and
   rewrite the header comment to describe the coverage instead of the defect.
2. Add its row to `tests/perf_baselines.csv`.
3. Confirm with `python scripts/validate-unit-test.py -App <name>` and
   `python3 ./scripts/runall.py -apps <name> -mode=full`.
4. Close the entry in `compiler-bugs.md`, and remove the bug's feature from
   `KNOWN_BUGS` in `scripts/clang_oracle_gen.py` so the fuzzer generates that
   form again.

`tests/twcasgm.c`, `tests/tblkdiv.c`, `tests/tcastasg.c`, `tests/tblkabi.c`,
`tests/tblkret.c`, `tests/tblkcopy.c`, `tests/tblkneg.c`, `tests/tforhome.c`,
`tests/tfltret.c`, `tests/tflucast.c`, `tests/tflscast.c`,
`tests/tandself.c`, `tests/tselfop.c`, `tests/tflmadd.c`, `tests/tflnot.c`,
`tests/twincg.c`, `tests/tlpcall.c`, `tests/tconstun.c`, `tests/tcmbyte.c`,
`tests/tcmwide.c`, `tests/tcmfloat.c`, `tests/tinc32a.c`, `tests/tflinc.c`,
`tests/tpephl.c`, `tests/tpost16.c`, `tests/tpostbr.c`, `tests/tpost32.c`,
`tests/tflcnot.c`, `tests/tflcini.c`, `tests/tconstdz.c`, `tests/tdeadblk.c`,
`tests/tpost8.c`, `tests/tinittyp.c`, `tests/tcmprhs.c`, `tests/tflncmp.c`,
`tests/tbcshl.c`, `tests/tbcsar.c`, `tests/tbfexpr.c`, `tests/tflidx.c`, `tests/tndwide.c`,
`tests/tlbreuse.c`, `tests/twincadd.c`, `tests/tmerge16.c`, `tests/tearly16.c`,
`tests/tphiarm.c`, `tests/twphicpy.c`, `tests/tphislot.c`, `tests/tbfcomp.c`, `tests/tfdivr.c`, `tests/tfpround.c` and
`tests/tsizhigh.c`, `tests/tblkword.c` and `tests/tblkbind.c` were promoted this way.


`tests/tlogblk.c` covers BUG-004's host-proven word and block-long facets
cleared by E1/A2b-1. A2b-3 closes the remaining byte-local logical SSA facet:
`tests/tlogbyte.c` promotes the exact checksum reproducer and adds the other
long consumers, nested/variable paths and short-circuit side effects.

`tests/tblkword.c` promotes BUG-058's full word-initializer reproducer.
`tests/tblkcast.c` separately covers the signed-word cast facet of BUG-015
cleared by A3 (parked output statements 156–158). With A4's block-pointer
load typing (`tests/tptrblk.c`) and the D2e-2 deferred float operand repair
(`tests/tflbconv.c`) also present, BUG-015's reproducer passes completely and
is promoted as `tests/tblkbind.c`; the `blockptr`, `blockcast`, `blockdec`,
`fblockneg` and `fblockint` steers are removed.

Restoring those shapes exposed two defects, both since fixed. BUG-063 (#332)
was a store through a nested-block pointer to a 32-bit object that kept the
16-bit conversion inserted for the assumed target; T2 retargets that
conversion in the pointee repair and promotes the reproducer as
`tests/tblkstor.c`. BUG-064 (#333) was integer seed 261 reduced to one call:
a promoted local's frame slot was reused as a spill hole while its fused
`inc (ix+n)` self-store still wrote it, incrementing a spilled function
pointer. T1 tightened the hole proof and promoted the reproducer as
`tests/tslothol.c`.

## Adding one

Write a small program that prints one line per shape of the bug, using
fixed-width types (`<stdint.h>`) so the result does not depend on `int` being
16 or 32 bits, then run `python3 scripts/run-known-bugs.py --update bugNNN` to
write the baseline from the host compiler. The script refuses a program that
ASan or UBSan objects to.
