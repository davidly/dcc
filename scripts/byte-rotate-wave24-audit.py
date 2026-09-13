#!/usr/bin/env python3
"""Mutation census for the retained byte-rotate-flags exact schedule."""

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


FUNCTION = "rotate_byte"
SOURCE = "tests/mir-clobber/brt24.c"
EXACT_SELECTION = (
    "MIR selection function=rotate_byte "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=rotate_byte "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "8ee1a5b458527472e66aba5eb2ec966aaf59290e642387af51ca7d5b6cca304d"
)
FIELD_VALUES = {
    "type": (0, 1, 2, 3, 4, 5, 6, 17, 18, 22, 33, 34, 912, 32767),
    "memory_size": (0, 1, 2, 4, 255),
    "src1": tuple(range(-1, 100)) + (999,),
    "src2": tuple(range(-1, 100)) + (999,),
    "immediate": (
        -32768, -1, 0, 1, 2, 3, 7, 8, 31, 32, 33, 38, 64, 124,
        128, 224, 255, 276, 282, 283, 32767,
    ),
    "identity": (88,),
}
UNUSED_FIELDS = {
    "memory_size": {
        "address", "binary", "brfalse", "const", "jump", "label",
        "load", "nop", "param", "return",
    },
    "src1": {"const", "jump", "label", "load", "nop", "param"},
    "src2": {
        "brfalse", "const", "jump", "label", "load", "loadind",
        "nop", "param", "return", "store", "unary",
    },
    "immediate": {
        "brfalse", "jump", "label", "loadind", "nop", "return",
        "storeind",
    },
    "identity": {
        "binary", "brfalse", "const", "jump", "label", "loadind",
        "memberaddr", "nop", "return", "storeind", "unary",
    },
}
EQUIVALENT_CONSTANT_SOURCES = {
    (21, "src1"),
    (26, "src2"),
    (48, "src1"),
    (53, "src2"),
    (61, "src2"),
    (80, "src2"),
    (85, "src2"),
    (101, "src2"),
    (106, "src2"),
    (114, "src2"),
    (128, "src2"),
}
EXPECTED_OUTCOMES = {"rejected": 15255, "accepted": 18800}
EXPECTED_CLASSES = {
    "baseline-no-op": 461,
    "dead-local-store": 42,
    "equivalent-constant-source": 65,
    "unused-field": 18220,
    "valid-state-layout": 12,
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
        raise RuntimeError("baseline did not select byte-rotate-flags schedule")
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
    if tuple(instructions) != tuple(range(139)):
        raise RuntimeError(
            f"expected instructions 0..138, got {len(instructions)}"
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
        for instruction in range(139)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, opcode, field, changed):
    if changed:
        if instruction in (124, 132) and field == "immediate":
            return "valid-state-layout"
        raise RuntimeError(
            f"unclassified output-changing survivor: "
            f"{instruction}:{opcode}:{field}"
        )
    if opcode in UNUSED_FIELDS.get(field, set()):
        return "unused-field"
    if instruction in (42, 95) and field == "immediate":
        return "dead-local-store"
    if (instruction, field) in EQUIVALENT_CONSTANT_SOURCES:
        return "equivalent-constant-source"
    return "baseline-no-op"


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
        instruction, opcodes[instruction], field, changed
    )
    output.unlink()
    return (*job, opcodes[instruction], "accepted", classification)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument(
        "--output-dir", default="build/byte-rotate-wave24-audit"
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
                for job in mutation_jobs()
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
    if outcomes != EXPECTED_OUTCOMES:
        raise RuntimeError(f"mutation outcome changed: {outcomes}")
    if classes != EXPECTED_CLASSES:
        raise RuntimeError(f"benign classification changed: {classes}")
    print(
        f"byte-rotate Wave 24 audit passed: {len(results)} mutations, "
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
