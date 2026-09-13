#!/usr/bin/env python3
"""Compile-only semantic mutation audit for catalan-driver-schedule."""

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
SOURCE = ROOT / "tests/catalan.c"
EXACT = (
    "MIR machine function=main "
    "template=catalan-driver-schedule accept=emitted"
)
SELECTED_HASH = "fab61cdb"
FULL_IO_HASH = "4728f29d"
GENERIC = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
MUTATIONS = {
    "type": (1, 5, 6, 18, 34, 32767),
    "memory_size": (1, 3, 7),
    "src1": (999,),
    "src2": (999,),
    "immediate": (-32768, 32767),
    "identity": (88,),
}
RELEVANT_FIELDS = {
    "address": {"type", "identity"},
    "arg": {"src1", "immediate"},
    "call": {"type", "src1", "identity"},
    "const": {"type", "immediate"},
    "binary": {"type", "src1", "src2", "immediate"},
    "unary": {"type", "src1", "immediate"},
    "brfalse": {"src1"},
    "load": {"type", "identity", "immediate"},
    "loadind": {"type", "src1", "immediate", "memory_size"},
    "store": {"type", "src1", "identity", "memory_size"},
    "storeind": {
        "type", "src1", "src2", "immediate", "memory_size",
    },
    "indexaddr": {
        "type", "src1", "src2", "immediate", "memory_size",
    },
    "straddr": {"type", "immediate"},
    "phi": {"type", "src1", "src2"},
    "return": {"src1"},
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 3444,
    "opcode-unused": 2313,
    "diagnostic-identity": 361,
})


def run_compiler(compiler, output, environment, extra_args=()):
    process = subprocess.run(
        [
            str(compiler), "-fstack-check", "-c", "-I", ".",
            *extra_args,
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
    if list(instructions) != list(range(437)):
        raise RuntimeError(
            f"expected 437 Catalan instructions, got {len(instructions)}"
        )
    return instructions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/catalan-wave23-audit"),
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
            "Catalan exact baseline or selected hash changed"
        )
    baseline = baseline_path.read_bytes()
    instructions = parse_final_instructions(baseline_report)
    floatio_report = run_compiler(
        compiler, output_dir / "floatio.MAC", environment,
        ("-ffloatio", "-flongio"),
    )
    if EXACT not in floatio_report or (
            f"selected-hash={FULL_IO_HASH}" not in floatio_report):
        raise RuntimeError(
            "Catalan full printf exact control or selected hash changed"
        )
    jobs = [
        (instruction, field, value)
        for instruction in range(437)
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
        f"catalan-wave23: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['opcode-unused']} opcode-unused; "
        f"{counts['diagnostic-identity']} diagnostic-identity; "
        f"{meaningful} meaningful survivors"
    )
    if meaningful or counts != EXPECTED_CLASSIFICATIONS:
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "catalan-wave23: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
