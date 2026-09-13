#!/usr/bin/env python3
"""Audit the retained LCS dynamic-programming schedule semantically."""

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


FUNCTION = "lcs_wave6"
SOURCE = "tests/mir-clobber/lcsw6.c"
EXACT_ACCEPT = (
    "MIR machine function=lcs_wave6 template=lcs-dp accept=emitted"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=lcs_wave6 "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "62cc6938901f65e8cafc231de7387c05c78de5d3651c1cac48e36ad498ebb243"
)
EXPECTED_OUTCOMES = Counter(rejected=3853, accepted=2474)
EXPECTED_BENIGN = Counter(
    **{
        "unused-opcode-field": 1817,
        "no-op": 475,
        "unused-identity": 161,
        "object-derived-word-width": 21,
    }
)
FIELDS = {
    "nop": ("identity",),
    "param": ("type", "memory_size", "src1", "src2", "identity"),
    "const": ("type", "immediate", "src1", "src2", "identity"),
    "load": ("type", "memory_size", "src1", "src2", "identity"),
    "store": ("type", "memory_size", "src1", "src2", "identity"),
    "phi": ("type", "src1", "src2", "identity"),
    "unary": ("type", "immediate", "src1", "src2", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "brfalse": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "jump": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "address": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "indexaddr": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "loadind": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "storeind": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
    "return": (
        "type", "immediate", "memory_size",
        "src1", "src2", "identity",
    ),
}
VALUES = {
    "type": (
        -12345, 0, 1, 2, 3, 4, 5, 17,
        18, 33, 34, 49, 50, 65, 81, 32767,
    ),
    "memory_size": (0, 1, 2, 4, 7, 18),
    "src1": (-1, 0, 1, 999),
    "src2": (-1, 0, 1, 999),
    "immediate": (
        -32768, -1, 0, 1, 2, 4, 8, 9, 16, 18,
        43, 45, 60, 62, 127, 255, 256, 276, 277, 278, 32767,
    ),
    "identity": (88,),
}
UNUSED_FIELDS = {
    ("address", "immediate"),
    ("address", "memory_size"),
    ("address", "src1"),
    ("address", "src2"),
    ("brfalse", "immediate"),
    ("brfalse", "memory_size"),
    ("brfalse", "src2"),
    ("brfalse", "type"),
    ("const", "src1"),
    ("const", "src2"),
    ("jump", "immediate"),
    ("jump", "memory_size"),
    ("jump", "src1"),
    ("jump", "src2"),
    ("jump", "type"),
    ("load", "src1"),
    ("load", "src2"),
    ("loadind", "immediate"),
    ("loadind", "src2"),
    ("param", "memory_size"),
    ("param", "src1"),
    ("param", "src2"),
    ("return", "immediate"),
    ("return", "memory_size"),
    ("return", "src2"),
    ("return", "type"),
    ("store", "src2"),
    ("store", "type"),
    ("storeind", "immediate"),
    ("storeind", "type"),
    ("unary", "src2"),
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
        raise RuntimeError("baseline did not select lcs-dp")
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
    if [item[0] for item in instructions] != list(range(225)):
        raise RuntimeError(
            f"expected instructions 0..224, got {len(instructions)}"
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


def match_int(pattern, detail, default):
    match = re.search(pattern, detail)
    return int(match.group(1)) if match else default


def reported_field_value(opcode, detail, field):
    if field == "type":
        return match_int(r"\btype=(-?\d+)", detail, 0)
    if field == "memory_size":
        return match_int(r"\bmem=(-?\d+)", detail, 0)
    if field == "immediate":
        if opcode == "const":
            return match_int(
                r"type=-?\d+\s+(-?\d+)", detail, 0
            )
        if opcode in ("unary", "binary"):
            return match_int(r"\bop=(-?\d+)", detail, 0)
        if opcode == "indexaddr":
            return match_int(r"\bstride=(-?\d+)", detail, 0)
        return 0
    if field == "src1":
        patterns = {
            "store": r"^v(-?\d+)\s",
            "phi": r"^v-?\d+ = v(-?\d+),",
            "unary": r"^v-?\d+ = v(-?\d+)",
            "binary": r"^v-?\d+ = v(-?\d+),",
            "brfalse": r"^v(-?\d+)",
            "indexaddr": r"^v-?\d+ = v(-?\d+),",
            "loadind": r"^v-?\d+ = v(-?\d+)",
            "storeind": r"^v(-?\d+),",
            "return": r"^v(-?\d+)",
        }
        return match_int(patterns.get(opcode, r"$^"), detail, -1)
    if field == "src2":
        patterns = {
            "phi": r"^v-?\d+ = v-?\d+,v(-?\d+)",
            "binary": r"^v-?\d+ = v-?\d+,v(-?\d+)",
            "indexaddr": r"^v-?\d+ = v-?\d+,v(-?\d+)",
            "storeind": r"^v-?\d+,v(-?\d+)",
        }
        return match_int(patterns.get(opcode, r"$^"), detail, -1)
    return None


def classify(result, instruction_details):
    instruction, opcode, field, value, outcome, _ = result
    if outcome == "rejected":
        return result
    original = reported_field_value(
        opcode, instruction_details[instruction], field
    )
    if outcome == "accepted-changed":
        classification = "meaningful-survivor"
    elif original == value:
        classification = "no-op"
    elif field == "identity":
        classification = "unused-identity"
    elif (
        (opcode, field) in {
            ("load", "memory_size"),
            ("store", "memory_size"),
        }
    ):
        classification = "object-derived-word-width"
    elif (opcode, field) in UNUSED_FIELDS:
        classification = "unused-opcode-field"
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
        "--output-dir", default="build/lcs-wave24-audit"
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
    print(f"LCS Wave 24 mutations={len(results)} {outcomes}")
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
