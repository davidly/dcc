#!/usr/bin/env python3
"""Audit the retained matrix-product-store exact schedule semantically."""

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


FUNCTION = "matrix_product_store"
SOURCE = "tests/mir-clobber/mst4pf.c"
EXACT_ACCEPT = (
    "MIR machine function=matrix_product_store "
    "template=matrix-product-store-schedule accept=emitted"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=matrix_product_store "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "8baa72fec1c210aeb1ee7c6bf9750b70a9ce7de9ae7b56e1bbdbdaaa8baec60d"
)
FIELDS = (
    "type", "immediate", "memory_size", "src1", "src2", "identity"
)
VALUES = {
    "type": 32767,
    "immediate": 123456789,
    "memory_size": 7,
    "src1": 999,
    "src2": 999,
    "identity": 88,
}
RELEVANT_FIELDS = {
    "param": {
        "type", "immediate", "memory_size", "src1", "src2", "identity"
    },
    "const": {"type", "immediate"},
    "load": {"type", "memory_size", "identity"},
    "store": {"type", "memory_size", "src1", "identity"},
    "loadind": {"type", "memory_size", "src1"},
    "storeind": {"type", "memory_size", "src1", "src2"},
    "indexaddr": {
        "type", "immediate", "memory_size", "src1", "src2"
    },
    "phi": {"type", "src1", "src2"},
    "unary": {"type", "immediate", "src1"},
    "binary": {"type", "immediate", "src1", "src2"},
    "brfalse": {"src1"},
}
EXPECTED_OUTCOMES = Counter(rejected=273, accepted=507)
EXPECTED_BENIGN = Counter(
    **{
        "opcode-unused": 406,
        "diagnostic-identity": 101,
    }
)


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
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        SOURCE, "-o", str(output),
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
            "baseline did not select matrix-product-store-schedule"
        )
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
    if [item[0] for item in instructions] != list(range(130)):
        raise RuntimeError(
            f"expected instructions 0..129, got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return instructions, output.read_bytes()


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
    return (
        *job,
        "accepted-changed" if changed else "accepted",
        "",
    )


def classify(result):
    instruction, opcode, field, value, outcome, _ = result
    if outcome == "rejected":
        return result
    if outcome == "accepted-changed":
        classification = "meaningful-survivor"
    elif field in RELEVANT_FIELDS.get(opcode, set()):
        classification = "meaningful-survivor"
    elif field == "identity":
        classification = "diagnostic-identity"
    else:
        classification = "opcode-unused"
    return (
        instruction, opcode, field, value, outcome, classification
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/matrix-store-wave26-audit",
    )
    parser.add_argument("--discover", action="store_true")
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
    instructions, baseline_bytes = baseline(
        root, compiler, output_dir / "baseline.MAC"
    )
    jobs = [
        (instruction, opcode, field, VALUES[field])
        for instruction, opcode, _ in instructions
        for field in FIELDS
    ]
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

    results = [classify(result) for result in results]
    results.sort(key=lambda row: (row[0], row[2]))
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
    print(f"matrix-store Wave 26 mutations={len(results)} {outcomes}")
    print(f"accepted classifications={sum(benign.values())} {benign}")
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
                f"unexpected accepted classifications: {benign} "
                f"!= {EXPECTED_BENIGN}"
            )
        if benign.get("meaningful-survivor", 0):
            raise RuntimeError("meaningful mutation survivor")


if __name__ == "__main__":
    main()
