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
from concurrent.futures import ThreadPoolExecutor, as_completed

SELECTION_RE = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=(?P<result>\S+) "
    r"reason=(?P<reason>\S+) generated-bytes=(?P<generated_bytes>-?\d+) "
    r"captured-bytes=(?P<captured_bytes>-?\d+) "
    r"generated-insns=(?P<generated_insns>-?\d+) "
    r"captured-insns=(?P<captured_insns>-?\d+) blocks=(?P<blocks>\d+) "
    r"selected-hash=(?P<selected_hash>[0-9a-fA-F]{8})"
)
MATRIX_RE = re.compile(
    r"MIR candidate-matrix\tfunction=(?P<function>\S+)"
    r"\tcandidate=(?P<candidate>\S+)\tmask=(?P<mask>[0-9a-fA-F]{8})"
    r"\temitted=(?P<emitted>[01])\treason=(?P<reason>\S+)"
    r"\tbytes=(?P<bytes>-?\d+)\tinsns=(?P<insns>-?\d+)"
    r"\tblocks=(?P<blocks>\d+)\tslots=(?P<slots>\d+)"
    r"\tcalls=(?P<calls>\d+)\tlocals=(?P<locals>\d+)"
    r"\treturn-kind=(?P<return_kind>\d+)\tvla=(?P<vla>[01])"
    r"\tbackedge=(?P<backedge>[01])"
    r"\tinline-substitution=(?P<inline_substitution>[01])"
    r"\tpointer-array=(?P<pointer_array>[01])"
    r"\tboolean-simplifications=(?P<boolean_simplifications>\d+)"
    r"\tlabel-phi-fallthrough=(?P<label_phi_fallthrough>[01])"
    r"\twide-values=(?P<wide_values>[01])"
    r"\thash=(?P<hash>[0-9a-fA-F]{8})"
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
    "selected_hash",
]
MATRIX_FIELDS = [
    "app",
    "function",
    "candidate",
    "mask",
    "emitted",
    "reason",
    "bytes",
    "insns",
    "blocks",
    "slots",
    "calls",
    "locals",
    "return_kind",
    "vla",
    "backedge",
    "inline_substitution",
    "pointer_array",
    "boolean_simplifications",
    "label_phi_fallthrough",
    "wide_values",
    "hash",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile test apps with MIR selection reporting, write a "
        "stable census, and compare it with an earlier snapshot."
    )
    parser.add_argument("--compiler", default="./dcc")
    parser.add_argument("--tests-dir", default="tests")
    parser.add_argument("--output", default="build/mir-migration-census.tsv")
    parser.add_argument(
        "--candidate-matrix-output",
        metavar="TSV",
        help="also evaluate isolated spilled-selector feature sets and write "
        "their metrics and output hashes to this TSV",
    )
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
    parser.add_argument(
        "--extra-args",
        default="",
        help="extra dcc flags appended to every compile, e.g. -fstack-check "
        "(applied after per-app overrides' dcc_args)",
    )
    parser.add_argument(
        "--jobs",
        "-j",
        type=int,
        default=os.cpu_count() or 1,
        help="parallel compiles (default: CPU count; each compile is an "
        "independent, short-lived subprocess.run call, so this scales well "
        "up to core count). Use -j1 for strictly sequential, deterministic "
        "progress-line ordering (e.g. when diagnosing a single hang).",
    )
    parser.add_argument(
        "--bloat-threshold",
        type=float,
        metavar="PERCENT",
        help="rank accepted MIR functions whose generated assembly exceeds "
        "captured legacy assembly by more than this percentage",
    )
    parser.add_argument(
        "--bloat-limit",
        type=int,
        default=40,
        help="maximum bloat rows to print; 0 prints every row (default: 40)",
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
    candidate_matrix: bool,
) -> tuple[list[dict[str, str]], list[dict[str, str]], str | None]:
    env = os.environ.copy()
    env["DCC_MIR_SELECT_REPORT"] = "1"
    if candidate_matrix:
        env["DCC_MIR_CANDIDATE_MATRIX"] = "1"
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
        return [], [], f"timed out after {timeout}s"
    if completed.returncode != 0:
        detail = completed.stderr.strip().splitlines()
        return (
            [],
            [],
            detail[-1] if detail else f"compiler exited {completed.returncode}",
        )

    rows: list[dict[str, str]] = []
    matrix_rows: list[dict[str, str]] = []
    seen: set[str] = set()
    for line in completed.stderr.splitlines():
        matrix_match = MATRIX_RE.search(line)
        if matrix_match:
            matrix_rows.append({"app": source.stem, **matrix_match.groupdict()})
            continue
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
    return rows, matrix_rows, None


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(sorted(rows, key=lambda row: (row["app"], row["function"])))


def write_matrix_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=MATRIX_FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(
            sorted(
                rows,
                key=lambda row: (
                    row["app"],
                    row["function"],
                    int(row["mask"], 16),
                ),
            )
        )


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


