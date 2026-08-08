#!/usr/bin/env python3
"""Bulk MIR fallback-reason acceptance scan (2026-08-08 coverage-first pivot).

Drives DCC_MIR_FORCE_ACCEPT_REASONS (a diagnostic-only compiler env var,
never a production default - see dcc_mir_select.c) across one or more
`fallback_reason` buckets in a single measurement pass, replacing the old
one-function-at-a-time forced-accept investigation loop with one command.

This tool never changes production code. It only measures: run it, read
the reported coverage delta and correctness result, then land a real
structural gate change by hand for whatever the scan proves safe (Phase 1
Step 3 of the pivot plan).

Typical usage
-------------

1. See the current census split by reason (no env var involved):

     python3 scripts/mir-bulk-accept-scan.py --list-reasons \
         --census build/mir-before.tsv

2. Measure a single bucket's correctness (fast signal, ignores
   performance):

     python3 scripts/mir-bulk-accept-scan.py \
         --reasons text-size --baseline build/mir-before.tsv \
         --correctness-only

3. Measure the full cost-only mega-experiment (every reason this project
   has already classified as a pure cost proxy, see the reason table in
   this file) against the full extended correctness gate:

     python3 scripts/mir-bulk-accept-scan.py --all-cost-reasons \
         --baseline build/mir-before.tsv --correctness-only --extended

4. Once a scan comes back 100% correctness-clean, capture the real
   performance delta before landing (no --correctness-only):

     python3 scripts/mir-bulk-accept-scan.py --all-cost-reasons \
         --baseline build/mir-before.tsv --extended

Every run also regenerates an ordinary + stack-check census under the
forced env var and diffs it against --baseline, so you always see exactly
which functions/apps the reason set would newly admit before spending
time on the (much more expensive) runall pass.
"""

from __future__ import annotations

import argparse
import collections
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# 2026-08-08 mega-experiment bisection (mir-text-size-plan.md T434): every
# reason below this comment was force-accepted *individually* against the
# full extended correctness gate (`runall.ps1 -Mode full -Extended`, no
# -NoPerfCheck skip). The original premise - that a "*-cost" fallback
# reason is always a pure cost proxy with zero remaining semantic risk -
# was WRONG for 16 of the 25 reasons originally listed here: each hid a
# real, reproducible correctness bug (wrong output or an infinite loop) in
# a specific, narrow, previously-untested shape, not just a slower-but-
# correct candidate. `block-cse-cost` in particular had already passed
# four rounds of forced-accept A/B (T407/T426/T430/T431) - but only for a
# hand-picked sample, never the reason's *entire* population; forcing all
# of it surfaced a genuine floating-point edge-case bug in tfmaf. Only the
# nine reasons in PROVEN_COST_ONLY_REASONS came back 100% clean both
# individually and combined (checked for cascading interaction, since
# forcing one reason can change candidate-selection retry order for an
# unrelated, already-accepted function elsewhere in the same file - see
# the tlngnarw finding in T434). Those nine are now landed permanently in
# dcc_mir_select.c's mir_reason_is_proven_cost_only() and will no longer
# appear as fallback reasons at all - they are kept here for the history,
# not as scan candidates.
#
# The moral: do not add a reason to PROVEN_COST_ONLY_REASONS without
# running the full reason alone AND combined with the rest of the proven
# set against the full extended gate first. There is no shortcut around
# this per-reason verification cost; see mir-text-size-plan.md T434 for
# the complete bisection log and root-cause notes for each confirmed-bad
# reason.
PROVEN_COST_ONLY_REASONS = [
    "absolute-address-cost",
    "constant-conversion-frame-cost",
    "rhs-stack-cost",
    "branch-condition-cost",
    "indirect-store-stack-cost",
    "lazy-parameter-cost",
    "dynamic-index-cost",
    "rematerialized-home-cost",
    "stable-pointer-local-cost",
]

