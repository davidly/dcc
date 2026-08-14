#!/usr/bin/env python3
"""Compare generated MIR selections from the current and parent compilers."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--parent-compiler",
        required=True,
        help="path to a dcc binary built from the parent revision",
    )
    parser.add_argument("--current-compiler", default="./dcc")
    parser.add_argument("--apps", help="optional comma-separated app list")
    parser.add_argument(
        "--output-dir",
        default="build/mir-current-vs-parent",
    )
    return parser.parse_args()


def run(command: list[str]) -> None:
    print("+ " + " ".join(command))
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def census_command(
    compiler: str,
    output: Path,
    apps: str | None,
    stack: bool,
    compare: Path | None = None,
) -> list[str]:
    command = [
        sys.executable,
        "scripts/mir-migration-census.py",
        "--compiler",
        compiler,
        "--output",
        str(output),
    ]
    if apps:
        command.extend(["--apps", apps])
    if stack:
        command.append("--extra-args=-fstack-check")
    if compare is not None:
        command.extend(["--compare", str(compare)])
    return command


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    for stack in (False, True):
        suffix = "stack" if stack else "normal"
        parent = output_dir / f"parent-{suffix}.tsv"
        current = output_dir / f"current-{suffix}.tsv"
        run(
            census_command(
                args.parent_compiler, parent, args.apps, stack
            )
        )
        run(
            census_command(
                args.current_compiler,
                current,
                args.apps,
                stack,
                compare=parent,
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
