#!/usr/bin/env python3
"""Audit buffered-console-runner diagnostic mutations semantically."""

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


ROOT = Path(__file__).resolve().parents[1]
FUNCTION = "main"
SOURCE = "tests/tsvbuf2.c"
EXACT = (
    "MIR machine function=main "
    "template=buffered-console-runner accept=emitted"
)
GENERIC = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
BASELINE_SHA256 = (
    "24ab653c45284767dfd54992436c3a5ef5e9c7036f792901edc41de1ed53fa56"
)
SELECTED_HASH = "7daa8d84"
MUTATIONS = {
    "type": 32767,
    "memory_size": 7,
    "src1": 999,
    "src2": 999,
    "immediate": 123456789,
    "identity": 88,
}
RELEVANT_FIELDS = {
    "straddr": {"type", "immediate"},
    "arg": {"src1"},
    "call": {"type", "src1", "identity"},
    "const": {"type", "immediate"},
    "load": {"type", "memory_size", "identity"},
    "store": {"type", "memory_size", "src1", "identity"},
    "brfalse": {"src1"},
    "binary": {"type", "immediate", "src1", "src2"},
    "indexaddr": {
        "type", "memory_size", "immediate", "src1", "src2"
    },
    "storeind": {"memory_size", "src1", "src2"},
    "phi": {"type", "src1", "src2"},
    "unary": {"type", "immediate", "src1"},
    "return": {"src1"},
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 1884,
    "opcode-unused": 529,
    "diagnostic-identity": 353,
})
FASTCALL_SOURCE_CLASSES = {
    1: "tests/mir-clobber/b27local.c",
    3: "tests/mir-clobber/b27local.c",
    4: "tests/mir-clobber/buf27.c",
    5: "tests/mir-clobber/buf27.c",
    6: "tests/mir-clobber/buf27.c",
    7: "tests/mir-clobber/buf27.c",
    8: "tests/mir-clobber/buf27.c",
    9: "tests/mir-clobber/buf27.c",
    11: "tests/mir-clobber/buf27.c",
    12: "tests/mir-clobber/buf27.c",
    13: "tests/mir-clobber/buf27.c",
    14: "tests/mir-clobber/buf27.c",
}
SOURCE_INVALID_FASTCALL_CLASSES = {
    0: "cannot be variadic",
    2: "at most 3 parameters",
    10: "cannot be variadic",
}


