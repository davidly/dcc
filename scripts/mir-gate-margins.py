#!/usr/bin/env python3
"""Rank MIR fallback candidates by how close they are to admission.

Consumes the TSV produced by scripts/mir-migration-census.py and, for each
distinct fallback `reason` bucket, sorts the declined candidates by their
raw generated-vs-captured instruction margin. This is a generic, gate-agnostic
proxy for "how close is this candidate to being admitted" -- it does not
duplicate any of the ~20 distinct per-gate profitability formulas in
dcc_mir_select.c (each has its own multiplier/margin, e.g. `*25 <= *24`,
`- 15`, `+ 5`, ...). Those formulas can drift out of sync with a hard-coded
copy; a smaller raw instruction delta reliably means "closer to whatever the
real gate requires" without needing to know the gate's exact arithmetic.

Usage:
    python3 scripts/mir-gate-margins.py census.tsv [--top N] [--reason NAME]
    python3 scripts/mir-gate-margins.py census.tsv --min-population 10
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tsv", help="census TSV produced by mir-migration-census.py")
    parser.add_argument(
        "--top", type=int, default=5,
        help="closest N candidates to print per reason bucket (default: 5)",
    )
    parser.add_argument(
        "--reason", action="append", default=None,
        help="only report this reason bucket (repeatable); default: all fallback buckets",
    )
    parser.add_argument(
        "--min-population", type=int, default=1,
        help="skip reason buckets with fewer than this many candidates (default: 1)",
    )
    parser.add_argument(
        "--accepted-reason", default="accepted",
        help="reason value marking an already-accepted row to exclude (default: accepted)",
    )
    return parser.parse_args()


def load_rows(path: str) -> list[dict[str, str]]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def main() -> int:
    args = parse_args()
    rows = load_rows(args.tsv)

    buckets: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        if row.get("result") == "mir" or row.get("reason") == args.accepted_reason:
            continue
        try:
            gen = int(row["generated_insns"])
            cap = int(row["captured_insns"])
        except (KeyError, ValueError):
            continue
        if cap <= 0:
            continue
        row["_delta"] = gen - cap
        row["_ratio"] = gen / cap
        buckets[row["reason"]].append(row)

    if args.reason:
        wanted = set(args.reason)
        buckets = {k: v for k, v in buckets.items() if k in wanted}

    # Order buckets by population descending, then by name for determinism.
    ordered_reasons = sorted(buckets.keys(), key=lambda r: (-len(buckets[r]), r))

    for reason in ordered_reasons:
        candidates = buckets[reason]
        if len(candidates) < args.min_population:
            continue
        candidates.sort(key=lambda r: (r["_delta"], r["_ratio"]))
        print(f"== {reason} ({len(candidates)} candidates) ==")
        for row in candidates[: args.top]:
            print(
                "  {app}:{function}  gen={gen} cap={cap} delta={delta:+d} "
                "ratio={ratio:.3f} blocks={blocks} selector={selector}".format(
                    app=row["app"],
                    function=row["function"],
                    gen=row["generated_insns"],
                    cap=row["captured_insns"],
                    delta=row["_delta"],
                    ratio=row["_ratio"],
                    blocks=row.get("blocks", "?"),
                    selector=row.get("selector", "?"),
                )
            )
        print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
