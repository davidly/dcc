#!/usr/bin/env python3
"""Audit the retained pointer-condition exact schedule semantically."""

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
SOURCE = "tests/tptrcnd.c"
EXACT_ACCEPT = (
    "MIR machine function=main "
    "template=pointer-condition-main accept=emitted"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "e1f98dfea6ad57a08642952881910d7da8d2eea0494a6044fc7144e6191cc080"
)
EXPECTED_OUTCOMES = Counter(
    rejected=5232,
    accepted=9406,
    **{"accepted-changed": 68},
)
EXPECTED_BENIGN = Counter(
    **{
        "unused-opcode-field": 6867,
        "unused-identity": 2066,
        "no-op": 473,
        "propagated-string": 68,
    }
)
FIELDS = (
    "type", "immediate", "memory_size", "src1", "src2", "identity"
)
VALUES = {
    "type": 32767,
    "immediate": 0,
    "memory_size": 7,
    "src1": 1734,
    "src2": 1734,
    "identity": 88,
}
RELEVANT_FIELDS = {
    "straddr": {"type"},
    "arg": {"type", "immediate", "src1"},
    "call": {"type", "identity"},
    "address": {"type", "identity"},
    "const": {"type", "immediate"},
    "indexaddr": {
        "type", "immediate", "memory_size", "src1", "src2"
    },
    "memberaddr": {
        "type", "immediate", "memory_size", "src1"
    },
    "load": {"type", "memory_size", "identity"},
    "store": {"memory_size", "src1", "identity"},
    "loadind": {"type", "memory_size", "src1"},
    "storeind": {"memory_size", "src1", "src2"},
    "phi": {"type", "src1", "src2"},
    "unary": {"type", "immediate", "src1"},
    "binary": {"type", "immediate", "src1", "src2"},
    "brfalse": {"src1"},
    "return": {"type", "src1"},
}


def run(command, root, env):
    completed = subprocess.run(
        command,
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
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
        raise RuntimeError(
            "baseline did not select pointer-condition-main"
        )
    instructions = []
    inside = False
    for line in report.splitlines():
        if line.startswith("; MIR function=main "):
            if instructions:
                break
            inside = True
            continue
        if line.startswith("; MIR summary function=main"):
            break
        if inside:
            match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
            if match:
                instructions.append(
                    (int(match.group(1)), match.group(2), match.group(3))
                )
    if [item[0] for item in instructions] != list(range(2451)):
        raise RuntimeError(
            f"expected instructions 0..2450, got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return instructions, output.read_bytes()


def mutation_jobs(instructions):
    return [
        (instruction, opcode, field, VALUES[field])
        for instruction, opcode, _ in instructions
        for field in FIELDS
    ]


def mutate(root, compiler, work, baseline_bytes, job):
    instruction, opcode, field, value = job
    output_path = work / f"{instruction}-{field}.MAC"
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


def classify(result):
    instruction, opcode, field, value, outcome, _ = result
    if outcome == "rejected":
        return result
    if outcome == "accepted-changed":
        classification = (
            "propagated-string"
            if opcode == "straddr" and field == "immediate"
            else "meaningful-survivor"
        )
    elif field == "identity":
        classification = "unused-identity"
    elif field not in RELEVANT_FIELDS.get(opcode, set()):
        classification = "unused-opcode-field"
    else:
        classification = "no-op"
    return (
        instruction, opcode, field, value,
        outcome, classification,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default="build/pointer-condition-wave25-audit",
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

    results = [classify(result) for result in results]
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
    print(f"pointer-condition Wave 25 mutations={len(results)} {outcomes}")
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
