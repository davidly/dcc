#!/usr/bin/env python3
"""Compile-only semantic mutation audit for action-decode-schedule."""

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
FUNCTION = "decode_action"
SOURCE = ROOT / "tests/forint.c"
EXACT = (
    "MIR machine function=decode_action "
    "template=action-decode-schedule accept=emitted"
)
SELECTED_HASH = "982ad63a"
GENERIC = re.compile(
    r"MIR selection function=decode_action "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
MUTATIONS = {
    "type": (1, 5, 6, 20, 34, 32767),
    "memory_size": (1, 3, 4, 7),
    "src1": (999,),
    "src2": (999,),
    "immediate": (-32768, 999, 32767),
    "identity": (88,),
}
PARAMETERIZED_CONSTANTS = {8, 40, 59, 74}
DIAGNOSTIC_IDENTITY_OPCODES = {
    "arg", "brfalse", "const", "jump", "label", "nop",
    "phi", "return", "storeind", "straddr",
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 1213,
    "diagnostic-identity": 55,
    "parameterized-constant": 12,
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
    if list(instructions) != list(range(80)):
        raise RuntimeError(
            f"expected 80 action decode instructions, "
            f"got {len(instructions)}"
        )
    return instructions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/action-wave24-audit"),
    )
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
            "action decode exact baseline or selected hash changed"
        )
    baseline = baseline_path.read_bytes()
    instructions = parse_final_instructions(baseline_report)
    jobs = [
        (instruction, field, value)
        for instruction in range(80)
        for field, values in MUTATIONS.items()
        for value in values
    ]

    def run_mutation(job):
        instruction, field, value = job
        output = work_dir / f"{instruction}-{field}-{value}.MAC"
        mutation_environment = os.environ.copy()
        mutation_environment.update({
            "DCC_MIR_FUNCTION": FUNCTION,
            "DCC_MIR_MACHINE_MUTATE_FUNCTION": FUNCTION,
            "DCC_MIR_MACHINE_MUTATE":
                f"{instruction}:{field}:{value}",
            "DCC_MIR_MACHINE_REPORT": "1",
            "DCC_MIR_SELECT_REPORT": "1",
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
        if exact and not same:
            if (instruction in PARAMETERIZED_CONSTANTS and
                    opcode == "const" and field == "immediate"):
                classification = "parameterized-constant"
            else:
                classification = "changed-output"
        elif exact and field == "identity" and (
                opcode in DIAGNOSTIC_IDENTITY_OPCODES):
            classification = "diagnostic-identity"
        elif exact:
            classification = "meaningful-survivor"
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
        f"action-wave24: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['diagnostic-identity']} diagnostic-identity; "
        f"{counts['parameterized-constant']} parameterized-constant; "
        f"{meaningful} meaningful survivors"
    )
    if meaningful or counts != EXPECTED_CLASSIFICATIONS:
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "action-wave24: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
