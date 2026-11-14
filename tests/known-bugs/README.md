# Known-bug reproducers

Each `bugNNN.c` here reproduces one open entry in
[compiler-bugs.md](../../compiler-bugs.md) and is expected to **fail** with the
current compiler: it is rejected, hangs, or prints the wrong output. They are
kept out of `tests/*.c` so the regression suite (`scripts/runall.py`,
`scripts/runall.ps1`) stays green; those runners only look at `tests/*.c`.

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

BUG-059 records the independent missing implicit float conversion exposed by
the stopped D2e admission trial. Like BUG-056, it remains latent behind
BUG-041 admission; neither parked fixture is promoted by D2e-1.
`tests/tupdprov.c` separately covers the already-admitted wide update
provenance repaired by that safe subset.

BUG-060 was promoted as `tfpround`: exact-bit `%f` nearest-even rounding,
decimal carries, signed zero and subnormal fractions. `%e`/`%g` remain
unsupported; `scripts/printf-float-proof.py` checks their pre-existing behavior.

## Checking them

```sh
python3 scripts/run-known-bugs.py            # every reproducer
python3 scripts/run-known-bugs.py bug003     # one
```

For each reproducer the script first proves the baseline: the program must
build with the host compiler (the flags `validate-unit-test.ps1` uses), print
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
3. Confirm with `pwsh ./scripts/validate-unit-test.ps1 -App <name>` and
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
`tests/tphiarm.c`, `tests/twphicpy.c`, `tests/tphislot.c`, `tests/tbfcomp.c`, `tests/tfdivr.c` and `tests/tfpround.c` were promoted this way.


`tests/tlogblk.c` covers BUG-004's host-proven word and block-long facets
cleared by E1/A2b-1. A2b-3 closes the remaining byte-local logical SSA facet:
`tests/tlogbyte.c` promotes the exact checksum reproducer and adds the other
long consumers, nested/variable paths and short-circuit side effects.

## Adding one

Write a small program that prints one line per shape of the bug, using
fixed-width types (`<stdint.h>`) so the result does not depend on `int` being
16 or 32 bits, then run `python3 scripts/run-known-bugs.py --update bugNNN` to
write the baseline from the host compiler. The script refuses a program that
ASan or UBSan objects to.
