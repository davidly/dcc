# dcc compiler bugs

## Current status

There are no open production compiler bugs in this checkout. Fixed findings
are covered by the regular tests under [`tests/`](tests/) and the known-bug
runner has no parked reproducers. The detailed fix history is in
[`fix-plan.md`](fix-plan.md).

The full suite (654 apps) passes in both stack modes on Linux and Windows,
with no performance regressions in the default configuration.

## Seed 282623

**Fixed locally.** The integer production campaign over seeds 260001–300000
found an assembly failure in `peep` and `peep-nosc`; both configurations
without peephole optimization passed. `pass_skip_ix_reload_across_label`
named its new labels from the current line index. Other passes shrink the
program between iterations, allowing another site to reuse that index. Two
`Lskrl_9418` definitions caused multiply defined symbols and an out-of-range
relative branch.

The pass now checks existing definitions and selects an unused suffix,
preserving the same instructions. The fixture
`tests/dccpeep/skip-long-reload-label.in.mac` covers an existing skip label;
`scripts/tests/test_dccpeep_reload_labels.py` checks unique labels, the
original branch target, successful assembly, and repeated optimization. The
regression fails before the fix, and the original seed passes all four
production configurations after it.

Verification on upstream `4754d505` passed: 300-seed production batches for
both generators, the full suite in both stack modes, and Python and debugger
checks. Existing performance baselines are unchanged.

## Float seed 418814

**Fixed locally.** The float production campaign over seeds 400001–450000
reported state mismatches at IDs `4001` through `4003` in all four
configurations. After scoped type repair, two prefix updates of float
fields temporarily had integer result types. MIR used those provisional
types to change their enclosing float division to integer division
before it resolved the field types.

MIR now preserves the parsed float division until the operand types are
known. The regression in `tests/tfincdiv.c` checks the quotient and both
updated fields. It printed `-4 10 8` before the fix and now matches Clang's
`5 10 8`. The original seed also agrees with Clang in all four production
configurations.

## Performance follow-up

Forcing the regional emitter with `DCC_MIR_PREFER_REGIONAL=1` produces correct
output for `a1`, but increases cycle counts and executable size. The default
production configuration passes its performance checks.

| Mode | Default cycles | Forced regional cycles | Default bytes | Forced regional bytes |
|---|---:|---:|---:|---:|
| Peephole | 10,188,168 | 67,313,788 | 18,944 | 22,272 |
| No peephole | 11,858,777 | 69,141,765 | 20,992 | 24,576 |

Recheck with:

```sh
DCC_MIR_PREFER_REGIONAL=1 python3 scripts/runall.py --apps a1 --mode full
```

## Reporting a bug

Add confirmed open-bug reproducers under [`tests/known-bugs/`](tests/known-bugs/)
with host-proven output baselines, then run
`python3 scripts/run-known-bugs.py`. See the
[`tests/known-bugs` guide](tests/known-bugs/README.md) for the workflow.
