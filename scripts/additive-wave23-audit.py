#!/usr/bin/env python3
"""Audit the retained additive-subscript exact schedule semantically."""

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


FUNCTION = "additive_wave12"
SOURCE = "tests/mir-clobber/addw12.c"
EXACT_ACCEPT = (
    "MIR machine function=additive_wave12 "
    "template=additive-subscript-runner accept=emitted"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=additive_wave12 "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "2c8340bb8fb6bc70257705d1b63b8b12c24bed69e1300d32b261b3bfac5c21eb"
)
EXPECTED_OUTCOMES = Counter(
    rejected=3265,
    accepted=532,
    **{"accepted-changed": 344},
)
EXPECTED_BENIGN = Counter(
    **{
        "propagated-constant": 266,
        "no-op": 261,
        "unused-store-result-type": 168,
        "unused-identity": 101,
        "propagated-string": 78,
        "direct-load-width-derived-from-object": 2,
    }
)
FIELDS = {
    "nop": ("identity",),
    "const": ("type", "immediate"),
    "load": ("type", "memory_size", "identity"),
    "store": ("type", "memory_size", "src1", "identity"),
    "phi": ("type", "src1", "src2", "identity"),
    "unary": ("type", "immediate", "src1", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "brfalse": ("src1", "identity"),
    "address": ("type", "immediate", "identity"),
    "idxaddr": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity",
    ),
    "loadind": ("type", "memory_size", "src1", "identity"),
    "storeind": (
        "type", "memory_size", "src1", "src2", "identity",
    ),
    "straddr": ("type", "immediate", "identity"),
    "arg": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "call": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
}
VALUES = {
    "type": (-12345, 0, 1, 2, 4, 5, 17, 18, 33, 34, 49, 50, 65, 81, 32767),
    "memory_size": (0, 1, 2, 4, 7),
    "src1": (-1, 0, 999),
    "src2": (-1, 0, 999),
    "immediate": (
        -32768, -1, 0, 1, 2, 3, 4, 6, 8, 15, 16,
        127, 128, 254, 255, 256, 32767, 65535,
    ),
    "identity": (88,),
}
ARGUMENT_TYPES = {
    58: 17, 66: 4, 68: 4,
    71: 17, 79: 4, 81: 4,
    84: 17, 92: 4, 94: 4,
    97: 17, 105: 4, 107: 4,
    159: 17, 169: 4, 171: 4,
    174: 17, 184: 4, 186: 4,
}
ARGUMENT_ORDINALS = {
    58: 0, 66: 1, 68: 2,
    71: 0, 79: 1, 81: 2,
    84: 0, 92: 1, 94: 2,
    97: 0, 105: 1, 107: 2,
    159: 0, 169: 1, 171: 2,
    174: 0, 184: 1, 186: 2,
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
        raise RuntimeError("baseline did not select additive-subscript-runner")
    instructions = []
    inside = False
    for line in report.splitlines():
        if line.startswith(f"; MIR function={FUNCTION} "):
            inside = True
            continue
        if line.startswith(f"; MIR summary function={FUNCTION}"):
            break
        if inside:
            match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
            if match:
                instructions.append(
                    (int(match.group(1)), match.group(2), match.group(3))
                )
    if [item[0] for item in instructions] != list(range(188)):
        raise RuntimeError(
            f"expected instructions 0..187, got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return instructions, output.read_bytes()


def mutation_jobs(instructions):
    return [
        (instruction, opcode, field, value)
        for instruction, opcode, _ in instructions
        for field in FIELDS.get(opcode, ())
        for value in VALUES[field]
    ]


def mutate(root, compiler, work, baseline_bytes, job):
    instruction, opcode, field, value = job
    output_path = work / f"{instruction}-{field}-{value}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output_path), root, env)
    if EXACT_ACCEPT not in report:
        if GENERIC_SELECTION.search(report) is None:
            raise RuntimeError(
                f"{instruction}:{field}:{value} selected neither "
                "exact nor generic MIR"
            )
        output_path.unlink()
        return (*job, "rejected", "")
    changed = output_path.read_bytes() != baseline_bytes
    output_path.unlink()
    return (*job, "accepted-changed" if changed else "accepted", "")


def reported_field_value(instruction, opcode, detail, field):
    if field == "type":
        if opcode == "arg":
            return ARGUMENT_TYPES[instruction]
        match = re.search(r"\btype=(-?\d+)", detail)
        return int(match.group(1)) if match else 0
    if field == "memory_size":
        match = re.search(r"\bmem=(-?\d+)", detail)
        return int(match.group(1)) if match else 0
    if field == "src1":
        patterns = {
            "store": r"^v(-?\d+)\s",
            "phi": r"^v-?\d+ = v(-?\d+),",
            "unary": r"^v-?\d+ = v(-?\d+)\b",
            "binary": r"^v-?\d+ = v(-?\d+),",
            "brfalse": r"^v(-?\d+)\b",
            "idxaddr": r"^v-?\d+ = v(-?\d+),",
            "loadind": r"^v-?\d+ = v(-?\d+)\b",
            "storeind": r"^v(-?\d+),",
            "arg": r"^v(-?\d+)\b",
        }
        match = re.search(patterns.get(opcode, r"$^"), detail)
        return int(match.group(1)) if match else -1
    if field == "src2":
        patterns = {
            "phi": r"^v-?\d+ = v-?\d+,v(-?\d+)\b",
            "binary": r"^v-?\d+ = v-?\d+,v(-?\d+)\b",
            "idxaddr": r"^v-?\d+ = v-?\d+,v(-?\d+)\b",
            "storeind": r"^v-?\d+,v(-?\d+)\b",
        }
        match = re.search(patterns.get(opcode, r"$^"), detail)
        return int(match.group(1)) if match else -1
    if field == "immediate":
        if opcode in ("const", "straddr"):
            match = re.search(
                r"(?:type=-?\d+\s+)?(-?\d+)\s+home=", detail
            )
            return int(match.group(1))
        if opcode in ("unary", "binary"):
            match = re.search(r"\bop=(-?\d+)", detail)
            return int(match.group(1))
        if opcode == "idxaddr":
            match = re.search(r"\bstride=(-?\d+)", detail)
            return int(match.group(1))
        if opcode == "arg":
            return ARGUMENT_ORDINALS[instruction]
        return 0
    return None


def classify(result, instruction_details):
    instruction, opcode, field, value, outcome, _ = result
    if outcome == "rejected":
        return result
    original = reported_field_value(
        instruction, opcode, instruction_details[instruction], field
    )
    if outcome == "accepted-changed":
        if opcode == "const" and field == "immediate":
            classification = "propagated-constant"
        elif opcode == "straddr" and field == "immediate":
            classification = "propagated-string"
        else:
            classification = "meaningful-survivor"
    elif original == value:
        classification = "no-op"
    elif field == "identity":
        classification = "unused-identity"
    elif opcode == "load" and field == "memory_size":
        classification = "direct-load-width-derived-from-object"
    elif opcode in ("store", "storeind") and field == "type":
        classification = "unused-store-result-type"
    elif opcode == "arg" and field == "immediate":
        classification = "canonical-argument-index"
    else:
        classification = "meaningful-survivor"
    return (
        instruction, opcode, field, value,
        outcome, classification,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/additive-wave23-audit"
    )
    parser.add_argument("--discover", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    jobs_limit = min(args.jobs, 2)

    root = Path(__file__).resolve().parents[1]
    compiler = compiler_path(root)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    work = output_dir / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    instructions, baseline_bytes = baseline(
        root, compiler, output_dir / "baseline.MAC"
    )
    jobs = mutation_jobs(instructions)
    results = []
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=jobs_limit
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

    instruction_details = {
        instruction: detail
        for instruction, _, detail in instructions
    }
    results = [
        classify(result, instruction_details)
        for result in results
    ]
    results.sort(key=lambda row: (row[0], row[2], row[3]))
    with (output_dir / "mutation-census.tsv").open(
        "w", newline="", encoding="utf-8"
    ) as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            ("instruction", "opcode", "field", "value",
             "outcome", "classification")
        )
        writer.writerows(results)
    outcomes = Counter(row[4] for row in results)
    benign = Counter(row[5] for row in results if row[4] != "rejected")
    print(f"additive Wave 23 mutations={len(results)} {outcomes}")
    print(f"benign survivors={sum(benign.values())} {benign}")
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")
    if not args.discover:
        if outcomes != EXPECTED_OUTCOMES:
            raise RuntimeError(
                f"unexpected mutation outcomes: {outcomes} "
                f"!= {EXPECTED_OUTCOMES}"
            )
        if benign != EXPECTED_BENIGN:
            raise RuntimeError(
                f"unexpected benign classifications: {benign} "
                f"!= {EXPECTED_BENIGN}"
            )
        if benign.get("meaningful-survivor", 0):
            raise RuntimeError("meaningful mutation survivor")


if __name__ == "__main__":
    main()
