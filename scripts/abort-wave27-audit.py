#!/usr/bin/env python3
"""Compile-only semantic mutation audit for abort-file-runner."""

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
EXACT = (
    "MIR machine function=main "
    "template=abort-file-runner accept=emitted"
)
GENERIC = re.compile(
    r"MIR selection function=main "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
SHAPES = (
    ("reachable", ROOT / "tests/mir-clobber/abortfil.c", 264, "c88f2aa5"),
    ("post-abort", ROOT / "tests/mir-clobber/abortw13.c", 269, "0a9ac45c"),
)
MUTATIONS = {
    "type": 32767,
    "memory_size": 32,
    "src1": 999,
    "src2": 999,
    "immediate": 999,
    "identity": 88,
}
BENIGN_OPCODES = {
    "type": {"label", "nop", "brfalse", "jump", "return"},
    "memory_size": {
        "label", "straddr", "arg", "call", "nop", "const",
        "unary", "brfalse", "binary", "jump", "address", "phi",
        "return",
    },
    "src1": {
        "label", "straddr", "nop", "load", "const", "jump", "address",
    },
    "src2": {
        "label", "straddr", "arg", "call", "nop", "store", "load",
        "const", "unary", "brfalse", "jump", "address", "return",
    },
    "immediate": {
        "label", "call", "brfalse", "nop", "jump", "phi", "return",
    },
    "identity": {
        "label", "straddr", "arg", "const", "unary", "brfalse",
        "binary", "nop", "jump", "phi", "return",
    },
}
EXPECTED_CLASSIFICATIONS = Counter({
    "rejected": 1201,
    "benign-unused-field": 1587,
    "diagnostic-identity": 410,
})


def run_compiler(compiler, source, output, environment):
    process = subprocess.run(
        [
            str(compiler), "-c", str(source.relative_to(ROOT)),
            "-o", str(output),
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


def parse_final_instructions(report, count):
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
    if list(instructions) != list(range(count)):
        raise RuntimeError(
            f"expected {count} abort instructions, "
            f"got {len(instructions)}"
        )
    return instructions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default=str(ROOT / "build/abort-wave27-audit"),
    )
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 2:
        parser.error("--jobs must be between 1 and 2")

    compiler = Path(os.environ.get("DCC", ROOT / "dcc")).resolve()
    output_dir = Path(args.output_dir).resolve()
    work_dir = output_dir / "work"
    shutil.rmtree(work_dir, ignore_errors=True)
    work_dir.mkdir(parents=True)

    baselines = {}
    instructions = {}
    jobs = []
    for shape, source, count, selected_hash in SHAPES:
        environment = os.environ.copy()
        environment.update({
            "DCC_MIR_FUNCTION": FUNCTION,
            "DCC_MIR_REPORT": "1",
            "DCC_MIR_MACHINE_REPORT": "1",
            "DCC_MIR_SELECT_REPORT": "1",
        })
        baseline_path = output_dir / f"{shape}-baseline.MAC"
        report = run_compiler(
            compiler, source, baseline_path, environment
        )
        if EXACT not in report or (
                f"selected-hash={selected_hash}" not in report):
            raise RuntimeError(
                f"{shape} abort exact baseline or hash changed"
            )
        baselines[shape] = baseline_path.read_bytes()
        instructions[shape] = parse_final_instructions(report, count)
        jobs.extend(
            (shape, source, instruction, field, value)
            for instruction in range(count)
            for field, value in MUTATIONS.items()
        )

    def run_mutation(job):
        shape, source, instruction, field, value = job
        output = work_dir / (
            f"{shape}-{instruction}-{field}-{value}.MAC"
        )
        environment = os.environ.copy()
        environment.update({
            "DCC_MIR_MACHINE_MUTATE_FUNCTION": FUNCTION,
            "DCC_MIR_MACHINE_MUTATE":
                f"{instruction}:{field}:{value}",
            "DCC_MIR_MACHINE_REPORT": "1",
            "DCC_MIR_SELECT_REPORT": "1",
            "DCC_MIR_SELECT_REPORT_FUNCTION": FUNCTION,
        })
        report = run_compiler(
            compiler, source, output, environment
        )
        exact = EXACT in report
        if not exact and not GENERIC.search(report):
            raise RuntimeError(
                f"{shape} mutation "
                f"{instruction}:{field}:{value} had no selector"
            )
        same = exact and output.read_bytes() == baselines[shape]
        opcode = instructions[shape][instruction][0]
        if exact and not same:
            classification = "changed-output"
        elif exact and opcode not in BENIGN_OPCODES[field]:
            classification = "meaningful-survivor"
        elif exact and field == "identity":
            classification = "diagnostic-identity"
        elif exact:
            classification = "benign-unused-field"
        else:
            classification = "rejected"
        output.unlink(missing_ok=True)
        return (
            shape, instruction, field, value, opcode,
            instructions[shape][instruction][1],
            classification, "same" if same else "changed",
        )

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs) as executor:
        results = list(executor.map(run_mutation, jobs))
    results.sort(key=lambda row: (row[0], row[1], row[2], row[3]))
    shutil.rmtree(work_dir, ignore_errors=True)

    census_path = output_dir / "census.tsv"
    with census_path.open("w", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow([
            "shape", "insn", "field", "value", "opcode",
            "detail", "classification", "bytes",
        ])
        writer.writerows(results)
    counts = Counter(row[6] for row in results)
    meaningful = (
        counts["meaningful-survivor"] +
        counts["changed-output"]
    )
    print(
        f"abort-wave27: {len(results)} mutations; "
        f"{counts['rejected']} rejected; "
        f"{counts['benign-unused-field']} benign unused fields; "
        f"{counts['diagnostic-identity']} diagnostic identities; "
        f"{meaningful} meaningful survivors"
    )
    if meaningful or counts != EXPECTED_CLASSIFICATIONS:
        if counts != EXPECTED_CLASSIFICATIONS:
            print(
                "abort-wave27: classification drift: "
                f"expected {dict(EXPECTED_CLASSIFICATIONS)}, "
                f"got {dict(counts)}"
            )
        raise SystemExit(1)


if __name__ == "__main__":
    main()
