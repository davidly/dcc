# Clang oracle differential testing for dcc

See [campaign procedure](#campaign-procedure), [triage](#triage), and
[closing a logged bug](#closing-a-logged-bug).

The host clang compiler is the oracle. `scripts/clang-oracle-fuzz.py`
generates a C program per seed, proves it is defined C on the host (ASan,
UBSan, checked rendering, `-O0`/`-O2` agreement), builds it with dcc in
each configuration, runs it under `ntvcm`, and reports disagreements. The
frontend generator also checks probe expectations. Confirm a finding before
filing by checking the generator, host proof, build, and target limits.

Background and the method are in
`docs/docs/en/appendix/06-differential-testing.md`. This skill is the
operating procedure for this driver. A discovery campaign ends at triage and
recording. Use the closure procedure when the task includes fixing a logged bug.

## Tools

| Tool | Role |
| --- | --- |
| `scripts/clang-oracle-fuzz.py` | Driver: generate, prove, build, run, compare, reduce |
| `scripts/clang_oracle_gen.py` | Integer generator; `FEATURES`, `KNOWN_BUGS`, `known_shapes()` |
| `scripts/clang_oracle_float_gen.py` | Float generator (`--gen float`); same structure |
| `scripts/clang_oracle_frontend_gen.py` | Preprocessor and parser probes (`--gen frontend`) |
| `scripts/run-known-bugs.py` | Proves each `tests/known-bugs/bugNNN.c` baseline on the host, then reports `fails`/`FIXED` for dcc peep and nopeep |
| `scripts/bug-issue-body.py` | Builds a GitHub issue body from the reproducer, baseline and dcc's current behaviour; `--create` opens it |
| `compiler-bugs.md` | The bug log: one `## BUG-NNN` entry plus a summary-table row per bug |
| `tests/known-bugs/` | Parked reproducers and host baselines; `README.md` has the promote-on-fix steps |

Prerequisites: a built toolchain in the repository root
(`python scripts/build-dcc.py`), `clang`, `ntvcm` on `PATH`, Python 3.9+.
Scripts use `from __future__ import annotations` for 3.9.

## Campaign procedure

1. **Check the setup** with a small batch. For a formal discovery campaign,
   run `python3 scripts/run-known-bugs.py` and
   `python3 scripts/runall.py --mode full --extended` to establish the
   known bug and regression baseline before the full batches.
   Investigate unexpected changes in those results before using them as a baseline.
2. **Run a batch** of 300 seeds per integer or float generator with the production
   configurations and every logged shape steered out:

   ```sh
   python3 scripts/clang-oracle-fuzz.py --seeds 1-300 --configs production --off known --timeout 6 --jobs 6
   python3 scripts/clang-oracle-fuzz.py --gen float --seeds 1-300 --configs production --off known --timeout 6 --jobs 6
   ```

   Integer and float campaigns also run up to eight frontend probes under
   the campaign's `frontend/` work directory. Use `--gen frontend` for a
   focused probe campaign and record its feature settings separately.
   Each campaign ends with a tally
   (`300 programs: FAIL 1, INVALID 29, ok 270`). `INVALID` of 10–25 % is
   normal (the host proof rejected a program); `limit-only` is a target
   limit (stack, 64K, `out of ram`), not a defect.
3. **Triage every FAIL** (next section) before the next batch. Do not file
   from the raw seed; always reduce and confirm.
4. **Stop rule:** two consecutive clean batches per generator *after the
   last generator change*. Any edit to a generator re-randomises every
   seed, so the count restarts. Use a fresh seed range for each batch.
5. **Validate confirmed bug changes** before committing: `run-known-bugs.py`
   (expected count of `fails`, zero `FIXED`), `runall.py -mode=full
   -extended` (`SUCCESS`), and
   `python3 -m unittest discover -s scripts/tests -p 'test_*.py'`.

Forced MIR candidates (`--configs all`, or a list from `--list-configs`)
force each candidate emitter with `DCC_MIR_SELECT_CANDIDATE`; roughly ten
times the cost, so use 150-seed batches with `--jobs 3`. A failure that
appears only in a forced configuration is latent, not shipped behaviour;
record it as such.

### Long runs and agents

- For long batches, use an asynchronous shell command with a log under
  `build/` and monitor the final tally and exit status.
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
   peep configuration failed. Preserve `--gen`, `--tests`, `--size`,
   `--depth`, and `--off` from the original run, along with relevant target
   options. Use `--gen frontend` when reducing a frontend probe.
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
     its issue when authorized, and a steering rule when supported.
   - Fault in the generator or the host proof (program was not defined C,
     or the renderer did not preserve 16-bit semantics) → fix the fuzzer,
     do not file.
   - Otherwise → new bug.
4. **Park host output reproducers** in `tests/known-bugs/bugNNN.c`: failing cases plus passing
   controls, `<stdint.h>` types only, one printed line per shape. Then
   `python3 scripts/run-known-bugs.py --update bugNNN` writes the host
   baseline (it refuses a program ASan or UBSan objects to) and
   `python3 scripts/run-known-bugs.py bugNNN` must say `fails`. For a
   frontend diagnostic that needs a different assertion, preserve the
   original source and headers and use the appropriate compile regression.
5. **Log** a `## BUG-NNN` entry (shape, minimal program, expected vs actual,
   what neighbouring shapes are right, which configurations) and a
   summary-table row `| [BUG-NNN](#bug-nnn) | #issue | kind | defect |`;
   update the open count and the kind tallies below the table.
6. **File when authorized** with `python3 scripts/bug-issue-body.py bugNNN --create` and
   put the issue number in the table row. The tool reads titles and
   cross-references from the summary table, so write the row first. It
   refuses to run if local commits touch `src/` or `DCCRTL.MAC`.
7. **Steer integer or float findings.** Add a feature name to `FEATURES` and `KNOWN_BUGS`
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
together, with the seed in the message, when committing campaign findings.
Use a fix branch for compiler or runtime changes.

## Closing a logged bug

For bugs tracked with host output baselines, verify the fix as follows.

1. `python3 scripts/run-known-bugs.py bugNNN` reports `FIXED` in peep and
   nopeep, and every other reproducer still reports `fails` (a fix that
   flips an unrelated reproducer must be understood, not just enjoyed).
2. The bug's feature is removed from `KNOWN_BUGS` in the generator and a
   300-seed batch per affected generator with `--off known` is clean. This
   is what proves the facets listed under the entry are covered, not only
   the parked program.
3. `python3 ./scripts/runall.py --mode full --extended` is `SUCCESS`, and
   no existing row of `tests/perf_baselines.csv` changes. Everything in
   `tests/` compiles correctly today, so a fix must not alter their emitted
   code; a changed row in either direction means the fix is wider than the
   defect. Baselines are never moved to accept a regression.
4. The reproducer is promoted to `tests/<name>.c` as a new unit test with
   its baseline and a new `perf_baselines.csv` row, the entry in
   `compiler-bugs.md` is closed, and the authorized issue update links
   the fixing commit. Steps are in `tests/known-bugs/README.md`.
5. The fix keeps the output concise and fast: the narrowest change that
   makes the wrong shape right, ideally emitting what the compiler already
   emits for the neighbouring correct shape listed in the entry, not a
   fallback to a slower or larger schedule.

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
