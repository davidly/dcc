#!/usr/bin/env python3
"""Measure staged MIR rollout and generate focused validation commands."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

SELECTION_RE = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=(?P<result>\S+) "
    r"reason=(?P<reason>\S+) generated-bytes=(?P<generated_bytes>-?\d+) "
    r"captured-bytes=(?P<captured_bytes>-?\d+) "
    r"generated-insns=(?P<generated_insns>-?\d+) "
    r"captured-insns=(?P<captured_insns>-?\d+) blocks=(?P<blocks>\d+)"
)
FIELDS = [
    "app",
    "function",
    "selector",
    "result",
    "reason",
    "generated_bytes",
    "captured_bytes",
    "generated_insns",
    "captured_insns",
    "blocks",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile test apps with MIR selection reporting, write a "
        "stable census, and compare it with an earlier snapshot."
    )
    parser.add_argument("--compiler", default="./dcc")
    parser.add_argument("--tests-dir", default="tests")
    parser.add_argument("--output", default="build/mir-migration-census.tsv")
    parser.add_argument("--compare", metavar="OLD_TSV")
    parser.add_argument("--apps", help="comma-separated app names")
    parser.add_argument(
        "--include-ignored",
        action="store_true",
        help="include apps marked ignore in tests/_test_overrides.json",
    )
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--stack", type=int, default=512)
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="exit nonzero if a previously MIR-emitted function falls back",
    )
    return parser.parse_args()


def load_overrides(tests_dir: Path) -> dict[str, dict[str, object]]:
    path = tests_dir / "_test_overrides.json"
    if not path.is_file():
        return {}
    with path.open(encoding="utf-8") as handle:
        data = json.load(handle)
    return {entry["name"]: entry for entry in data.get("apps", [])}


def selected_sources(
    tests_dir: Path,
    apps: str | None,
    overrides: dict[str, dict[str, object]],
    include_ignored: bool,
) -> list[Path]:
    sources = {path.stem: path for path in tests_dir.glob("*.c")}
    if not include_ignored:
        sources = {
            name: path
            for name, path in sources.items()
            if not overrides.get(name, {}).get("ignore", False)
        }
    if not apps:
        return [sources[name] for name in sorted(sources)]
    requested = [name.strip().lower() for name in apps.split(",") if name.strip()]
    missing = [name for name in requested if name not in sources]
    if missing:
        raise ValueError("unknown app(s): " + ", ".join(missing))
    return [sources[name] for name in requested]


def compile_source(
    compiler: str,
    source: Path,
    output: Path,
    stack: int,
    timeout: int,
    extra_args: list[str],
) -> tuple[list[dict[str, str]], str | None]:
    env = os.environ.copy()
    env["DCC_MIR_SELECT_REPORT"] = "1"
    command = [
        compiler,
        "-stack",
        str(stack),
        "-I",
        ".",
        *extra_args,
        str(source),
        "-o",
        str(output),
    ]
    try:
        completed = subprocess.run(
            command,
            env=env,
            text=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return [], f"timed out after {timeout}s"
    if completed.returncode != 0:
        detail = completed.stderr.strip().splitlines()
        return [], detail[-1] if detail else f"compiler exited {completed.returncode}"

    rows: list[dict[str, str]] = []
    seen: set[str] = set()
    for line in completed.stderr.splitlines():
        match = SELECTION_RE.search(line)
        if not match:
            continue
        values = match.groupdict()
        function = values["function"]
        # Verify/deferred/final passes can report the same source function.
        # Keep the first report, matching the historical shell census.
        if function in seen:
            continue
        seen.add(function)
        rows.append({"app": source.stem, **values})
    return rows, None


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(sorted(rows, key=lambda row: (row["app"], row["function"])))


def read_rows(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        return {(row["app"], row["function"]): row for row in reader}


def print_summary(rows: list[dict[str, str]]) -> None:
    outcomes = collections.Counter((row["result"], row["reason"]) for row in rows)
    selectors = collections.Counter(row["selector"] for row in rows)
    print("\nMIR outcomes")
    for (result, reason), count in sorted(
        outcomes.items(), key=lambda item: (-item[1], item[0])
    ):
        print(f"  {count:5d}  {result:8s} {reason}")
    print("\nFinal selectors")
    for selector, count in selectors.most_common():
        print(f"  {count:5d}  {selector}")
    emitted = sum(row["result"] == "mir" for row in rows)
    percent = emitted * 100.0 / len(rows) if rows else 0.0
    print(f"\nCoverage: {emitted}/{len(rows)} functions ({percent:.2f}%)")


def compare_rows(
    old: dict[tuple[str, str], dict[str, str]],
    new: dict[tuple[str, str], dict[str, str]],
) -> tuple[set[str], int]:
    newly_mir: list[tuple[str, str]] = []
    regressed: list[tuple[str, str]] = []
    changed_apps: set[str] = set()
    compared_apps = {app for app, _ in new}

    for key in sorted(
        key for key in old.keys() | new.keys() if key[0] in compared_apps
    ):
        before = old.get(key)
        after = new.get(key)
        if before == after:
            continue
        changed_apps.add(key[0])
        before_result = before["result"] if before else "missing"
        after_result = after["result"] if after else "missing"
        if before_result != "mir" and after_result == "mir":
            newly_mir.append(key)
        elif before_result == "mir" and after_result != "mir":
            regressed.append(key)

    print("\nSnapshot delta")
    print(f"  newly MIR-emitted: {len(newly_mir)}")
    for app, function in newly_mir:
        print(f"    + {app}.{function}")
    print(f"  no longer MIR-emitted: {len(regressed)}")
    for app, function in regressed:
        print(f"    - {app}.{function}")
    print(f"  affected apps: {len(changed_apps)}")
    if changed_apps:
        app_list = ",".join(sorted(changed_apps))
        print("\nFocused validation")
        print(
            "  pwsh ./scripts/runall.ps1 "
            f"-Apps {app_list} -Mode full -RunTimeout 20"
        )
    return changed_apps, len(regressed)


def main() -> int:
    args = parse_args()
    tests_dir = Path(args.tests_dir)
    output_path = Path(args.output)
    overrides = load_overrides(tests_dir)
    try:
        sources = selected_sources(
            tests_dir, args.apps, overrides, args.include_ignored
        )
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    rows: list[dict[str, str]] = []
    failures: list[tuple[str, str]] = []
    with tempfile.TemporaryDirectory(prefix="dcc-mir-census-") as directory:
        assembly = Path(directory) / "census.mac"
        for index, source in enumerate(sources, 1):
            app_rows, error = compile_source(
                args.compiler,
                source,
                assembly,
                args.stack,
                args.timeout,
                shlex.split(str(overrides.get(source.stem, {}).get("dcc_args", ""))),
            )
            if error:
                failures.append((source.stem, error))
            else:
                rows.extend(app_rows)
            print(f"\r[{index:3d}/{len(sources)}] {source.stem:12s}", end="", flush=True)
    print()

    if failures:
        for app, error in failures:
            print(f"error: {app}: {error}", file=sys.stderr)
        return 1

    write_rows(output_path, rows)
    print(f"Wrote {output_path}")
    print_summary(rows)

    regressions = 0
    if args.compare:
        old_path = Path(args.compare)
        if not old_path.is_file():
            print(f"error: comparison snapshot not found: {old_path}", file=sys.stderr)
            return 2
        _, regressions = compare_rows(read_rows(old_path), read_rows(output_path))

    return 1 if args.fail_on_regression and regressions else 0


if __name__ == "__main__":
    raise SystemExit(main())
