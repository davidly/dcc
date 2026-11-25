# Fuzz testing

Fuzz testing automatically generates or mutates many inputs and checks the results. For dcc, it explores unusual combinations of C features and malformed inputs, revealing miscompilations, crashes, and hangs that tests written by hand can miss.

Run these commands from the repository root with Python 3 and a built [DCC toolchain](../00-setup-toolchain.md). Put `ntvcm` on `PATH` for the first three fuzzers. `clang` is required by the Clang and runtime fuzzers and by the syntax fuzzer's `--accepts` option. Use `--help` on any driver to see all its options.

## How seeds work

A seed is an integer that controls the fuzzer's random choices. Each seed generates a test program or selects and mutates an existing C file. Use `--seeds 17` to run one seed, `--seeds 1-20` to try an inclusive range, or `--seeds 1-20,250` to combine ranges and individual seeds.

Try more seeds to explore more inputs. To replay a failure, run the reported seed with the same checkout and options. Save the generated C file and any headers with the failure log, since changes to the generator or its inputs can change what a seed produces.

## Running a campaign

All four fuzzers take `--seeds` to select inputs and `--jobs` to set the number of parallel workers. Run this setup in Bash or Zsh before using the examples below in the same shell. `tee` displays and saves the output, and `pipefail` preserves a failure status from the fuzzer. A typical campaign runs tens of thousands of seeds. For a quick check, use `--seeds 1-20`.

```bash
set -o pipefail
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
mkdir -p build
```

## Clang oracle (`clang-oracle-fuzz.py`)

Generates C programs and compares dcc's output with Clang's. The default `int` generator exercises integer code generation, `--gen float` covers floating point arithmetic and conversions, and `--gen frontend` targets preprocessing and parsing. See [Differential testing against clang](06-differential-testing.md) for how the oracle checks inputs and how to reduce failures.

```bash
python3 scripts/clang-oracle-fuzz.py --gen int \
  --seeds 260001-300000 --configs production --jobs "$JOBS" \
  2>&1 | tee build/fuzz-int.log

python3 scripts/clang-oracle-fuzz.py --gen float \
  --seeds 260001-300000 --configs production --jobs "$JOBS" \
  2>&1 | tee build/fuzz-float.log

python3 scripts/clang-oracle-fuzz.py --gen frontend \
  --seeds 300000-400000 --configs production --jobs "$JOBS" \
  2>&1 | tee build/fuzz-frontend.log
```

`--configs production` tests combinations of peephole optimization and stack checking. Add `--off known` to skip generated forms associated with logged bugs.

## Typed expressions (`typed-expr-fuzz.py`)

Checks integer expressions and small programs against a Python model of dcc's integer widths and conversions, with and without peephole optimization. Use `--mode tree` for nested expressions, `store` for updates to stored values, or `prog` and `scope` for small programs with calls and control flow.

```bash
python3 scripts/typed-expr-fuzz.py --seeds 1-20000 --jobs "$JOBS" \
  2>&1 | tee build/fuzz-typed-expr.log
```

## Runtime library (`rtl-oracle-fuzz.py`)

Compares dcc's runtime results with a native Clang build for cases such as formatted I/O, strings, math, allocation and file operations. Add `--io-matrix` to exercise all four combinations of float and long formatted I/O support.

```bash
python3 scripts/rtl-oracle-fuzz.py --seeds 1-20000 --jobs "$JOBS" \
  2>&1 | tee build/fuzz-rtl.log
```

## Syntax and diagnostics (`syntax-crash-fuzz.py`)

Mutates C files from `tests/` and checks for crashes, hangs and diagnostic failures. Add `--directives` to focus on preprocessor directives or `--accepts` to flag syntax dcc accepts that Clang rejects. Pass an AddressSanitizer and UndefinedBehaviorSanitizer build with `--dcc PATH` to detect sanitizer errors too.

```bash
python3 scripts/syntax-crash-fuzz.py --seeds 1-20000 --jobs "$JOBS" \
  2>&1 | tee build/fuzz-syntax.log
```
