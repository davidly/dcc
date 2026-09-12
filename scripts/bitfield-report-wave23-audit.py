#!/usr/bin/env python3
"""Mutation census for the retained bitfield-report exact schedule."""

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


FUNCTION = "main"
SOURCE = "tests/tbitfld.c"
EXACT_SELECTION = (
    "MIR selection function=main "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "03bf422d036df62dd132ed8cebbe09ff1b99e978e972c1f90025de018f9208c0"
)
FIELD_VALUES = {
    "type": (0, 1, 2, 3, 4, 5, 6, 17, 18, 34, 50, 400, 656, 32767),
    "memory_size": (0, 1, 2, 4, 255),
    "src1": (-1, 0, 1, 2, 312, 999),
    "src2": (-1, 0, 1, 2, 312, 999),
    "immediate": (
        -32768, -1, 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 20, 31, 40,
        43, 45, 94, 200, 240, 248, 255, 282, 1000, 32767, 65535,
    ),
    "identity": (88,),
}
UNUSED_FIELDS = {
    "type": {"label", "nop", "return", "store", "storeind"},
    "memory_size": {
        "address", "arg", "binary", "call", "const", "label", "nop",
        "return", "straddr", "unary",
    },
    "src1": {
        "call", "const", "label", "nop", "straddr",
    },
    "src2": {
        "address", "arg", "call", "const", "label", "loadind",
        "memberaddr", "nop", "return", "store", "straddr", "unary",
    },
    "immediate": {
        "address", "arg", "call", "label", "loadind", "nop", "return",
        "store", "storeind",
    },
    "identity": {
        "arg", "binary", "call", "const", "label", "loadind",
        "memberaddr", "nop", "return", "storeind", "straddr", "unary",
    },
}
BASELINE_NO_OP_FIELDS = {
    ("address", "src1"),
    ("address", "type"),
    ("binary", "immediate"),
    ("binary", "type"),
    ("call", "type"),
    ("callagg", "memory_size"),
    ("callagg", "src1"),
    ("callagg", "src2"),
    ("callagg", "type"),
    ("const", "immediate"),
    ("const", "type"),
    ("loadind", "memory_size"),
    ("loadind", "type"),
    ("memberaddr", "immediate"),
    ("memberaddr", "memory_size"),
    ("memberaddr", "type"),
    ("return", "src1"),
    ("store", "memory_size"),
    ("store", "src1"),
    ("storeind", "memory_size"),
    ("straddr", "immediate"),
    ("straddr", "type"),
    ("unary", "immediate"),
    ("unary", "type"),
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
        raise RuntimeError("baseline did not select bitfield-report schedule")
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
    if tuple(instructions) != tuple(range(428)):
        raise RuntimeError(
            f"expected instructions 0..427, got {len(instructions)}"
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
        for instruction in range(428)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, opcode, field, value, changed):
    if changed:
        if opcode == "straddr" and field == "immediate":
            return "valid-format-string"
        raise RuntimeError(
            f"unclassified output-changing survivor: "
            f"{instruction}:{opcode}:{field}:{value}"
        )
    if opcode in UNUSED_FIELDS[field]:
        return "unused-field"
    if opcode == "arg" and field == "type":
        return "unused-arg-type"
    if (opcode, field) in BASELINE_NO_OP_FIELDS:
        return "baseline-no-op"
    raise RuntimeError(
        f"unclassified byte-identical survivor: "
        f"{instruction}:{opcode}:{field}:{value}"
    )


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
    changed = output.read_bytes() != baseline_bytes
    classification = classify(
        instruction, opcodes[instruction], field, value, changed
    )
    output.unlink()
    return (
        *job, opcodes[instruction], "accepted", classification,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/bitfield-report-wave23-audit"
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
    expected_outcomes = {"rejected": 15041, "accepted": 9783}
    expected_classes = {
        "baseline-no-op": 647,
        "unused-arg-type": 1050,
        "unused-field": 7998,
        "valid-format-string": 88,
    }
    if outcomes != expected_outcomes:
        raise RuntimeError(f"mutation outcome changed: {outcomes}")
    if classes != expected_classes:
        raise RuntimeError(f"benign classification changed: {classes}")
    print(
        f"bitfield-report Wave 23 audit passed: {len(results)} mutations, "
        f"{outcomes['rejected']} semantic rejections, "
        f"{outcomes['accepted']} classified survivors"
    )
    print("classes: " + ", ".join(
        f"{name}={count}" for name, count in sorted(classes.items())
    ))
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")


if __name__ == "__main__":
    main()
