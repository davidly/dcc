---
name: clang-oracle-testing
description: "Find, triage, log, and later close dcc compiler bugs with the clang-oracle differential fuzzer. Use for fuzz campaigns, seed reduction, bug triage, parking tests/known-bugs reproducers, compiler-bugs.md entries, GitHub bug issues, generator steering, forced-MIR-candidate runs, or verifying that a compiler fix really closes a logged bug."
---

# Clang-oracle differential testing for dcc

The host clang compiler is the oracle. `scripts/clang-oracle-fuzz.py`
generates a C program per seed, proves it is defined C on the host (ASan,
UBSan, checked rendering, `-O0`/`-O2` agreement), builds it with dcc in
each configuration, runs it under `ntvcm`, and reports any disagreement. A
report is therefore a dcc defect, never a porting difference.

Background and the method are in
`docs/docs/en/appendix/06-differential-testing.md`. This skill is the
operating procedure. The default posture of a campaign is **log, do not
fix**: every finding becomes a parked reproducer, a `compiler-bugs.md` entry
and a GitHub issue. Fixing is a separate task with its own acceptance test
(last section).

## Tools

| Tool | Role |
| --- | --- |
| `scripts/clang-oracle-fuzz.py` | Driver: generate, prove, build, run, compare, reduce |
| `scripts/clang_oracle_gen.py` | Integer generator; `FEATURES`, `KNOWN_BUGS`, `known_shapes()` |
| `scripts/clang_oracle_float_gen.py` | Float generator (`--gen float`); same structure |
| `scripts/run-known-bugs.py` | Proves each `tests/known-bugs/bugNNN.c` baseline on the host, then reports `fails`/`FIXED` for dcc peep and nopeep |
| `scripts/bug-issue-body.py` | Builds a GitHub issue body from the reproducer, baseline and dcc's current behaviour; `--create` opens it |
| `compiler-bugs.md` | The bug log: one `## BUG-NNN` entry plus a summary-table row per bug |
| `tests/known-bugs/` | Parked reproducers and host baselines; `README.md` has the promote-on-fix steps |

Prerequisites: a built toolchain in the repository root
(`pwsh ./scripts/build-dcc.ps1`), `clang`, `ntvcm` on `PATH`, Python 3.9+.
Scripts use `from __future__ import annotations` for 3.9.

## Campaign procedure

1. **Baseline first.** `python3 scripts/run-known-bugs.py` must report every
   reproducer as `fails` (the count is in `compiler-bugs.md`), and
   `python3 ./scripts/runall.py -mode=full -extended` must print `SUCCESS`.
   If either differs, stop and report; something other than the fuzzer
   changed.
2. **Run a batch** of 300 seeds per generator with the production
   configurations and every logged shape steered out:

   ```sh
   python3 scripts/clang-oracle-fuzz.py --seeds 1-300 --configs production --off known --timeout 6 --jobs 6
   python3 scripts/clang-oracle-fuzz.py --gen float --seeds 1-300 --configs production --off known --timeout 6 --jobs 6
   ```

   About ten minutes each. The last line is the tally
   (`300 programs: FAIL 1, INVALID 29, ok 270`). `INVALID` of 10–25 % is
   normal (the host proof rejected a program); `limit-only` is a target
   limit (stack, 64K, `out of ram`), not a defect.
3. **Triage every FAIL** (next section) before the next batch. Do not file
   from the raw seed; always reduce and confirm.
4. **Stop rule:** two consecutive clean batches per generator *after the
   last generator change*. Any edit to a generator re-randomises every
   seed, so the count restarts. Use a fresh seed range for each batch.
5. **Validate and commit** after each batch's triage: `run-known-bugs.py`
   (expected count of `fails`, zero `FIXED`), `runall.py -mode=full
   -extended` (`SUCCESS`), and
   `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`.

Forced MIR candidates (`--configs all`, or a list from `--list-configs`)
force each candidate emitter with `DCC_MIR_SELECT_CANDIDATE`; roughly ten
times the cost, so use 150-seed batches with `--jobs 3`. A failure that
appears only in a forced configuration is latent, not shipped behaviour;
record it as such.

### Long runs and agents

- A 300-seed batch is too long for a background task agent: harness-level
  agents are cancelled after about fifteen minutes and kill their children.
  Run batches with an asynchronous shell command that redirects output to a
  log under `build/` and read the log when it finishes.
- Keep every scratch program, reduction and log under `build/`; nothing
  there is committed.
- macOS has no GNU `timeout`; the driver has its own `--timeout` and
  `--build-timeout`. For manual probes use a Perl `alarm` wrapper.

## Triage

1. **Reduce** under one configuration, with a short timeout so hangs reduce
   quickly:

   ```sh
   python3 scripts/clang-oracle-fuzz.py --reduce SEED --config nopeep --off known --timeout 3 --reduce-jobs 3 --work build/clang-oracle-red-SEED
   ```

   Result: `build/clang-oracle-red-SEED/SEED-reduce-nopeep/reduced.c`,
   `.expected` (host) and `.actual` (dcc). Use `--config peep` when only the
   peep configuration failed. Add `--gen float` for float seeds.
