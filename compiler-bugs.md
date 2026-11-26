# dcc compiler bugs

## Current status

There are no open production compiler bugs in this checkout. Fixed findings
are covered by the regular tests under [`tests/`](tests/) and the known-bug
runner has no parked reproducers. The detailed fix history is in
[`fix-plan.md`](fix-plan.md).

The full suite (654 apps) passes in both stack modes on Linux and Windows,
with no performance regressions in the default configuration.

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
