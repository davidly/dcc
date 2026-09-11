#!/usr/bin/env python3
"""Mutation census for the retained symbol-insert exact schedule."""

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


FUNCTION = "sym_add"
SOURCE = "tests/mir-clobber/syminspf.c"
EXACT_SELECTION = (
    "MIR selection function=sym_add "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=sym_add "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "e4ef3322d56226a7cea371f524685831807c0287a701e8d51bfbb283f99863ab"
)
FIELD_VALUES = {
    "type": (1, 2, 3, 4, 5, 6, 17, 18, 19, 33, 34, 49, 912, 32767),
    "memory_size": (0, 1, 2, 4, 16, 26, 27, 255),
    "src1": (-1, 0, 1, 2, 53, 999),
    "src2": (-1, 0, 1, 2, 53, 999),
    "immediate": (
        -32768, -1, 0, 1, 2, 15, 16, 17, 18, 21, 27, 37, 38, 42,
        43, 45, 47, 60, 62, 94, 124, 127, 128, 129, 276, 277, 278,
        279, 32767,
    ),
    "identity": (88,),
}
UNUSED_FIELDS = {
    "type": {
        "brfalse", "label", "nop", "return", "store", "storeind",
    },
    "memory_size": {
        "arg", "binary", "brfalse", "call", "const", "label",
        "load", "nop", "param", "phi", "return", "straddr",
        "unary",
    },
    "src1": {
        "call", "const", "label", "load", "nop", "param",
        "straddr",
    },
    "src2": {
        "arg", "brfalse", "call", "const", "label", "load",
        "memberaddr", "nop", "param", "return", "store", "straddr",
        "unary",
    },
    "immediate": {
        "arg", "brfalse", "call", "label", "load", "nop", "param",
        "return", "store", "storeind",
    },
    "identity": {
        "arg", "binary", "brfalse", "call", "const", "label",
        "nop", "return", "straddr", "unary",
    },
}
INERT_IDENTITY_OPCODES = {
    "indexaddr", "loadind", "memberaddr", "storeind",
}
REPRESENTATION_EQUIVALENTS = {
    (8, "type", 17), (8, "type", 49),
    (32, "type", 17), (32, "type", 49),
}
BASELINE_NO_OPS = {
    (1, "type", 17),
    (2, "type", 2),
    (3, "type", 2),
    (4, "type", 2),
    (5, "type", 2), (5, "immediate", 128),
    (6, "type", 2), (6, "src1", 3), (6, "src2", 4),
    (6, "immediate", 279),
    (8, "immediate", 1),
    (9, "type", 17), (9, "src1", 6),
    (10, "type", 3),
    (12, "type", 2),
    (13, "type", 2), (13, "immediate", 1),
    (14, "type", 2), (14, "src1", 8), (14, "src2", 9),
    (14, "immediate", 43),
    (15, "memory_size", 2), (15, "src1", 10),
    (17, "memory_size", 2), (17, "src1", 8),
    (18, "type", 912),
    (20, "type", 912), (20, "memory_size", 27),
    (20, "src1", 12), (20, "src2", 8), (20, "immediate", 27),
    (22, "type", 19), (22, "src1", 14),
    (23, "type", 2), (23, "immediate", 0),
    (24, "type", 2), (24, "src1", 16),
    (25, "type", 2), (25, "immediate", 27),
    (27, "type", 34), (27, "src1", 17),
    (28, "type", 19),
    (29, "type", 912),
    (31, "type", 912), (31, "memory_size", 27),
    (31, "src1", 20), (31, "src2", 8), (31, "immediate", 27),
    (32, "memory_size", 16), (32, "src1", 22),
    (32, "immediate", 0),
    (33, "type", 17), (33, "src1", 23),
    (34, "type", 17),
    (35, "type", 17), (35, "src1", 24),
    (38, "type", 2), (38, "immediate", 15),
    (40, "type", 34), (40, "src1", 27),
    (41, "type", 17),
    (42, "type", 912),
    (44, "type", 912), (44, "memory_size", 27),
    (44, "src1", 30), (44, "src2", 8), (44, "immediate", 27),
    (45, "type", 49), (45, "memory_size", 1),
    (45, "src1", 32), (45, "immediate", 16),
    (47, "type", 33), (47, "src1", 1), (47, "immediate", 0),
    (48, "memory_size", 1), (48, "src1", 33), (48, "src2", 35),
    (49, "type", 912),
    (51, "type", 912), (51, "memory_size", 27),
    (51, "src1", 36), (51, "src2", 8), (51, "immediate", 27),
    (52, "type", 49), (52, "memory_size", 1),
    (52, "src1", 38), (52, "immediate", 17),
    (54, "type", 33), (54, "src1", 2), (54, "immediate", 0),
    (55, "memory_size", 1), (55, "src1", 39), (55, "src2", 41),
    (56, "type", 912),
    (58, "type", 912), (58, "memory_size", 27),
    (58, "src1", 42), (58, "src2", 8), (58, "immediate", 27),
    (59, "type", 18), (59, "memory_size", 2),
    (59, "src1", 44), (59, "immediate", 21),
    (60, "type", 2), (60, "immediate", 2),
    (61, "memory_size", 2), (61, "src1", 45), (61, "src2", 46),
    (62, "type", 912),
    (64, "type", 912), (64, "memory_size", 27),
    (64, "src1", 47), (64, "src2", 8), (64, "immediate", 27),
    (65, "type", 49), (65, "memory_size", 1),
    (65, "src1", 49), (65, "immediate", 18),
    (67, "type", 33), (67, "immediate", 2),
    (68, "memory_size", 1), (68, "src1", 50), (68, "src2", 52),
    (70, "src1", 8),
}
EXPECTED_OUTCOMES = {"rejected": 2569, "accepted": 1975}
EXPECTED_CLASSES = {
    "char-pointer-representation": 4,
    "inert-identity": 15,
    "no-op": 85,
    "unused-field": 1870,
    "valid-error-string": 1,
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
        raise RuntimeError("baseline did not select symbol-insert-schedule")
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
    if tuple(instructions) != tuple(range(71)):
        raise RuntimeError(
            f"expected instructions 0..70, got {len(instructions)}"
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
        for instruction in range(71)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, opcode, field, value, changed):
    if changed:
        if instruction == 8 and field == "immediate" and value == 0:
            return "valid-error-string"
        raise RuntimeError(
            f"unclassified output-changing survivor: "
            f"{instruction}:{opcode}:{field}:{value}"
        )
    if field == "identity" and opcode in INERT_IDENTITY_OPCODES:
        return "inert-identity"
    if opcode in UNUSED_FIELDS[field]:
        return "unused-field"
    if (instruction, field, value) in REPRESENTATION_EQUIVALENTS:
        return "char-pointer-representation"
    if (instruction, field, value) in BASELINE_NO_OPS:
        return "no-op"
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
        "--output-dir", default="build/symbol-insert-wave22-audit"
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
    if outcomes != EXPECTED_OUTCOMES:
        raise RuntimeError(f"mutation outcome changed: {outcomes}")
    if classes != EXPECTED_CLASSES:
        raise RuntimeError(f"benign classification changed: {classes}")
    print(
        f"symbol-insert Wave 22 audit passed: {len(results)} mutations, "
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
