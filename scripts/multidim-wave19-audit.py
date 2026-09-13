#!/usr/bin/env python3
"""Exhaustively mutate the retained multidimensional-array schedule."""

import argparse
import concurrent.futures
import csv
import hashlib
import os
import re
import shutil
import subprocess
from collections import Counter
from pathlib import Path


FUNCTION = "multidim_wave8"
SOURCE = "tests/mir-clobber/mdimw8.c"
EXACT_ACCEPT = (
    "MIR machine function=multidim_wave8 "
    "template=multidim-array-runner accept=emitted"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=multidim_wave8 "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "7928111188a6415c5e4855ec0ba4e6f2266846b72b3e512c818171058686306a"
)
FIELD_VALUES = {
    "type": (-12345, 32767),
    "memory_size": (3, 7),
    "src1": (-1, 999),
    "src2": (-1, 999),
    "immediate": (-32768, 32767),
    "identity": (88,),
}
EXPECTED_CLASSES = {
    "no-op": 869,
    "inert-identity": 567,
    "unused-field": 1977,
}


def run(command, root, env):
    completed = subprocess.run(
        command,
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    output = completed.stdout + completed.stderr
    if completed.returncode:
        raise RuntimeError(
            f"{' '.join(map(str, command))} failed\n{output}"
        )
    return output


def compiler_path(root):
    configured = os.environ.get("DCC")
    compiler = (
        Path(configured)
        if configured
        else root / ("dcc.exe" if os.name == "nt" else "dcc")
    )
    if not compiler.is_absolute():
        compiler = (root / compiler).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"DCC compiler not found: {compiler}")
    return compiler


def compiler_command(compiler, output):
    return [
        str(compiler), "-fstack-check", "-c", SOURCE,
        "-I", ".", "-o", str(output),
    ]


def baseline(root, compiler, output):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), root, env)
    if EXACT_ACCEPT not in report:
        raise RuntimeError("baseline did not select multidim-array-runner")
    instructions = []
    inside = False
    for line in report.splitlines():
        if line.startswith(f"; MIR function={FUNCTION} "):
            inside = True
            continue
        if line.startswith(f"; MIR summary function={FUNCTION}"):
            break
        if inside:
            match = re.match(r";\s+(\d+)\s+\w+\s+", line)
            if match:
                instructions.append(int(match.group(1)))
    if instructions != list(range(680)):
        raise RuntimeError(
            f"expected instructions 0..679, got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return output.read_bytes()


def mutation_jobs():
    return [
        (instruction, field, value)
        for instruction in range(680)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, field, value):
    del instruction
    if field in ("src1", "src2") and value == -1:
        return "no-op"
    if field == "identity":
        return "inert-identity"
    return "unused-field"


def mutate(root, compiler, work, baseline_bytes, job):
    instruction, field, value = job
    output_path = work / f"{instruction}-{field}-{value}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output_path), root, env)
    accepted = EXACT_ACCEPT in report
    if not accepted:
        if GENERIC_SELECTION.search(report) is None:
            raise RuntimeError(
                f"{instruction}:{field}:{value} neither selected exact "
                "nor generic MIR"
            )
        output_path.unlink()
        return (*job, "rejected", "")
    if output_path.read_bytes() != baseline_bytes:
        raise RuntimeError(
            f"{instruction}:{field}:{value} changed accepted assembly"
        )
    output_path.unlink()
    return (*job, "accepted", classify(*job))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/multidim-wave19-audit"
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    root = Path(__file__).resolve().parents[1]
    compiler = compiler_path(root)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    work = output_dir / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    baseline_path = output_dir / "baseline.MAC"
    baseline_bytes = baseline(root, compiler, baseline_path)
    jobs = mutation_jobs()
    results = []
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs
        ) as executor:
            futures = [
                executor.submit(
                    mutate, root, compiler, work, baseline_bytes, job
                )
                for job in jobs
            ]
            for future in concurrent.futures.as_completed(futures):
                results.append(future.result())
    finally:
        shutil.rmtree(work, ignore_errors=True)

    results.sort(key=lambda row: (row[0], row[1], row[2]))
    with (output_dir / "mutation-census.tsv").open(
        "w", newline="", encoding="utf-8"
    ) as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            ("instruction", "field", "value", "outcome", "classification")
        )
        writer.writerows(results)

    outcomes = Counter(row[3] for row in results)
    classes = Counter(row[4] for row in results if row[3] == "accepted")
    if len(results) != 7480 or outcomes != {
        "rejected": 4067, "accepted": 3413
    }:
        raise RuntimeError(
            f"mutation outcome changed: total={len(results)} {outcomes}"
        )
    if classes != EXPECTED_CLASSES:
        raise RuntimeError(f"benign classification changed: {classes}")
    print(
        "multidim Wave 19 audit passed: "
        "7480 mutations, 4067 semantic rejections, "
        "3413 classified byte-identical survivors"
    )
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")


if __name__ == "__main__":
    main()
