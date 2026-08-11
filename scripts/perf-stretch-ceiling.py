#!/usr/bin/env python3
"""Rank profiled apps by feasibility of a main-relative cycle target."""

from __future__ import annotations

import argparse
import csv
import io
import re
import subprocess
from pathlib import Path


TOTAL_RE = re.compile(r"Total cycles counted: \*\*(\d+)\*\*")
ROW_RE = re.compile(
    r"^\|\s*(\d+)\s*\|\s*[^|]+\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|$"
)


def load_csv(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return {row["app"]: row for row in csv.DictReader(stream)}


def load_reference(ref: str) -> dict[str, dict[str, str]]:
    text = subprocess.check_output(
        ["git", "show", f"{ref}:tests/perf_baselines.csv"],
        text=True,
    )
    return {row["app"]: row for row in csv.DictReader(io.StringIO(text))}


def load_profile(path: Path, app: str) -> tuple[int, int] | None:
    text = path.read_text(encoding="utf-8")
    total_match = TOTAL_RE.search(text)
    if total_match is None:
        return None
    total = int(total_match.group(1))
    module_name = f"{app}.mac"
    app_cycles = 0
    for line in text.splitlines():
        match = ROW_RE.match(line)
        if match is None:
            continue
        if match.group(2).strip().lower() == module_name.lower():
            app_cycles += int(match.group(1))
    return total, app_cycles


def target_required_fraction(current: int, reference: int, target: float) -> float:
    target_cycles = reference * (1.0 - target)
    return max(0.0, (current - target_cycles) / current)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--current-file",
        type=Path,
        default=Path("tests/perf_baselines.csv"),
    )
    parser.add_argument("--reference", default="main")
    parser.add_argument(
        "--profile-root", type=Path, default=Path("build/dccprof")
    )
    parser.add_argument(
        "--target-percent",
        type=float,
        default=10.0,
        help="Required improvement relative to the reference.",
    )
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    current = load_csv(args.current_file)
    reference = load_reference(args.reference)
    target = args.target_percent / 100.0
    rows: list[dict[str, object]] = []

    for summary in sorted(args.profile_root.glob("*/*_profile_summary.md")):
        app = summary.name.removesuffix("_profile_summary.md")
        if summary.parent.name != app:
            continue
        if app not in current or app not in reference:
            continue
        profile = load_profile(summary, app)
        if profile is None:
            continue
        total, app_cycles = profile
        peep_current = int(current[app]["peep_cycles"])
        nopeep_current = int(current[app]["nopeep_cycles"])
        peep_reference = int(reference[app]["peep_cycles"])
        nopeep_reference = int(reference[app]["nopeep_cycles"])
        peep_required = target_required_fraction(
            peep_current, peep_reference, target
        )
        nopeep_required = target_required_fraction(
            nopeep_current, nopeep_reference, target
        )
        required = max(peep_required, nopeep_required)
        app_share = app_cycles / total if total else 0.0
        rows.append(
            {
                "app": app,
                "peep_vs_ref_percent": 100.0
                * (peep_current / peep_reference - 1.0),
                "nopeep_vs_ref_percent": 100.0
                * (nopeep_current / nopeep_reference - 1.0),
                "required_percent": 100.0 * required,
                "app_code_percent": 100.0 * app_share,
                "headroom_percent": 100.0 * (app_share - required),
                "feasible": app_share >= required,
                "profile_cycles": total,
                "app_cycles": app_cycles,
            }
        )

    rows.sort(
        key=lambda row: (
            not bool(row["feasible"]),
            -float(row["required_percent"]),
            -float(row["app_code_percent"]),
        )
    )
    print(
        f"Profiled {len(rows)} apps; target {args.target_percent:.1f}% "
        f"faster than {args.reference}"
    )
    print(
        f"{'app':16} {'peep%':>8} {'nopeep%':>8} {'need%':>8} "
        f"{'app-code%':>10} {'headroom%':>10} {'feasible':>9}"
    )
    for row in rows[: args.limit]:
        print(
            f"{row['app']:16} "
            f"{row['peep_vs_ref_percent']:8.2f} "
            f"{row['nopeep_vs_ref_percent']:8.2f} "
            f"{row['required_percent']:8.2f} "
            f"{row['app_code_percent']:10.2f} "
            f"{row['headroom_percent']:10.2f} "
            f"{'yes' if row['feasible'] else 'no':>9}"
        )
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else [])
            if rows:
                writer.writeheader()
                writer.writerows(rows)
        print(f"\nWrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