# Reasons this project once believed were pure cost proxies but which the
# 2026-08-08 mega-experiment bisection (T434) proved hide a genuine
# correctness bug for some shape within the reason's population. Treated
# identically to ARCHITECTURE_REASONS below: refuse bulk acceptance
# without an explicit override, and expect the same forced-A/B-per-shape
# rigor as any other semantic gate, not a blanket relaxation.
CONFIRMED_UNSAFE_COST_REASONS = {
    "text-size": "T437-T450 admitted all but ts32.main after mulmod, "
                 "PHI-slot, pointer-array, wide-forward, and high-call "
                 "oversized validation. ts32 still has real narrow/wide "
                 "shift failures.",
    "boolean-phi-cost": "T436/T448/T452/T456 admitted acyclic, small-loop, "
                 "medium-loop, and repaired call-containing <=20-block "
                 "strata. Remaining larger/direct-failure and combination-"
                 "sensitive interpreter shapes still produce wrong output "
                 "or CP/M resource exhaustion when forced.",
    "phi-fallthrough-cost": "T455 fixed real-edge PHI copy ownership, "
                 "NOP-only arm duplication, typed signedness/narrowing "
                 "aliases, and fused wide-constant operands; the complete "
                 "terminal reason is now admitted.",
    "dynamic-index-base-cost": "T440/T451/T453/T454 admitted 69 terminal "
                 "functions across bounded acyclic, scalar-loop, wide, and VLA strata. Remaining "
                 "wide/backedge, label-PHI, VLA, pointer/inline, oversized, "
                 "or later-retry shapes include real failures.",
    "unary-not-cost": "T441/T452/T457 admitted terminal acyclic, call-free "
                 "loop, and repaired <=6-block strata. The remaining larger "
                 "backedge, label-PHI, pointer/inline, oversized, or later-"
                 "retry interpreter shapes include real failures.",
    "wide-constant-cost": "T442/T454 admitted the complete terminal reason. "
                 "Blind reason forcing still intercepts tpfauto.main before "
                 "its true block-cse-cost retry.",
    "wide-store-cost": "T443/T453 admitted 18 terminal acyclic "
                 "call-containing functions. Remaining call-free, backedge, "
                 "label-PHI, oversized, or later-retry shapes include pint "
                 "stack pressure and ttrig loop failures.",
    "dead-local-suffix-cost": "T444/T458 admitted every current terminal "
                 "candidate except the unique >20-block, <=1-call failure.",
    "absolute-index-cost": "T444/T459 admitted the complete terminal reason. "
                 "Blind forcing still intercepts unsafe transient pint "
                 "boolean-PHI and tstructv block-CSE retries.",
    "planned-index-base-cost": "T444 admitted 10 terminal bounded acyclic "
                 "functions; unsafe loop/resource shapes remain.",
    "planned-stack-cost": "T444 admitted 8 terminal bounded acyclic "
                 "functions; wumpus/later-retry shapes remain.",
    "binary-load-pair-cost": "T445/T448 admitted 10 functions after fixing "
                 "computed PHI source slots. The single-call pint.emit "
                 "resource stratum remains.",
    "indirect-store-address-cost": "T445 admitted 6 terminal bounded "
                 "acyclic functions; tlngnarw/loop shapes remain.",
    "dead-store-forwarding-cost": "T445 admitted 2 terminal bounded "
                 "acyclic functions; interpreter loop failures remain.",
    "constant-conversion-home-cost": "T445 admitted 1 terminal bounded "
                 "acyclic function; tregnarw/later strata remain.",
    "block-cse-cost": "T450 admitted 29 post-PHI functions below 10 KiB "
                 "outside the unique wide/20-call tpfauto failure stratum. "
                 "tpfauto remains a real semantic failure; the other residue "
                 "is oversized.",
}

# Deprecated alias kept for any external caller still importing the old
# name; equals the union of both proven-safe and confirmed-unsafe reasons
# above plus "instruction-count" (never individually re-verified). Do not
# add new reasons here - classify them into one of the two tables above
# after running the real bisection, not by assumption.
COST_ONLY_REASONS = PROVEN_COST_ONLY_REASONS + list(
    CONFIRMED_UNSAFE_COST_REASONS.keys()) + ["instruction-count"]

# Reasons known NOT to be pure cost proxies - a genuine correctness or
# feature-completeness gap. --all-cost-reasons will refuse to include
# these even if accidentally listed; use --reasons explicitly (and expect
# real investigation, not a bulk scan) if you believe one of these has
# been reclassified.
ARCHITECTURE_REASONS = {
    "selector",
    "pointer-array",
    "cfg-backedge",
    "inline-substitution",
} | set(CONFIRMED_UNSAFE_COST_REASONS.keys())


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    print(f"+ {' '.join(cmd)}", file=sys.stderr)
    return subprocess.run(cmd, cwd=REPO_ROOT, env=env, text=True)


def read_census_reason_counts(path: Path) -> collections.Counter:
    counts: collections.Counter = collections.Counter()
    if not path.exists():
        return counts
    with path.open(newline="") as handle:
        header = handle.readline()
        columns = header.rstrip("\n").split("\t")
        try:
            reason_idx = columns.index("reason")
            result_idx = columns.index("result")
        except ValueError:
            return counts
        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if len(fields) <= max(reason_idx, result_idx):
                continue
            if fields[result_idx] != "fallback":
                continue
            counts[fields[reason_idx]] += 1
    return counts


def cmd_list_reasons(args: argparse.Namespace) -> int:
    counts = read_census_reason_counts(Path(args.census))
    total = sum(counts.values())
    cost_total = 0
    print(f"{'reason':<36}{'count':>8}  class")
    for reason, count in counts.most_common():
        if reason in PROVEN_COST_ONLY_REASONS:
            cls = "proven-safe (landed)"
        elif reason in CONFIRMED_UNSAFE_COST_REASONS:
            cls = "CONFIRMED-UNSAFE (T434)"
        elif reason in ARCHITECTURE_REASONS:
            cls = "ARCHITECTURE"
        else:
            cls = "unclassified"
        if reason in PROVEN_COST_ONLY_REASONS:
            cost_total += count
        print(f"{reason:<36}{count:>8}  {cls}")
    print(f"\ntotal fallback: {total}, proven cost-only: {cost_total} "
          f"({100.0 * cost_total / total:.1f}%)" if total else "no fallback rows")
    return 0


