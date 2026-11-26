# Known-bug reproducers

There are currently no open known-bug reproducers in this directory. The
reproducers have been fixed and promoted to the regular test suite. Current
status is summarized in [compiler-bugs.md](../../compiler-bugs.md); the fix
history is in [fix-plan.md](../../fix-plan.md).

## Adding a reproducer

When a new compiler bug is confirmed, add `bugNNN.c` here and its expected
host-compiler output at `baselines/bugNNN.txt`. Use fixed-width types so output
does not depend on the host's `int` size. Prove the baseline with the host
compiler and sanitizers by running:

```sh
python3 scripts/run-known-bugs.py --update bugNNN
python3 scripts/run-known-bugs.py bugNNN
```

The runner checks dcc with and without `dccpeep`. A reproducer should fail in
both configurations until the bug is fixed. For a diagnostic-masking bug, add
`DCC-EXPECT-ERROR` to the source header; the runner then requires the expected
primary diagnostic.

Once fixed, promote the reproducer to a regular test under `tests/`, add its
performance baseline, and update `compiler-bugs.md` and the oracle generator.
See [tests/README.md](../README.md) for the regular test workflow.
