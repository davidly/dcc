# Typed expression fuzzer

Use this driver for integer conversions, storage and code generation. The
Python model accounts for dcc's integer widths and skips undefined inputs.
Runs need `dccmake`, the built dcc toolchain, and an emulator (default
`ntvcm`). Expected output comes from the model.

```sh
python3 scripts/typed-expr-fuzz.py --seeds 1-10 --mode tree --jobs 2 --work build/fuzz-typed-tree
```

Use `binary` for individual operators, `tree` for nested expressions,
`mixed` for both, `store` for updates, and `prog` or `scope` for interactions
among statements, aliases, calls, or control flow. The driver builds peep
and nopeep and stops a seed at its first failure. Record any `--env` overrides.

- `SKIP` means a target size limit was reached. For `COMPILE-FAIL` or
  `HANG`, inspect the retained build logs and the stage named in the report.
- For a `MISMATCH`, compare `t.expected` with `out-peep` or `out-nopeep`
  in the seed directory. The first hex field is the case number times ten
  plus the input index. Confirm the expected value using dcc's widths and
  conversions.
- Use the saved `t.c` or regenerate with the original `--mode` and
  `--count`: `--generate SEED SOURCE EXPECTED`. Reduce by removing unrelated
  cases while retaining the failing context and its expected output.

See [the script guide](../../../../scripts/README.md#typed-expr-fuzzpy)
for mode details.
