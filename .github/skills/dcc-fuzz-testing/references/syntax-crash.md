# Syntax robustness fuzzer

Use this driver to check how dcc handles mutated C from `tests/*.c`. It
needs a dcc compiler binary. Select a sanitizer build with `--dcc` when
investigating memory errors. The optional `--accepts` check also needs clang.

```sh
python3 scripts/syntax-crash-fuzz.py --seeds 1-20 --jobs 2 --work build/fuzz-syntax
```

- Use `--directives` to restrict mutations to preprocessor directives.
  Use `--messages` for a diagnostic frequency tally.
- A normal compiler diagnostic is a clean rejection. The driver reports
  `crash`, `hang`, `silent`, `badline`, or `fatal` when it observes an
  abnormal outcome. With `--accepts`, it can also report `accepted` when
  dcc accepts a mutation rejected by clang and clang accepts the original.
  Confirm the relevant language rule before treating that difference as a bug.
- Preserve `SEED.c`, `SEED.out`, and the reported class and signature.
  Re-run with `--reduce SEED` and the original `--dcc`, `--timeout`,
  `--directives`, and `--accepts` settings where applicable. Recheck
  `reduced-SEED.c` against the original outcome.
- Use a compile or diagnostic regression for confirmed bugs. Assert the
  intended compiler outcome and include a valid control when it helps
  distinguish the failure.
