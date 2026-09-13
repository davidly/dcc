#!/usr/bin/env python3
"""Compile-only semantic mutation audit for narrowed-divmod-loop."""

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
FUNCTION = "test_while_register_narrowed"
SOURCE = ROOT / "tests/tdmfuse.c"
EXACT = (
    "MIR machine function=test_while_register_narrowed "
    "template=narrowed-divmod-loop accept=emitted"
)
SELECTED_HASH = "78a8c112"
GENERIC = re.compile(
    r"MIR selection function=test_while_register_narrowed "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
MUTATIONS = {
    "type": (5, 6, 18, 34, 32767),
    "memory_size": (3, 7),
    "src1": (999,),
    "src2": (999,),
    "immediate": (-32768, 32767),
    "identity": (88,),
}
RELEVANT_FIELDS = {
    "address": {"type", "identity"},
    "const": {"type", "immediate"},
    "indexaddr": {"type", "memory_size", "src1", "src2", "immediate"},
    "storeind": {"type", "memory_size", "src1", "src2", "immediate"},
    "store": {"type", "memory_size", "src1", "identity"},
    "binary": {"type", "src1", "src2", "immediate"},
    "unary": {"type", "src1", "immediate"},
    "phi": {"type", "src1", "src2"},
    "brfalse": {"src1"},
    "load": {"type", "memory_size", "identity"},
    "loadind": {"type", "memory_size", "src1", "immediate"},
    "arg": {"type", "src1", "immediate"},
    "straddr": {"type", "immediate"},
    "call": {"type", "identity"},
}
PARAMETERIZED_MUTATIONS = {
    (46, "immediate", -32768),
    (46, "immediate", 32767),
    (125, "immediate", 32767),
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 1071,
    "opcode-unused": 769,
    "diagnostic-identity": 137,
    "parameterized-exact": 3,
})


def run_compiler(compiler, output, environment):
    process = subprocess.run(
        [
            str(compiler), "-fstack-check", "-c", "-I", ".",
            str(SOURCE.relative_to(ROOT)), "-o", str(output),
        ],
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    report = process.stdout + process.stderr
    if process.returncode != 0:
        raise RuntimeError(report)
    return report


def parse_instructions(report):
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
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions[int(match.group(1))] = (
                match.group(2), match.group(3)
            )
    if list(instructions) != list(range(165)):
        raise RuntimeError(
            f"expected 165 narrowed-divmod instructions, "
            f"got {len(instructions)}"
        )
    return instructions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/divmod-wave22-audit"),
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
            "narrowed-divmod exact baseline or selected hash changed"
        )
    baseline = baseline_path.read_bytes()
    instructions = parse_instructions(baseline_report)
    jobs = [
        (instruction, field, value)
        for instruction in range(165)
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
        if exact and job in PARAMETERIZED_MUTATIONS:
            classification = "parameterized-exact"
        elif exact and not same:
            classification = "changed-output"
        elif exact and field in RELEVANT_FIELDS.get(opcode, set()):
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
        f"divmod-wave22: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['opcode-unused']} opcode-unused; "
        f"{counts['diagnostic-identity']} diagnostic-identity; "
        f"{counts['parameterized-exact']} parameterized exact; "
        f"{meaningful} meaningful survivors"
    )
    if meaningful or counts != EXPECTED_CLASSIFICATIONS:
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "divmod-wave22: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
