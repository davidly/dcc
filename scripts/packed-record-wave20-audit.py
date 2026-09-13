#!/usr/bin/env python3
"""Exhaustively mutate the retained packed-record exact schedule."""

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


FUNCTION = "test_many"
SOURCE = "tests/mir-clobber/pr6rec.c"
EXACT_SELECTION = (
    "MIR selection function=test_many "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=test_many "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "03670106bb30e4650f96c2d8dda0469e030e6421c02f5b08cb320894efdc7936"
)
FIELD_VALUES = {
    "type": (-12345, 32767),
    "memory_size": (3, 7),
    "src1": (-1, 999),
    "src2": (-1, 999),
    "immediate": (-32768, 32767),
    "identity": (88,),
}
BENIGN_UNUSED_FIELDS = {
    "type": {"brfalse", "jump", "label", "nop"},
    "memory_size": {
        "binary", "brfalse", "const", "jump", "label", "nop", "phi",
        "straddr", "unary",
    },
    "src1": {
        "address", "call", "const", "jump", "label", "load", "nop",
        "straddr",
    },
    "src2": {
        "address", "arg", "brfalse", "call", "const", "jump", "label",
        "load", "loadind", "memberaddr", "nop", "store", "straddr",
        "unary",
    },
    "immediate": {
        "brfalse", "jump", "label", "loadind", "nop", "storeind",
    },
    "identity": {
        "arg", "binary", "brfalse", "const", "indexaddr", "jump",
        "label", "loadind", "memberaddr", "nop", "storeind", "straddr",
        "unary",
    },
}
EXPECTED_CLASSES = {
    "unused-field": 1903,
    "no-op": 61,
    "inert-identity": 38,
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
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        SOURCE, "-o", str(output),
    ]


def baseline(root, compiler, output):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), root, env)
    if EXACT_SELECTION not in report:
        raise RuntimeError("baseline did not select packed-record-runner")
    instructions = {}
    inside = False
    for line in report.splitlines():
        if line.startswith(f"; MIR function={FUNCTION} "):
            inside = True
            continue
        if line.startswith(f"; MIR summary function={FUNCTION}"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+", line)
        if match:
            instructions[int(match.group(1))] = match.group(2)
    if tuple(instructions) != tuple(range(319)):
        raise RuntimeError(
            f"expected instructions 0..318, got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return instructions, output.read_bytes()


def mutation_jobs():
    return [
        (instruction, field, value)
        for instruction in range(319)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, opcode, field, value):
    del instruction
    if opcode not in BENIGN_UNUSED_FIELDS[field]:
        raise RuntimeError(
            f"unclassified accepted mutation: {opcode}:{field}:{value}"
        )
    if ((field == "src1" and opcode == "call" and value == -1) or
        (field == "src2" and opcode in ("arg", "call") and value == -1)):
        return "no-op"
    if field == "identity" and opcode in {
        "indexaddr", "loadind", "memberaddr", "storeind"
    }:
        return "inert-identity"
    return "unused-field"


def mutate(root, compiler, work, baseline_bytes, opcodes, job):
    instruction, field, value = job
    output = work / f"{instruction}-{field}-{value}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), root, env)
    if EXACT_SELECTION not in report:
        if GENERIC_SELECTION.search(report) is None:
            raise RuntimeError(
                f"{instruction}:{field}:{value} neither selected exact "
                "nor generic MIR"
            )
        output.unlink()
        return (*job, opcodes[instruction], "rejected", "")
    if output.read_bytes() != baseline_bytes:
        raise RuntimeError(
            f"{instruction}:{field}:{value} changed accepted assembly"
        )
    output.unlink()
    return (
        *job,
        opcodes[instruction],
        "accepted",
        classify(instruction, opcodes[instruction], field, value),
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/packed-record-wave20-audit"
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
    opcodes, baseline_bytes = baseline(
        root, compiler, output_dir / "baseline.MAC"
    )
    jobs = mutation_jobs()
    results = []
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs
        ) as executor:
            futures = [
                executor.submit(
                    mutate,
                    root,
                    compiler,
                    work,
                    baseline_bytes,
                    opcodes,
                    job,
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
            (
                "instruction", "field", "value", "opcode",
                "outcome", "classification",
            )
        )
        writer.writerows(results)

    outcomes = Counter(row[4] for row in results)
    classes = Counter(row[5] for row in results if row[4] == "accepted")
    if len(results) != 3509 or outcomes != {
        "rejected": 1507, "accepted": 2002
    }:
        raise RuntimeError(
            f"mutation outcome changed: total={len(results)} {outcomes}"
        )
    if classes != EXPECTED_CLASSES:
        raise RuntimeError(f"benign classification changed: {classes}")
    print(
        "packed-record Wave 20 audit passed: "
        "3509 mutations, 1507 semantic rejections, "
        "2002 classified byte-identical survivors"
    )
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")


if __name__ == "__main__":
    main()
