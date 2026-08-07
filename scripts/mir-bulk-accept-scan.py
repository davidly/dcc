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

# Reasons this project's investigation history (mir-text-size-plan.md,
# T407/T421/T426/T430/T431 for block-cse-cost; the 2026-08-08 pivot census
# for everything else) has classified as a *pure cost proxy*: the
# candidate already lowered correctly through MIR's own selector and was
# rejected only for being judged bigger/slower than legacy, never for a
# semantic/structural reason. Kept here as the single source of truth so
# every caller of --all-cost-reasons agrees on the same list; update this
# list (and cite the investigation) if a reason is reclassified.
COST_ONLY_REASONS = [
    "text-size",
    "boolean-phi-cost",
    "block-cse-cost",
    "dynamic-index-base-cost",
    "unary-not-cost",
    "wide-constant-cost",
    "phi-fallthrough-cost",
    "wide-store-cost",
    "dead-local-suffix-cost",
    "absolute-address-cost",
    "absolute-index-cost",
    "planned-index-base-cost",
    "constant-conversion-frame-cost",
    "planned-stack-cost",
    "rhs-stack-cost",
    "binary-load-pair-cost",
    "indirect-store-address-cost",
    "branch-condition-cost",
    "indirect-store-stack-cost",
    "dead-store-forwarding-cost",
    "lazy-parameter-cost",
    "constant-conversion-home-cost",
    "dynamic-index-cost",
    "instruction-count",
    "rematerialized-home-cost",
    "stable-pointer-local-cost",
]

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
}


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
        cls = (
            "cost-only" if reason in COST_ONLY_REASONS
            else "ARCHITECTURE" if reason in ARCHITECTURE_REASONS
            else "unclassified"
        )
        if reason in COST_ONLY_REASONS:
            cost_total += count
        print(f"{reason:<36}{count:>8}  {cls}")
    print(f"\ntotal fallback: {total}, cost-only: {cost_total} "
          f"({100.0 * cost_total / total:.1f}%)" if total else "no fallback rows")
    return 0


def resolve_reasons(args: argparse.Namespace) -> list[str]:
    if args.all_cost_reasons:
        reasons = list(COST_ONLY_REASONS)
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
                         help="use the built-in COST_ONLY_REASONS list")
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
