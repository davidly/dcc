#!/usr/bin/env python3
"""Mutation census for the retained symbol-find exact schedule."""

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


FUNCTION = "sym_find"
SOURCE = "tests/mir-clobber/scansym.c"
EXACT_SELECTION = (
    "MIR selection function=sym_find "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=sym_find "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "9df9cad21d0b1b84bceff334612cc271b923594e619416e47932787642aa4ff6"
)
FIELD_VALUES = {
    "type": (1, 17, 18, 912, 32767),
    "memory_size": (1, 4, 255),
    "src1": (-1, 0, 999),
    "src2": (-1, 0, 999),
    "immediate": (-32768, 0, 1, 2, 32767),
    "identity": (88,),
}
UNUSED_FIELDS = {
    "type": {
        "brfalse", "jump", "label", "nop", "return", "store",
        "storeind",
    },
    "memory_size": {
        "arg", "binary", "brfalse", "call", "const", "jump", "label",
        "load", "nop", "param", "phi", "return", "straddr",
    },
    "src1": {
        "call", "const", "jump", "label", "load", "nop", "param",
        "straddr",
    },
    "src2": {
        "arg", "brfalse", "call", "const", "jump", "label", "load",
        "memberaddr", "nop", "param", "return", "store", "straddr",
    },
    "immediate": {
        "brfalse", "call", "jump", "label", "nop", "phi", "return",
        "storeind",
    },
    "identity": {
        "arg", "binary", "brfalse", "call", "const", "jump", "label",
        "nop", "return", "straddr", "unary",
    },
}
INERT_IDENTITY_OPCODES = {
    "indexaddr", "loadind", "memberaddr", "storeind",
}
PARAMETERIZED_IMMEDIATES = {
    32: "symbol-limit",
    35: "symbol-error-string",
    75: "memory-limit",
    78: "memory-error-string",
}
EXPECTED_OUTCOMES = {"rejected": 738, "accepted": 1002}
EXPECTED_CLASSES = {
    "constant-representation": 5,
    "inert-identity": 13,
    "memory-error-string": 1,
    "memory-limit": 3,
    "no-op": 66,
    "symbol-error-string": 1,
    "symbol-limit": 2,
    "unused-field": 911,
}
BASELINE_NO_OPS = {
    (1, "type", 17), (6, "type", 17), (12, "type", 912),
    (14, "type", 912), (15, "type", 17), (16, "type", 17),
    (17, "type", 17), (18, "type", 17), (35, "type", 17),
    (36, "type", 17), (39, "type", 912), (41, "type", 912),
    (42, "type", 17), (43, "type", 17), (44, "type", 17),
    (45, "type", 17), (51, "type", 17), (52, "type", 912),
    (54, "type", 912), (55, "type", 18), (61, "type", 912),
    (63, "type", 912), (64, "type", 18), (68, "type", 912),
    (70, "type", 912), (71, "type", 18), (78, "type", 17),
    (79, "type", 17),
    (1, "immediate", 0), (2, "immediate", 0),
    (4, "immediate", 0), (6, "immediate", 0),
    (9, "immediate", 0), (12, "immediate", 0),
    (15, "immediate", 0), (16, "immediate", 0),
    (17, "immediate", 0), (18, "immediate", 1),
    (26, "immediate", 1), (28, "immediate", 0),
    (31, "immediate", 0), (35, "immediate", 1),
    (36, "immediate", 0), (39, "immediate", 0),
    (40, "immediate", 0), (42, "immediate", 0),
    (43, "immediate", 0), (44, "immediate", 0),
    (45, "immediate", 1), (50, "immediate", 2),
    (52, "immediate", 0), (53, "immediate", 0),
    (56, "immediate", 0), (57, "immediate", 1),
    (59, "immediate", 0), (61, "immediate", 0),
    (62, "immediate", 0), (68, "immediate", 0),
    (69, "immediate", 0), (72, "immediate", 0),
    (74, "immediate", 0), (78, "immediate", 2),
    (79, "immediate", 0), (82, "immediate", 0),
    (83, "immediate", 1), (85, "immediate", 0),
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
        raise RuntimeError("baseline did not select symbol-find-schedule")
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
    if tuple(instructions) != tuple(range(87)):
        raise RuntimeError(
            f"expected instructions 0..86, got {len(instructions)}"
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
        for instruction in range(87)
        for field, values in FIELD_VALUES.items()
        for value in values
    ]


def classify(instruction, opcode, field, value, changed):
    if changed:
        if field == "immediate" and instruction in PARAMETERIZED_IMMEDIATES:
            return PARAMETERIZED_IMMEDIATES[instruction]
        raise RuntimeError(
            f"unclassified output-changing survivor: "
            f"{instruction}:{opcode}:{field}"
        )
    if field == "identity" and opcode in INERT_IDENTITY_OPCODES:
        return "inert-identity"
    if opcode in UNUSED_FIELDS[field]:
        return "unused-field"
    if instruction == 48 and field == "type":
        return "constant-representation"
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
        "--output-dir", default="build/symbol-find-wave21-audit"
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
        f"symbol-find Wave 21 audit passed: {len(results)} mutations, "
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