def print_bloat_report(
    rows: list[dict[str, str]], threshold: float, limit: int
) -> None:
    ranked: list[tuple[float, dict[str, str]]] = []
    for row in rows:
        if row["result"] != "mir":
            continue
        generated = int(row["generated_bytes"])
        captured = int(row["captured_bytes"])
        if captured <= 0:
            continue
        growth = (generated - captured) * 100.0 / captured
        if growth > threshold:
            ranked.append((growth, row))
    ranked.sort(
        key=lambda item: (
            -item[0],
            item[1]["app"],
            item[1]["function"],
        )
    )
    selectors = collections.Counter(row["selector"] for _, row in ranked)
    apps = {row["app"] for _, row in ranked}
    print(f"\nAccepted MIR bloat > {threshold:g}%")
    print(f"  functions: {len(ranked)}")
    print(f"  apps: {len(apps)}")
    for selector, count in selectors.most_common():
        print(f"  {count:5d}  {selector}")
    if not ranked:
        return
    print(
        "\n"
        f"{'app':16s} {'function':32s} {'selector':27s} "
        f"{'generated':>9s} {'captured':>9s} {'growth':>9s}"
    )
    visible = ranked if limit == 0 else ranked[: max(limit, 0)]
    for growth, row in visible:
        print(
            f"{row['app']:16s} {row['function'][:32]:32s} "
            f"{row['selector']:27s} {int(row['generated_bytes']):9d} "
            f"{int(row['captured_bytes']):9d} {growth:8.1f}%"
        )


def compare_rows(
    old: dict[tuple[str, str], dict[str, str]],
    new: dict[tuple[str, str], dict[str, str]],
) -> tuple[set[str], int]:
    newly_mir: list[tuple[str, str]] = []
    regressed: list[tuple[str, str]] = []
    changed_apps: set[str] = set()
    runtime_apps: set[str] = set()
    compared_apps = {app for app, _ in new}

    def rows_equal(
        before: dict[str, str] | None, after: dict[str, str] | None
    ) -> bool:
        if before is None or after is None:
            return before is after
        # Snapshots written before selected_hash was added remain comparable:
        # they retain metric-based behavior, while two new-format snapshots
        # additionally catch byte/instruction-count-neutral assembly changes.
        fields = FIELDS if before.get("selected_hash") else FIELDS[:-1]
        return all(before.get(field) == after.get(field) for field in fields)

    for key in sorted(
        key for key in old.keys() | new.keys() if key[0] in compared_apps
    ):
        before = old.get(key)
        after = new.get(key)
        if rows_equal(before, after):
            continue
        changed_apps.add(key[0])
        before_result = before["result"] if before else "missing"
        after_result = after["result"] if after else "missing"
        if before_result != "mir" and after_result == "mir":
            newly_mir.append(key)
            runtime_apps.add(key[0])
        elif before_result == "mir" and after_result != "mir":
            regressed.append(key)
            runtime_apps.add(key[0])
        elif before_result == "mir" and after_result == "mir":
            runtime_apps.add(key[0])
        elif (
            before
            and after
            and before.get("selected_hash")
            and after.get("selected_hash")
            and before["selected_hash"] != after["selected_hash"]
        ):
            runtime_apps.add(key[0])

    print("\nSnapshot delta")
    print(f"  newly MIR-emitted: {len(newly_mir)}")
    for app, function in newly_mir:
        print(f"    + {app}.{function}")
    print(f"  no longer MIR-emitted: {len(regressed)}")
    for app, function in regressed:
        print(f"    - {app}.{function}")
    print(f"  apps with census changes: {len(changed_apps)}")
    print(f"  apps requiring runtime validation: {len(runtime_apps)}")
    if runtime_apps:
        app_list = ",".join(sorted(runtime_apps))
        print("\nFocused validation")
        print(
            "  pwsh ./scripts/runall.ps1 "
            f"-Apps {app_list} -Mode full -RunTimeout 20"
        )
    return runtime_apps, len(regressed)


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
    matrix_rows: list[dict[str, str]] = []
    failures: list[tuple[str, str]] = []
    with tempfile.TemporaryDirectory(prefix="dcc-mir-census-") as directory:
        directory_path = Path(directory)

        def run_one(
            index: int, source: Path
        ) -> tuple[
            int,
            Path,
            list[dict[str, str]],
            list[dict[str, str]],
            str | None,
        ]:
            # Each worker writes to its own assembly file: compiles run
            # concurrently (subprocess.run releases the GIL while the child
            # process runs, so a thread pool scales across cores same as
            # runall.ps1's parallel app suite), and a shared output path
            # would let two in-flight compiles clobber each other's .mac.
            worker_assembly = directory_path / f"census-{index}.mac"
            app_rows, app_matrix_rows, error = compile_source(
                args.compiler,
                source,
                worker_assembly,
                args.stack,
                args.timeout,
                shlex.split(str(overrides.get(source.stem, {}).get("dcc_args", "")))
                + shlex.split(args.extra_args),
                args.candidate_matrix_output is not None,
            )
            return index, source, app_rows, app_matrix_rows, error

        done = 0
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
            futures = [
                executor.submit(run_one, index, source)
                for index, source in enumerate(sources, 1)
            ]
            for future in as_completed(futures):
                index, source, app_rows, app_matrix_rows, error = future.result()
                done += 1
                if error:
                    failures.append((source.stem, error))
                else:
                    rows.extend(app_rows)
                    matrix_rows.extend(app_matrix_rows)
                # Completion order (not dispatch order) with -j>1, matching
                # runall.ps1's own parallel status-line convention.
                print(f"\r[{done:3d}/{len(sources)}] {source.stem:12s}", end="", flush=True)
    print()

    if failures:
        for app, error in failures:
            print(f"error: {app}: {error}", file=sys.stderr)
        return 1

    write_rows(output_path, rows)
    print(f"Wrote {output_path}")
    if args.candidate_matrix_output:
        matrix_path = Path(args.candidate_matrix_output)
        write_matrix_rows(matrix_path, matrix_rows)
        print(f"Wrote {matrix_path} ({len(matrix_rows)} candidates)")
    print_summary(rows)
    if args.bloat_threshold is not None:
        print_bloat_report(rows, args.bloat_threshold, args.bloat_limit)

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
