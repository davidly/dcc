#!/usr/bin/env python3
"""Compile-only semantic mutation audit for for-increment-runner."""

import argparse
import concurrent.futures
import csv
import os
import re
import shutil
import subprocess
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTION = "main"
SOURCE = ROOT / "tests/tforinc.c"
EXACT = (
    "MIR machine function=main "
    "template=for-increment-runner accept=emitted"
)
SELECTED_HASH = "127b9826"
GENERIC = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
MUTATIONS = {
    "type": 32767,
    "memory_size": 32,
    "src1": 999,
    "src2": 999,
    "immediate": 999,
    "identity": 88,
}
BENIGN_FIELDS = {
    "type": {
        0, 31, 33, 35, 37, 39, 41, 43, 46, 49, 50, 53, 54, 56,
        57, 59, 61, 62, 65, 66, 68, 69, 71, 73, 74, 77, 78, 80,
        81, 83, 85, 86, 89, 90, 92, 93, 95, 97, 98, 101, 102,
        104, 105, 107, 109, 110, 113, 114, 116, 117, 119, 122,
    },
    "memory_size": set(range(123)) - {4, 8, 14, 20, 24, 26, 28},
    "src1": {
        0, 1, 5, 9, 11, 15, 17, 21, 29, 31, 33, 35, 37, 39, 41,
        43, 46, 47, 50, 51, 54, 55, 56, 57, 58, 59, 62, 63, 66,
        67, 68, 69, 70, 71, 74, 75, 78, 79, 80, 81, 82, 83, 86,
        87, 90, 91, 92, 93, 94, 95, 98, 99, 102, 103, 104, 105,
        106, 107, 110, 111, 114, 115, 116, 117, 118, 119,
    },
    "src2": {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        44, 45, 46, 47, 49, 50, 51, 53, 54, 55, 56, 57, 58, 59,
        61, 62, 63, 65, 66, 67, 68, 69, 70, 71, 73, 74, 75, 77,
        78, 79, 80, 81, 82, 83, 85, 86, 87, 89, 90, 91, 92, 93,
        94, 95, 97, 98, 99, 101, 102, 103, 104, 105, 106, 107,
        109, 110, 111, 113, 114, 115, 116, 117, 118, 119, 121,
        122,
    },
    "immediate": {
        0, 3, 7, 13, 19, 23, 25, 27, 45, 49, 53, 54, 56, 57,
        59, 60, 61, 65, 66, 68, 69, 71, 72, 73, 77, 78, 80, 81,
        83, 84, 85, 89, 90, 92, 93, 95, 96, 97, 101, 102, 104,
        105, 107, 108, 109, 113, 114, 116, 117, 119, 120, 122,
    },
    "identity": {
        0, 1, 2, 5, 6, 9, 10, 11, 12, 15, 16, 17, 18, 21, 22,
        29, 30, 32, 34, 36, 38, 40, 42, 44, 47, 48, 49, 51, 52,
        53, 54, 55, 56, 57, 58, 59, 60, 61, 63, 64, 65, 66, 67,
        68, 69, 70, 71, 72, 73, 75, 76, 77, 78, 79, 80, 81, 82,
        83, 84, 85, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
        99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
        111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
        122,
    },
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 248,
    "benign-unused-field": 396,
    "diagnostic-identity": 94,
})


def run_compiler(compiler, output, environment):
    process = subprocess.run(
        [
            str(compiler), "-c", "-fno-floatio", "-fno-longio",
            str(SOURCE.relative_to(ROOT)), "-o", str(output),
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
    if process.returncode != 0:
        raise RuntimeError(report)
    return report


def parse_final_instructions(report):
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
    if list(instructions) != list(range(123)):
        raise RuntimeError(
            f"expected 123 for-increment instructions, "
            f"got {len(instructions)}"
        )
    return instructions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/for-increment-wave26-audit"),
    )
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 2:
        parser.error("--jobs must be between 1 and 2")

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
            "for-increment exact baseline or selected hash changed"
        )
    baseline = baseline_path.read_bytes()
    instructions = parse_final_instructions(baseline_report)
    jobs = [
        (instruction, field, value)
        for instruction in range(123)
        for field, value in MUTATIONS.items()
    ]

    def run_mutation(job):
        instruction, field, value = job
        output = work_dir / f"{instruction}-{field}-{value}.MAC"
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
        expected_benign = instruction in BENIGN_FIELDS[field]
        if exact and not same:
            classification = "changed-output"
        elif exact and not expected_benign:
            classification = "meaningful-survivor"
        elif exact and field == "identity":
            classification = "diagnostic-identity"
        elif exact:
            classification = "benign-unused-field"
        else:
            classification = "rejected"
        output.unlink(missing_ok=True)
        return (
            instruction, field, value, instructions[instruction][0],
            instructions[instruction][1], classification,
            "same" if same else "changed",
        )

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs) as executor:
        results = list(executor.map(run_mutation, jobs))
    results.sort(key=lambda row: (row[0], row[1], row[2]))
    shutil.rmtree(work_dir, ignore_errors=True)

    census_path = output_dir / "census.tsv"
    with census_path.open("w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow([
            "insn", "field", "value", "opcode", "detail",
            "classification", "bytes",
        ])
        writer.writerows(results)
    counts = Counter(row[5] for row in results)
    meaningful = (
        counts["meaningful-survivor"] +
        counts["changed-output"]
    )
    print(
        f"for-increment-wave26: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['benign-unused-field']} benign unused fields; "
        f"{counts['diagnostic-identity']} diagnostic identities; "
        f"{meaningful} meaningful survivors"
    )
    if meaningful or counts != EXPECTED_CLASSIFICATIONS:
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "for-increment-wave26: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