def resolve_reasons(args: argparse.Namespace) -> list[str]:
    if args.all_cost_reasons:
        reasons = list(PROVEN_COST_ONLY_REASONS)
    elif args.reasons:
        reasons = [r.strip() for r in args.reasons.split(",") if r.strip()]
    else:
        raise SystemExit("specify --reasons or --all-cost-reasons")
    bad = [r for r in reasons if r in ARCHITECTURE_REASONS]
    if bad and not args.allow_architecture_reasons:
        raise SystemExit(
            f"refusing to bulk-accept known architecture reason(s) {bad} "
            "without --allow-architecture-reasons (these need a real fix, "
            "not gate relaxation - see mir-text-size-plan.md)"
        )
    return reasons


def cmd_scan(args: argparse.Namespace) -> int:
    reasons = resolve_reasons(args)
    reason_list = ",".join(reasons)
    print(f"=== scanning {len(reasons)} reason(s): {reason_list} ===")

    env = dict(os.environ)
    env["DCC_MIR_FORCE_ACCEPT_REASONS"] = reason_list

    build_dir = Path(args.scratch)
    build_dir.mkdir(parents=True, exist_ok=True)
    ordinary_out = build_dir / "scan-ordinary.tsv"
    stack_out = build_dir / "scan-stack.tsv"

    census_cmd = [
        "python3", "scripts/mir-migration-census.py",
        "--output", str(ordinary_out),
    ]
    if args.baseline:
        census_cmd += ["--compare", args.baseline]
    result = run(census_cmd, env=env)
    if result.returncode != 0 and not args.baseline:
        print("ordinary census failed", file=sys.stderr)
        return result.returncode

    stack_cmd = [
        "python3", "scripts/mir-migration-census.py",
        "--extra-args=-fstack-check",
        "--output", str(stack_out),
    ]
    if args.stack_baseline:
        stack_cmd += ["--compare", args.stack_baseline]
    run(stack_cmd, env=env)

    if args.census_only:
        return 0

    runall_cmd = ["pwsh", "./scripts/runall.ps1", "-Mode", "full",
                  "-RunTimeout", str(args.run_timeout)]
    if args.extended:
        runall_cmd.append("-Extended")
    if args.apps:
        runall_cmd += ["-Apps", args.apps]
    if args.correctness_only:
        runall_cmd.append("-NoPerfCheck")

    result = run(runall_cmd, env=env)
    return result.returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--census", default="build/mir-before.tsv",
                         help="existing ordinary census to summarize "
                              "(--list-reasons only)")
    parser.add_argument("--list-reasons", action="store_true",
                         help="print the current fallback-reason "
                              "histogram from --census and exit")
    parser.add_argument("--reasons",
                         help="comma-separated fallback_reason tokens to "
                              "force-accept")
    parser.add_argument("--all-cost-reasons", action="store_true",
                         help="use PROVEN_COST_ONLY_REASONS (the nine "
                              "reasons already landed permanently as of "
                              "T434 - this is now a no-op measurement "
                              "since they no longer appear as fallback; "
                              "kept for regression-checking the landed "
                              "gate). Use --reasons explicitly to "
                              "re-investigate any CONFIRMED_UNSAFE_COST_"
                              "REASONS entry with --allow-architecture-"
                              "reasons.")
    parser.add_argument("--allow-architecture-reasons", action="store_true",
                         help="override the refusal to bulk-accept a "
                              "known architecture reason (selector, "
                              "pointer-array, cfg-backedge, "
                              "inline-substitution)")
    parser.add_argument("--baseline",
                         help="ordinary census tsv to diff the scan "
                              "against (pass --fail-on-regression is not "
                              "used here; this is informational)")
    parser.add_argument("--stack-baseline",
                         help="stack-check census tsv to diff against")
    parser.add_argument("--scratch", default="build/mir-bulk-accept-scan",
                         help="directory for scan output tsvs")
    parser.add_argument("--census-only", action="store_true",
                         help="only run the two census passes, skip "
                              "runall.ps1")
    parser.add_argument("--correctness-only", action="store_true",
                         help="pass -NoPerfCheck to runall.ps1 (fast "
                              "correctness-only signal, per amended "
                              "Phase 1 policy)")
    parser.add_argument("--extended", action="store_true",
                         help="pass -Extended to runall.ps1")
    parser.add_argument("--apps",
                         help="restrict runall.ps1 to a comma-separated "
                              "app list instead of the full corpus")
    parser.add_argument("--run-timeout", type=int, default=30)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list_reasons:
        return cmd_list_reasons(args)
    return cmd_scan(args)


if __name__ == "__main__":
    raise SystemExit(main())
