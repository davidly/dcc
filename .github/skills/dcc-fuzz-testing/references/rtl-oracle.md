# Runtime library fuzzer

Use this driver for library behavior against the host, with comparison rules
for the CP/M target. Runs need clang, the dcc toolchain, and an emulator
(default `ntvcm`). The driver tests both peep and nopeep.

```sh
python3 scripts/rtl-oracle-fuzz.py --seeds 1-10 --jobs 2 --work build/fuzz-rtl
```

- `INVALID` means the host build or run failed. `BUILD` covers target
  build, run, or profiling errors. Check the reported stage and configuration.
  `FAIL` means a comparison mismatch. The report shows the first example
  per test name unless `--verbose` is set.
- Record the seed, test ID, profile, peep setting, `--tests`, and
  `--ulp`. Confirm the expected behavior under the driver's target rules,
  especially file semantics and float comparisons. The ULP tolerance
  applies to marked `strtod` results.
- For a test in the main program, use `--tests COUNT --generate SEED SOURCE
  --only TEST_ID` to isolate it. For an `--io-matrix` finding, keep the
  saved profile's `t.c` and rebuild with its float/long I/O settings. The
  generation command emits the main program.
- Use `--rtl-coverage` when auditing runtime label reach. It requires an
  emulator that writes per-PC profiles and produces `rtl-coverage.tsv`.
  Confirm correctness by comparing expected and actual behavior.
