#!/usr/bin/env python3
"""Mutation census for retained fixed-array affine-fill schedules."""

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


SOURCE = "tests/tarray6.c"
FUNCTIONS = ("fill_i", "fill_c", "fill_l")
INSTRUCTION_COUNTS = {"fill_i": 156, "fill_c": 157, "fill_l": 157}
BASELINE_SHA256 = (
    "7bfbdfbe324256fca020f2d1ba9308a3ef7372a3f8c028a609c6b60570013b1d"
)
FIELD_VALUES = {
    "type": (
        0, 1, 2, 3, 4, 5, 6, 17, 18, 19, 33, 34, 35, 49, 50,
        145, 146, 147, 912, 32767,
    ),
    "memory_size": (0, 1, 2, 4, 8, 32, 64, 128, 255),
    "src1": tuple(range(-1, 110)) + (999,),
    "src2": tuple(range(-1, 110)) + (999,),
    "immediate": (
        -32768, -1, 0, 1, 2, 3, 4, 5, 6, 8, 16, 32, 43,
        45, 60, 61, 62, 64, 94, 124, 128, 276, 277, 282, 283,
        32767,
    ),
    "identity": (88,),
}
UNUSED_FIELDS = {
    "type": {"nop", "label", "brfalse", "jump", "store"},
    "memory_size": {
        "nop", "label", "param", "const", "load", "phi", "unary",
        "binary", "brfalse", "jump", "arg", "call",
    },
    "src1": {"nop", "label", "param", "const", "load", "call"},
    "src2": {
        "nop", "label", "param", "const", "load", "store", "unary",
        "brfalse", "jump", "arg", "call",
    },
    "immediate": {
        "nop", "label", "load", "store", "phi", "brfalse", "jump",
        "call", "storeind",
    },
    "identity": {
        "nop", "label", "const", "unary", "binary", "brfalse",
        "jump", "indexaddr", "arg", "storeind",
    },
}
EXPECTED = {
    "fill_i": {
        "outcomes": {"rejected": 9953, "accepted": 33727},
        "classes": {
            "baseline-no-op": 748,
            "equivalent-constant-or-rhs": 38,
            "equivalent-index-value": 35,
            "equivalent-left-value": 29,
            "equivalent-local-value": 22,
            "retained-integer-type": 134,
            "unused-field": 32721,
        },
    },
    "fill_c": {
        "outcomes": {"rejected": 10107, "accepted": 33853},
        "classes": {
            "baseline-no-op": 750,
            "equivalent-constant-or-rhs": 38,
            "equivalent-index-value": 35,
            "equivalent-left-value": 29,
            "equivalent-local-value": 22,
            "retained-integer-type": 136,
            "unused-field": 32843,
        },
    },
    "fill_l": {
        "outcomes": {"rejected": 10117, "accepted": 33843},
        "classes": {
            "baseline-no-op": 750,
            "equivalent-constant-or-rhs": 38,
            "equivalent-index-value": 35,
            "equivalent-left-value": 29,
            "equivalent-local-value": 22,
            "retained-integer-type": 126,
            "unused-field": 32843,
        },
    },
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
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        SOURCE, "-o", str(output),
    ]


def exact_selection(function):
    return (
        f"MIR selection function={function} "
        "selector=scheduled-machine-cfg result=mir"
    )


def generic_selection(function):
    return re.compile(
        rf"MIR selection function={function} "
        r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
        r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
    )