def run_compiler(compiler, output, environment):
    process = subprocess.run(
        [
            str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
            SOURCE, "-o", str(output),
        ],
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    report = process.stdout + process.stderr
    if process.returncode:
        raise RuntimeError(report)
    return report


def parse_instructions(report):
    instructions = {}
    inside = False

    for line in report.splitlines():
        if line.startswith(f"; MIR function={FUNCTION} "):
            instructions = {}
            inside = True
            continue
        if line.startswith(f"; MIR summary function={FUNCTION}"):
            inside = False
            continue
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions[int(match.group(1))] = (
                match.group(2), match.group(3)
            )
    if list(instructions) != list(range(461)):
        raise RuntimeError(
            f"expected 461 buffered-console instructions, "
            f"got {len(instructions)}"
        )
    return instructions


def audit_fastcall_classes(compiler, output_dir):
    rejected = 0
    invalid = 0

    for fastcall_class, source in FASTCALL_SOURCE_CLASSES.items():
        output = output_dir / f"fastcall-{fastcall_class}.MAC"
        environment = os.environ.copy()
        environment.update({
            "DCC_MIR_MACHINE_REPORT": "1",
            "DCC_MIR_SELECT_REPORT": "1",
        })
        process = subprocess.run(
            [
                str(compiler),
                f"-DBUF27_FASTCALL_CLASS={fastcall_class}",
                "-fstack-check", "-stack", "512", "-I", ".",
                source, "-o", str(output),
            ],
            cwd=ROOT,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
            check=False,
        )
        report = process.stdout + process.stderr
        if process.returncode:
            raise RuntimeError(
                f"fastcall class {fastcall_class} failed\n{report}"
            )
        if EXACT in report or GENERIC.search(report) is None:
            raise RuntimeError(
                f"fastcall class {fastcall_class} was not rejected "
                "to generic MIR"
            )
        output.unlink(missing_ok=True)
        rejected += 1

    for fastcall_class, diagnostic in (
            SOURCE_INVALID_FASTCALL_CLASSES.items()):
        output = output_dir / f"fastcall-invalid-{fastcall_class}.MAC"
        process = subprocess.run(
            [
                str(compiler),
                f"-DBUF27_FASTCALL_CLASS={fastcall_class}",
                "-fstack-check", "-stack", "512", "-I", ".",
                "tests/mir-clobber/buf27.c", "-o", str(output),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
            check=False,
        )
        report = process.stdout + process.stderr
        output.unlink(missing_ok=True)
        if process.returncode == 0 or diagnostic not in report:
            raise RuntimeError(
                f"fastcall class {fastcall_class} did not produce "
                f"the expected source diagnostic: {report}"
            )
        invalid += 1
    print(
        "buffered-console-wave27 ABI: "
        f"{rejected} fastcall classes rejected; "
        f"{invalid} source-invalid classes diagnosed"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/buffered-console-wave27-audit"),
    )
    parser.add_argument("--discover", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    compiler = Path(os.environ.get("DCC", ROOT / "dcc")).resolve()
    output_dir = Path(args.output_dir).resolve()
    work_dir = output_dir / "work"
    shutil.rmtree(work_dir, ignore_errors=True)
    work_dir.mkdir(parents=True)

    environment = os.environ.copy()
    environment.update({
        "DCC_MIR_FUNCTION": FUNCTION,
        "DCC_MIR_REPORT": "1",
        "DCC_MIR_MACHINE_REPORT": "1",
        "DCC_MIR_SELECT_REPORT": "1",
    })
    baseline_path = output_dir / "baseline.MAC"
    baseline_report = run_compiler(
        compiler, baseline_path, environment
    )
    if EXACT not in baseline_report or (
            f"selected-hash={SELECTED_HASH}" not in baseline_report):
        raise RuntimeError(
            "buffered-console exact baseline or selected hash changed"
        )
    if hashlib.sha256(baseline_path.read_bytes()).hexdigest() != (
            BASELINE_SHA256):
        raise RuntimeError("buffered-console assembly baseline changed")
    baseline = baseline_path.read_bytes()
    instructions = parse_instructions(baseline_report)
    jobs = [
        (instruction, field, value)
        for instruction in range(461)
        for field, value in MUTATIONS.items()
    ]

    def run_mutation(job):
        instruction, field, value = job
        output = work_dir / f"{instruction}-{field}.MAC"
        mutation_environment = os.environ.copy()
        mutation_environment.update({
            "DCC_MIR_MACHINE_MUTATE_FUNCTION": FUNCTION,
            "DCC_MIR_MACHINE_MUTATE":
                f"{instruction}:{field}:{value}",
            "DCC_MIR_MACHINE_REPORT": "1",
            "DCC_MIR_SELECT_REPORT": "1",
            "DCC_MIR_SELECT_REPORT_FUNCTION": FUNCTION,
        })
        report = run_compiler(
            compiler, output, mutation_environment
        )
        exact = EXACT in report
        if not exact and not GENERIC.search(report):
            raise RuntimeError(
                f"mutation {instruction}:{field}:{value} "
                "had no selector"
            )
        same = exact and output.read_bytes() == baseline
        opcode = instructions[instruction][0]
        relevant = field in RELEVANT_FIELDS.get(opcode, set())
        if exact and not same:
            classification = "changed-output"
        elif exact and relevant:
            classification = "meaningful-survivor"
        elif exact and field == "identity":
            classification = "diagnostic-identity"
        elif exact:
            classification = "opcode-unused"
        else:
            classification = "rejected"
        output.unlink(missing_ok=True)
        return (
            instruction, field, value, opcode,
            instructions[instruction][1], classification,
            "same" if same else "changed",
        )

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs) as executor:
        results = list(executor.map(run_mutation, jobs))
    results.sort(key=lambda row: (row[0], row[1]))
    shutil.rmtree(work_dir, ignore_errors=True)

    census_path = output_dir / "mutation-census.tsv"
    with census_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow([
            "insn", "field", "value", "opcode", "detail",
            "classification", "bytes",
        ])
        writer.writerows(results)
    counts = Counter(row[5] for row in results)
    meaningful = (
        counts["meaningful-survivor"] + counts["changed-output"]
    )
    print(
        f"buffered-console-wave27: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['opcode-unused']} opcode-unused; "
        f"{counts['diagnostic-identity']} diagnostic identities; "
        f"{meaningful} meaningful survivors"
    )
    print(f"census: {census_path}")
    audit_fastcall_classes(compiler, output_dir)
    if not args.discover and (
            meaningful or counts != EXPECTED_CLASSIFICATIONS):
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "buffered-console-wave27: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
