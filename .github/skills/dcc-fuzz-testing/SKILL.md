---
name: dcc-fuzz-testing
description: Run dcc fuzz campaigns, reproduce and triage seeds, and verify fixes for compiler, runtime library, or frontend bugs found by the repository's standalone fuzz drivers.
---

# Fuzz testing dcc

Choose the driver by the behavior under investigation and read its reference.
Stages such as preprocessing may be exercised by more than one driver with
different expectations.

| Driver under `scripts/` | Use it for | Reference |
| --- | --- | --- |
| `clang-oracle-fuzz.py` | Integer and float code generation or valid frontend constructs, compared with host clang | [Clang oracle](references/clang-oracle.md) |
| `typed-expr-fuzz.py` | Integer conversions, expression evaluation and stores, checked against a Python model of dcc's types | [Typed expressions](references/typed-expr.md) |
| `rtl-oracle-fuzz.py` | Runtime library behavior such as formatted I/O, math, and files, compared with the host under target-aware rules | [Runtime library](references/rtl-oracle.md) |
| `syntax-crash-fuzz.py` | Compiler robustness on mutated C or preprocessor directives, with an optional clang acceptance check | [Syntax robustness](references/syntax-crash.md) |

## Run a campaign

- From the repository root, start with a small batch to check the toolchain,
  then size the campaign and worker count for the driver and available time.
- Record the repo revision, compiler build and full command, including
  generator settings, configuration and timeouts. A seed alone is insufficient
  to reproduce an input.
- Use a separate `--work` directory under `build/` for each run or rerun that
  needs to preserve prior evidence. Keep the log, retained sources and
  expected/actual results or diagnostics there. Several drivers replace
  per-seed directories.
- The bar shows processed inputs and ETA on a terminal, including via
  `/dev/tty` where available when stdout is redirected to a log. Use the
  final tally, exit status and saved artifacts for triage.

## Confirm, record and close

1. Reproduce in the original configuration and preserve the input. Reduce or
   isolate it using the driver's reference, then confirm the same observed
   failure. Check target limits, the harness and the driver's expected behavior
   before classifying it as a compiler or runtime defect.
2. Look for the same root cause in `compiler-bugs.md`. Record confirmed bugs
   with the command, configuration, expected and actual behavior, and a
   reproducer with a passing control where practical. Pick a regression
   assertion that matches the failure, such as output, a diagnostic, or clean
   compiler termination. File or update a GitHub issue when authorized.
3. For fix verification, rerun the confirmed reproducer and a focused batch
   with the driver that found it. Run the project regressions required for
   the fix and promote the reproducer to the appropriate suite before closing
   the entry. For clang bugs tracked with host baselines, use the closure
   procedure in the clang reference.