2. **Confirm by hand** with one-statement variants in one file: change one
   thing at a time (constant vs variable, parameter vs global, block vs
   function scope, operand order, the statement before it) and run both
   compilers. Many float and register-placement bugs depend on context, so
   several variants in one file tell you more than one minimal program.
   Direct compile for a fatal diagnostic:
   `./dcc -ffloatio -I . f.c -o f.MAC`; build and run:
   `./dccmake dcc-input=f.c dcc-output=F dcc-floatio=true dcc-peep=false`
   then `cd build && ntvcm -s:0 F.COM | tr -d '\r'`.
   Probes must avoid logged compile-time bugs (for example implicit
   float→int `return`, BUG-046): use explicit casts or assign to a global.
3. **Classify.** Search `compiler-bugs.md` for the same root shape.
   - Same root cause as a logged bug → a bullet in that entry, a comment on
     its issue, and a steering rule. Not a new bug.
   - Fault in the generator or the host proof (program was not defined C,
     or the renderer did not preserve 16-bit semantics) → fix the fuzzer,
     do not file.
   - Otherwise → new bug.
4. **Park** `tests/known-bugs/bugNNN.c`: failing cases plus passing
   controls, `<stdint.h>` types only, one printed line per shape. Then
   `python3 scripts/run-known-bugs.py --update bugNNN` writes the host
   baseline (it refuses a program ASan or UBSan objects to) and
   `python3 scripts/run-known-bugs.py bugNNN` must say `fails`.
5. **Log** a `## BUG-NNN` entry (shape, minimal program, expected vs actual,
   what neighbouring shapes are right, which configurations) and a
   summary-table row `| [BUG-NNN](#bug-nnn) | #issue | kind | defect |`;
   update the open count and the kind tallies below the table.
6. **File** with `python3 scripts/bug-issue-body.py bugNNN --create` and
   put the issue number in the table row. The tool reads titles and
   cross-references from the summary table, so write the row first. It
   refuses to run if local commits touch `src/` or `DCCRTL.MAC`.
7. **Steer.** Add a feature name to `FEATURES` and `KNOWN_BUGS`
   (`feature → "BUG-NNN"`), a detector in `known_shapes()`, and a rewrite in
   the generator that removes the shape when the feature is off. Edit nodes
   in place. Then prove the steer:

   ```sh
   cd scripts && python3 -c "
   from clang_oracle_gen import generate, known_shapes, KNOWN_BUGS
   assert not any(known_shapes(generate(s, off=frozenset(KNOWN_BUGS))) for s in range(1, 3001))"
   ```

   (`known_shapes` returns the set of logged features still present; use
   `clang_oracle_float_gen` for the float generator.)

   and re-run the failing seed, which must now pass. Prefer a narrow
   detector: steering out more than the bug costs coverage.

Commit the reproducer, baseline, bug-log entry and generator change
together, with the seed in the message. Never commit a compiler or runtime
change on a campaign branch.

## Closing a logged bug

A fix is accepted only when all of these hold:

1. `python3 scripts/run-known-bugs.py bugNNN` reports `FIXED` in peep and
   nopeep, and every other reproducer still reports `fails` (a fix that
   flips an unrelated reproducer must be understood, not just enjoyed).
2. The bug's feature is removed from `KNOWN_BUGS` in the generator and a
   300-seed batch per affected generator with `--off known` is clean. This
   is what proves the facets listed under the entry are covered, not only
   the parked program.
3. `python3 ./scripts/runall.py -mode=full -extended` is `SUCCESS`, and
   `tests/perf_baselines.csv` is not regressed. Baselines are never moved
   to accept a regression.
4. The reproducer is promoted to `tests/<name>.c` with its baseline and a
   `perf_baselines.csv` row, the entry in `compiler-bugs.md` is closed, and
   the issue is closed from the fixing commit. Steps are in
   `tests/known-bugs/README.md`.

Related bugs are grouped by probable root cause in `fix-plan.md`; work one
cluster per branch, and if the first fix in a cluster does not clear its
siblings, stop and report rather than force them together.

## Pitfalls

- A seed names a program only together with the generator version and
  `--tests`, `--size`, `--depth`, `--off`. Keep `reduced.c`, not the seed.
- The int generator never spells `int` or `long`; the renderer adds the
  casts and masks that make 16-bit and 32-bit `int` agree. A shape that
  needs a plain `int` must be expressed through `int16_t`.
- Floats are compared as IEEE bits with `-0` folded to `0`; a one-ulp
  difference is a real finding (BUG-039 is the runtime divider).
- `dccpeep`-only failures reduce with `--config peep`; confirm nopeep is
  right before blaming the peephole optimizer.
- Do not test older compiler builds to date a bug; the maintainer treats
  every finding as long-standing.
