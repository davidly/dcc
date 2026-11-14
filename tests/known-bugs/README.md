# Known-bug reproducers

Each `bugNNN.c` here reproduces one open entry in
[compiler-bugs.md](../../compiler-bugs.md) and is expected to **fail** with the
current compiler: it is rejected, hangs, or prints the wrong output. They are
kept out of `tests/*.c` so the regression suite (`scripts/runall.py`,
`scripts/runall.ps1`) stays green; those runners only look at `tests/*.c`.

`baselines/bugNNN.txt` is the output the program should print. It is the host
compiler's output, not dcc's.

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

A reproducer reported `FIXED` passes in both modes. Promote it to a regular
test:

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
`tests/tpephl.c`, `tests/tpost16.c` and `tests/tpostbr.c`
were promoted this way.

## Adding one

Write a small program that prints one line per shape of the bug, using
fixed-width types (`<stdint.h>`) so the result does not depend on `int` being
16 or 32 bits, then run `python3 scripts/run-known-bugs.py --update bugNNN` to
write the baseline from the host compiler. The script refuses a program that
ASan or UBSan objects to.
