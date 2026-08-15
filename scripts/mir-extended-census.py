#!/usr/bin/env python3
"""Census MIR selection across the runnable extended c-testsuite corpus."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed


SELECTION_RE = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=(?P<result>\S+) "
    r"reason=(?P<reason>\S+) generated-bytes=(?P<generated_bytes>-?\d+) "
    r"captured-bytes=(?P<captured_bytes>-?\d+) "
    r"generated-insns=(?P<generated_insns>-?\d+) "
    r"captured-insns=(?P<captured_insns>-?\d+) blocks=(?P<blocks>\d+) "
    r"selected-hash=(?P<selected_hash>[0-9a-fA-F]{8}) "
    r"sink=(?P<sink>\S+) "
    r"mir-insns=(?P<mir_insns>\d+) values=(?P<values>\d+) "
    r"calls=(?P<calls>\d+) locals=(?P<locals>\d+) "
    r"aggregate-temps=(?P<aggregate_temps>\d+) slots=(?P<slots>\d+) "
    r"vla=(?P<vla>[01]) backedge=(?P<backedge>[01]) "
    r"wide=(?P<wide>[01]) "
    r"inline-substitution=(?P<inline_substitution>[01]) "
    r"member-address=(?P<member_address>[01]) "
    r"bool-values=(?P<bool_values>[01]) "
    r"return-size=(?P<return_size>\d+)"
)

FIELDS = [
    "test",
    "mode",
    "tags",
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
    "sink",
    "mir_insns",
    "values",
    "calls",
    "locals",
    "aggregate_temps",
    "slots",
    "vla",
    "backedge",
    "wide",
    "inline_substitution",
    "member_address",
    "bool_values",
    "return_size",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile the runnable extended c-testsuite source set "
        "with MIR selection reporting in stack and no-stack modes."
    )
    parser.add_argument("--compiler", default="./dcc")
    parser.add_argument(
        "--suite-dir",
        default="tests/extended-tests/tests/single-exec",
    )
    parser.add_argument(
        "--overrides",
        default="tests/_extended_test_overrides.json",
    )
    parser.add_argument(
        "--output",
        default="build/mir-extended-census.tsv",
    )
    parser.add_argument(
        "--tests",
        help="optional comma-separated test basenames",
    )
    parser.add_argument(
        "--mode",
        choices=("both", "stack", "no-stack"),
        default="both",
    )
    parser.add_argument("--stack", type=int, default=512)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument(
        "--jobs",
        "-j",
        type=int,
        default=os.cpu_count() or 1,
    )
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="also set DCC_MIR_REQUIRE_COMPLETE=1",
    )
    parser.add_argument(
        "--keep-work",
        action="store_true",
        help="retain per-compile assembly files beside the output",
    )
    return parser.parse_args()


def load_configuration(
    path: Path,
) -> tuple[set[str], dict[str, dict[str, object]]]:
    if not path.is_file():
        return set(), {}
    with path.open(encoding="utf-8") as handle:
        data = json.load(handle)
    ignored: set[str] = set()
    overrides: dict[str, dict[str, object]] = {}
    for entry in data.get("tests", []):
        name = str(entry.get("name", "")).lower()
        if not name:
            continue
        if entry.get("ignore", False):
            ignored.add(name)
        overrides[name] = entry
    return ignored, overrides


def load_tags(source: Path) -> str:
    path = source.with_suffix(source.suffix + ".tags")
    if not path.is_file():
        return ""
    tags = [
        line.strip().lower()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    return ",".join(tags)


def select_sources(
    suite_dir: Path,
    ignored: set[str],
    requested_text: str | None,
) -> list[Path]:
    sources = {
        source.stem.lower(): source
        for source in suite_dir.glob("*.c")
        if source.stem.lower() not in ignored
    }
    if requested_text is None:
        return [sources[name] for name in sorted(sources)]
    requested = [
        Path(name.strip()).stem.lower()
        for name in requested_text.split(",")
        if name.strip()
    ]
    missing = [name for name in requested if name not in sources]
    if missing:
        raise ValueError("unknown or ignored test(s): " + ", ".join(missing))
    return [sources[name] for name in requested]


def boolean_override(entry: dict[str, object], key: str, default: bool) -> bool:
    value = entry.get(key)
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in ("1", "true", "yes", "on"):
        return True
    if text in ("0", "false", "no", "off"):
        return False
    raise ValueError(f"invalid {key} override: {value}")


def compile_source(
    compiler: str,
    source: Path,
    assembly: Path,
    mode: str,
    stack: int,
    timeout: int,
    override: dict[str, object],
    require_complete: bool,
) -> tuple[list[dict[str, str]], str | None]:
    command = [compiler]
    if mode == "stack":
        command.append("-fstack-check")
    if boolean_override(override, "dcc_floatio", True):
        command.append("-ffloatio")
    if boolean_override(override, "dcc_longio", True):
        command.append("-flongio")
    command.extend(
        [
            "-stack",
            str(stack),
            "-I",
            ".",
            *shlex.split(str(override.get("dcc_args", ""))),
            str(source),
            "-o",
            str(assembly),
        ]
    )
    environment = os.environ.copy()
    environment["DCC_MIR_SELECT_REPORT"] = "1"
    environment["DCC_MIR_REQUIRE_EMIT"] = "1"
    if require_complete:
        environment["DCC_MIR_REQUIRE_COMPLETE"] = "1"
    try:
        completed = subprocess.run(
            command,
            env=environment,
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
        return [], detail[-1] if detail else (
            f"compiler exited {completed.returncode}"
        )

    rows: list[dict[str, str]] = []
    seen: set[str] = set()
    tags = load_tags(source)
    for line in completed.stderr.splitlines():
        match = SELECTION_RE.search(line)
        if match is None:
            continue
        values = match.groupdict()
        if values["function"] in seen:
            continue
        seen.add(values["function"])
        rows.append(
            {
                "test": source.stem,
                "mode": mode,
                "tags": tags,
                **values,
            }
        )
    return rows, None


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(
            sorted(
                rows,
                key=lambda row: (
                    row["test"],
                    row["mode"],
                    row["function"],
                ),
            )
        )


def print_summary(rows: list[dict[str, str]]) -> None:
    for mode in ("no-stack", "stack"):
        mode_rows = [row for row in rows if row["mode"] == mode]
        if not mode_rows:
            continue
        emitted = sum(row["result"] == "mir" for row in mode_rows)
        non_mir = len(mode_rows) - emitted
        percent = emitted * 100.0 / len(mode_rows) if mode_rows else 0.0
        print(
            f"{mode}: {emitted}/{len(mode_rows)} MIR "
            f"({percent:.2f}%), {non_mir} non-MIR/error"
        )
        for (selector, result, reason), count in sorted(
            collections.Counter(
                (row["selector"], row["result"], row["reason"])
                for row in mode_rows
            ).items(),
            key=lambda item: (-item[1], item[0]),
        ):
            print(f"  {count:4d} {selector} {result} {reason}")


def main() -> int:
    args = parse_args()
    suite_dir = Path(args.suite_dir)
    output = Path(args.output)
    ignored, overrides = load_configuration(Path(args.overrides))
    try:
        sources = select_sources(suite_dir, ignored, args.tests)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    modes = (
        ["no-stack", "stack"]
        if args.mode == "both"
        else [args.mode]
    )
    work_dir = output.parent / f"{output.stem}-work"
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)

    rows: list[dict[str, str]] = []
    failures: list[tuple[str, str, str]] = []
    jobs = [
        (source, mode)
        for source in sources
        for mode in modes
    ]

    def run_one(
        index: int, source: Path, mode: str
    ) -> tuple[str, str, list[dict[str, str]], str | None]:
        assembly = work_dir / f"{index:04d}-{source.stem}-{mode}.mac"
        source_rows, error = compile_source(
            args.compiler,
            source,
            assembly,
            mode,
            args.stack,
            args.timeout,
            overrides.get(source.stem.lower(), {}),
            args.require_complete,
        )
        return source.stem, mode, source_rows, error

    done = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [
            executor.submit(run_one, index, source, mode)
            for index, (source, mode) in enumerate(jobs, 1)
        ]
        for future in as_completed(futures):
            test, mode, source_rows, error = future.result()
            done += 1
            print(
                f"\r[{done:3d}/{len(jobs)}] {test} {mode:8s}",
                end="",
                flush=True,
            )
            if error is not None:
                failures.append((test, mode, error))
            else:
                rows.extend(source_rows)
    print()

    write_rows(output, rows)
    if not args.keep_work:
        shutil.rmtree(work_dir)
    print(f"Wrote {output}")
    print_summary(rows)
    if failures:
        for test, mode, error in sorted(failures):
            print(f"error: {test} {mode}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
