#!/usr/bin/env python3
"""Compare checked performance baselines with the same file at a git ref."""

from __future__ import annotations

import argparse
import csv
import io
from pathlib import Path
import subprocess
import sys

FIELDS = ("peep_cycles", "nopeep_cycles", "peep_size", "nopeep_size")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Rank per-app performance changes against a git reference."
    )
    parser.add_argument(
        "--reference",
        default="main",
        help="git reference containing the comparison baseline (default: main)",
    )
    parser.add_argument(
        "--baseline-file",
        default="tests/perf_baselines.csv",
        help="baseline CSV path in the worktree and reference",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=2.0,
        help="regression percentage highlighted in the summary (default: 2)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=40,
        help="maximum ranked rows to print; 0 prints every row (default: 40)",
    )
    parser.add_argument(
        "--output",
        metavar="CSV",
        help="also write the complete ranked comparison to this CSV",
    )
    parser.add_argument(
        "--fail-over-threshold",
        action="store_true",
        help="exit nonzero when any app exceeds --threshold in peep or nopeep",
    )
    return parser.parse_args()


def read_baselines(handle: io.TextIOBase) -> dict[str, dict[str, int]]:
    baselines: dict[str, dict[str, int]] = {}
    for row in csv.DictReader(handle):
        app = row.get("app", "")
        if not app:
            continue
        try:
            baselines[app] = {field: int(row[field]) for field in FIELDS}
        except (KeyError, ValueError) as error:
            raise ValueError(f"invalid baseline row for {app}: {error}") from error
    return baselines


def load_worktree(path: Path) -> dict[str, dict[str, int]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return read_baselines(handle)


def load_reference(reference: str, path: str) -> dict[str, dict[str, int]]:
    completed = subprocess.run(
        ["git", "show", f"{reference}:{path}"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or f"git show exited {completed.returncode}"
        raise ValueError(detail)
    return read_baselines(io.StringIO(completed.stdout))


def percent(actual: int, baseline: int) -> float:
    return (actual - baseline) * 100.0 / baseline if baseline else 0.0


def build_rows(
    reference: dict[str, dict[str, int]],
    current: dict[str, dict[str, int]],
) -> tuple[list[dict[str, object]], list[str], list[str]]:
    rows: list[dict[str, object]] = []
    common = sorted(reference.keys() & current.keys())
    for app in common:
        before = reference[app]
        after = current[app]
        rows.append(
            {
                "app": app,
                "peep_cycles_ref": before["peep_cycles"],
                "peep_cycles_current": after["peep_cycles"],
                "peep_cycles_percent": percent(
                    after["peep_cycles"], before["peep_cycles"]
                ),
                "nopeep_cycles_ref": before["nopeep_cycles"],
                "nopeep_cycles_current": after["nopeep_cycles"],
                "nopeep_cycles_percent": percent(
                    after["nopeep_cycles"], before["nopeep_cycles"]
                ),
                "peep_size_percent": percent(
                    after["peep_size"], before["peep_size"]
                ),
                "nopeep_size_percent": percent(
                    after["nopeep_size"], before["nopeep_size"]
                ),
            }
        )
    rows.sort(
        key=lambda row: (
            -max(
                float(row["peep_cycles_percent"]),
                float(row["nopeep_cycles_percent"]),
            ),
            str(row["app"]),
        )
    )
    return (
        rows,
        sorted(current.keys() - reference.keys()),
        sorted(reference.keys() - current.keys()),
    )


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]) if rows else ["app"])
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    try:
        reference = load_reference(args.reference, args.baseline_file)
        current = load_worktree(Path(args.baseline_file))
        rows, added, removed = build_rows(reference, current)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    over = [
        row
        for row in rows
        if max(
            float(row["peep_cycles_percent"]),
            float(row["nopeep_cycles_percent"]),
        )
        > args.threshold
    ]
    improved = [
        row
        for row in rows
        if min(
            float(row["peep_cycles_percent"]),
            float(row["nopeep_cycles_percent"]),
        )
        < -args.threshold
    ]

    print(
        f"Compared {len(rows)} apps: current {args.baseline_file} vs "
        f"{args.reference}"
    )
    print(
        f"Cycle regressions > {args.threshold:g}%: {len(over)}; "
        f"improvements > {args.threshold:g}%: {len(improved)}; "
        f"new: {len(added)}; removed: {len(removed)}"
    )
    if added:
        print("New apps: " + ", ".join(added))
    if removed:
        print("Removed apps: " + ", ".join(removed))

    print(
        "\n"
        f"{'app':16s} {'peep%':>9s} {'nopeep%':>9s} "
        f"{'peep-size%':>11s} {'nopeep-size%':>13s}"
    )
    visible = rows if args.limit == 0 else rows[: max(args.limit, 0)]
    for row in visible:
        print(
            f"{str(row['app']):16s} "
            f"{float(row['peep_cycles_percent']):8.2f}% "
            f"{float(row['nopeep_cycles_percent']):8.2f}% "
            f"{float(row['peep_size_percent']):10.2f}% "
            f"{float(row['nopeep_size_percent']):12.2f}%"
        )

    if args.output:
        write_rows(Path(args.output), rows)
        print(f"\nWrote {args.output}")

    return 1 if args.fail_over_threshold and over else 0


if __name__ == "__main__":
    raise SystemExit(main())