def baseline(root, compiler, output, function):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=function,
        DCC_MIR_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), root, env)
    if exact_selection(function) not in report:
        raise RuntimeError(
            f"{function} did not select fixed-array-affine-fill"
        )
    instructions = {}
    inside = False
    for line in report.splitlines():
        if line.startswith(f"; MIR function={function} "):
            inside = True
            continue
        if line.startswith(f"; MIR summary function={function}"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+", line)
        if match:
            instructions[int(match.group(1))] = match.group(2)
    expected_count = INSTRUCTION_COUNTS[function]
    if tuple(instructions) != tuple(range(expected_count)):
        raise RuntimeError(
            f"{function}: expected instructions 0..{expected_count - 1}, "
            f"got {len(instructions)}"
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"{function} baseline changed: "
            f"{digest} != {BASELINE_SHA256}"
        )
    return instructions, output.read_bytes()


def classify(opcode, field, changed):
    if changed:
        raise RuntimeError(
            f"unclassified output-changing survivor: {opcode}:{field}"
        )
    if opcode in UNUSED_FIELDS.get(field, set()):
        return "unused-field"
    if field == "type":
        return "retained-integer-type"
    if field in ("src1", "src2") and opcode in ("arg", "indexaddr"):
        return "equivalent-index-value"
    if field == "src1" and opcode == "binary":
        return "equivalent-left-value"
    if field == "src2" and opcode == "binary":
        return "equivalent-constant-or-rhs"
    if field == "src1" and opcode == "store":
        return "equivalent-local-value"
    return "baseline-no-op"


def mutate(
    root, compiler, work, baseline_bytes, opcodes,
    function, job,
):
    instruction, field, value = job
    output = work / f"{function}-{instruction}-{field}-{value}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=function,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), root, env)
    if exact_selection(function) not in report:
        if generic_selection(function).search(report) is None:
            raise RuntimeError(
                f"{function}:{instruction}:{field}:{value} selected "
                "neither exact nor generic MIR"
            )
        output.unlink()
        return (
            function, *job, opcodes[instruction], "rejected", "",
        )
    changed = output.read_bytes() != baseline_bytes
    classification = classify(opcodes[instruction], field, changed)
    output.unlink()
    return (
        function, *job, opcodes[instruction], "accepted",
        classification,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument(
        "--functions", default=",".join(FUNCTIONS),
        help="comma-separated subset of fill_i,fill_c,fill_l",
    )
    parser.add_argument(
        "--output-dir", default="build/affine-fill-wave25-audit"
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    selected = tuple(
        name.strip() for name in args.functions.split(",")
        if name.strip()
    )
    if not selected or any(name not in FUNCTIONS for name in selected):
        parser.error("--functions contains an unknown affine fill")

    root = Path(__file__).resolve().parents[1]
    compiler = compiler_path(root)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    work = output_dir / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)

    baselines = {}
    for function in selected:
        baselines[function] = baseline(
            root, compiler, output_dir / f"{function}.MAC", function
        )
    jobs = [
        (
            function,
            (instruction, field, value),
        )
        for function in selected
        for instruction in range(INSTRUCTION_COUNTS[function])
        for field, values in FIELD_VALUES.items()
        for value in values
    ]
    results = []
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs
        ) as executor:
            futures = [
                executor.submit(
                    mutate,
                    root,
                    compiler,
                    work,
                    baselines[function][1],
                    baselines[function][0],
                    function,
                    job,
                )
                for function, job in jobs
            ]
            for future in concurrent.futures.as_completed(futures):
                results.append(future.result())
    finally:
        shutil.rmtree(work, ignore_errors=True)

    results.sort(key=lambda row: (row[0], row[1], row[2], row[3]))
    with (output_dir / "mutation-census.tsv").open(
        "w", newline="", encoding="utf-8"
    ) as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            (
                "function", "instruction", "field", "value", "opcode",
                "outcome", "classification",
            )
        )
        writer.writerows(results)

    for function in selected:
        rows = [row for row in results if row[0] == function]
        outcomes = Counter(row[5] for row in rows)
        classes = Counter(
            row[6] for row in rows if row[5] == "accepted"
        )
        if outcomes != EXPECTED[function]["outcomes"]:
            raise RuntimeError(
                f"{function} mutation outcome changed: {outcomes}"
            )
        if classes != EXPECTED[function]["classes"]:
            raise RuntimeError(
                f"{function} benign classification changed: {classes}"
            )
        print(
            f"{function}: {len(rows)} mutations, "
            f"{outcomes['rejected']} semantic rejections, "
            f"{outcomes['accepted']} classified survivors"
        )
        print("  classes: " + ", ".join(
            f"{name}={count}"
            for name, count in sorted(classes.items())
        ))
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")


if __name__ == "__main__":
    main()
