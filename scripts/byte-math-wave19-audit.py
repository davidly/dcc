#!/usr/bin/env python3
"""Exhaustively mutate the retained byte-math exact schedule metadata."""

import argparse
import concurrent.futures
import csv
import os
import re
import shutil
import subprocess
from pathlib import Path


EXACT_ACCEPT = (
    "MIR machine function=op_math "
    "template=byte-math-flags accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=op_math "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=op_math "
    r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)

# This is the audited type shape. A -1 entry is a context-assigned pointer to
# the state object and is recovered from the baseline MIR report.
EXPECTED_TYPES = (
    0, 33, 33, 33, 2, 2, 2, 33, 33, 33,
    2, 33, 2, 2, 0, -1, 49, 33, 33, 33,
    33, 3, 0, 0, 0, -1, 22, 6, 0, 2,
    33, 2, 2, 0, 0, 0, 0, 0, 2, 33,
    2, 2, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 33, 33, 33, 33, 3, 0, 0,
    0, 2, 33, 2, 2, 0, 2, 33, 2, 2,
    33, 33, 2, 33, 33, 0, 0, 2, 33, 2,
    2, 0, 0, -1, 49, 33, 34, 33, 34, 34,
    -1, 22, 6, 34, 34, 34, 34, 34, 33, 33,
    33, -1, 22, 2, 34, 34, 34, 34, 2, 6,
    6, -1, 22, -1, 49, 33, 33, 2, 2, 2,
    2, 2, 2, 0, -1, 49, 33, 33, 2, 2,
    2, 2, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 6, -1, 49, 33, 33, 0, 0, 0, 2,
    33, 2, 2, 0, 0, -1, 49, 33, 33, 2,
    2, 33, 33, 0, 0, 2, 33, 2, 2, 0,
    0, -1, 49, 33, 33, 2, 2, 33, 33, 0,
    0, -1, 49, 33, 33, 2, 2, 33, 33, 0,
    0, 0, -1, 22, -1, 49, 33, 2, 2, 2,
    6, 6, -1, 22, -1, 49, 33, 2, 6, 6,
)

IMMEDIATE_OPS = {
    "const", "unary", "binary", "memberaddr", "arg", "call"
}
SRC1_OPS = {
    "unary", "binary", "brfalse", "memberaddr", "loadind", "arg",
    "call", "store", "storeind", "phi", "return"
}
SRC2_OPS = {"binary", "storeind", "phi"}
MEMORY_OPS = {"memberaddr", "load", "loadind", "store", "storeind"}
IDENTITY_OPS = {
    "param", "nop", "address", "memberaddr", "load", "loadind",
    "store", "storeind"
}
IDENTITY_NAME = re.compile(
    r"\b(?:op|rhs|cpu|a|f[A-Z]|res16|result)\b"
)

# These are the only accepted mutations. Identity changes affect diagnostic
# names only; instruction 106 already has memory_size=2, making that mutation
# an intentional no-op. Every accepted output must equal the baseline bytes.
BENIGN_SURVIVORS = {
    (3, "identity", 88): "diagnostic-name-only",
    (8, "identity", 88): "diagnostic-name-only",
    (11, "identity", 88): "diagnostic-name-only",
    (16, "identity", 88): "diagnostic-name-only",
    (17, "identity", 88): "diagnostic-name-only",
    (19, "identity", 88): "diagnostic-name-only",
    (94, "identity", 88): "diagnostic-name-only",
    (95, "identity", 88): "diagnostic-name-only",
    (105, "identity", 88): "diagnostic-name-only",
    (106, "memory_size", 2): "existing-value-no-op",
    (107, "identity", 88): "diagnostic-name-only",
    (109, "identity", 88): "diagnostic-name-only",
    (114, "identity", 88): "diagnostic-name-only",
    (124, "identity", 88): "diagnostic-name-only",
    (125, "identity", 88): "diagnostic-name-only",
    (135, "identity", 88): "diagnostic-name-only",
    (136, "identity", 88): "diagnostic-name-only",
    (137, "identity", 88): "diagnostic-name-only",
    (153, "identity", 88): "diagnostic-name-only",
    (154, "identity", 88): "diagnostic-name-only",
    (155, "identity", 88): "diagnostic-name-only",
    (166, "identity", 88): "diagnostic-name-only",
    (172, "identity", 88): "diagnostic-name-only",
    (182, "identity", 88): "diagnostic-name-only",
    (188, "identity", 88): "diagnostic-name-only",
    (192, "identity", 88): "diagnostic-name-only",
    (198, "identity", 88): "diagnostic-name-only",
    (205, "identity", 88): "diagnostic-name-only",
    (206, "identity", 88): "diagnostic-name-only",
    (215, "identity", 88): "diagnostic-name-only",
    (216, "identity", 88): "diagnostic-name-only",
}


def run(command, root, env, timeout=120):
    completed = subprocess.run(
        command,
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
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
        str(compiler),
        "-stack",
        "512",
        "-I",
        ".",
        "tests/mir-clobber/bmw9.c",
        "-o",
        str(output),
    ]


def baseline_instructions(root, compiler, baseline):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION="op_math",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    output = run(compiler_command(compiler, baseline), root, env)
    if EXACT_ACCEPT not in output or EXACT_SELECTION not in output:
        raise RuntimeError("baseline did not select byte-math-flags")

    instructions = {}
    inside = False
    for line in output.splitlines():
        if line.startswith("; MIR function=op_math "):
            inside = True
            continue
        if line.startswith("; MIR summary function=op_math"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instruction = int(match.group(1))
            instructions[instruction] = (
                match.group(2), match.group(3)
            )
    if tuple(instructions) != tuple(range(220)):
        raise RuntimeError(
            f"expected instructions 0..219, got {len(instructions)}"
        )
    return instructions


def actual_types(instructions):
    types = list(EXPECTED_TYPES)
    if len(types) != 220:
        raise RuntimeError(f"expected 220 type entries, got {len(types)}")
    for instruction, expected in enumerate(types):
        detail = instructions[instruction][1]
        reported = re.search(r"\btype=(\d+)\b", detail)
        if expected == -1:
            if reported is None:
                raise RuntimeError(
                    f"instruction {instruction} has no reported type"
                )
            types[instruction] = int(reported.group(1))
        elif reported is not None and int(reported.group(1)) != expected:
            raise RuntimeError(
                f"instruction {instruction} type changed: "
                f"expected {expected}, got {reported.group(1)}"
            )
    return types


def mutation_jobs(instructions, types):
    jobs = []
    for instruction in range(220):
        opcode, detail = instructions[instruction]
        type_values = (1, 2) if types[instruction] == 33 else (18, 33)
        for value in type_values:
            jobs.append((instruction, "type", value, opcode, detail))
        if opcode in IMMEDIATE_OPS:
            jobs.append(
                (instruction, "immediate", 12345, opcode, detail)
            )
        if opcode in SRC1_OPS:
            jobs.append((instruction, "src1", 999, opcode, detail))
        if opcode in SRC2_OPS:
            jobs.append((instruction, "src2", 999, opcode, detail))
        if opcode in MEMORY_OPS:
            jobs.append(
                (instruction, "memory_size", 2, opcode, detail)
            )
        if opcode in IDENTITY_OPS and IDENTITY_NAME.search(detail):
            jobs.append(
                (instruction, "identity", 88, opcode, detail)
            )
    if len(jobs) != 806:
        raise RuntimeError(f"expected 806 mutations, got {len(jobs)}")
    return jobs


def run_mutation(root, compiler, work, baseline_bytes, job):
    instruction, field, value, opcode, detail = job
    key = (instruction, field, value)
    output_path = work / f"{instruction}-{field}-{value}.asm"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION="op_math",
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
        DCC_MIR_SELECT_REPORT="1",
    )
    output = run(compiler_command(compiler, output_path), root, env)
    accepted = EXACT_SELECTION in output
    if accepted:
        byte_identical = output_path.read_bytes() == baseline_bytes
    else:
        byte_identical = False
        if GENERIC_SELECTION.search(output) is None:
            raise RuntimeError(
                f"{instruction}:{field}:{value} neither selected exact "
                "nor generic MIR"
            )
    output_path.unlink()
    return (
        instruction,
        field,
        value,
        opcode,
        detail,
        accepted,
        byte_identical,
        BENIGN_SURVIVORS.get(key, "semantic-rejection"),
    )


def write_census(path, results):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            (
                "instruction", "field", "value", "opcode", "mir",
                "accepted", "byte_identical", "classification",
            )
        )
        writer.writerows(results)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/byte-math-wave19-audit"
    )
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
    baseline = output_dir / "baseline.asm"

    instructions = baseline_instructions(root, compiler, baseline)
    jobs = mutation_jobs(instructions, actual_types(instructions))
    baseline_bytes = baseline.read_bytes()
    results = []
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs
        ) as executor:
            futures = [
                executor.submit(
                    run_mutation,
                    root,
                    compiler,
                    work,
                    baseline_bytes,
                    job,
                )
                for job in jobs
            ]
            for future in concurrent.futures.as_completed(futures):
                results.append(future.result())
    finally:
        shutil.rmtree(work, ignore_errors=True)

    results.sort(key=lambda row: (row[0], row[1], row[2]))
    write_census(output_dir / "mutation-census.tsv", results)
    accepted = {
        (row[0], row[1], row[2])
        for row in results
        if row[5]
    }
    expected = set(BENIGN_SURVIVORS)
    if accepted != expected:
        raise RuntimeError(
            "benign survivor classification changed: "
            f"missing={sorted(expected - accepted)} "
            f"unexpected={sorted(accepted - expected)}"
        )
    nonidentical = [
        (row[0], row[1], row[2])
        for row in results
        if row[5] and not row[6]
    ]
    if nonidentical:
        raise RuntimeError(
            f"accepted survivors changed assembly: {nonidentical}"
        )
    if len(BENIGN_SURVIVORS) != 31:
        raise RuntimeError("expected exactly 31 classified survivors")

    print(
        f"byte-math Wave 19 audit passed: {len(results)} mutations, "
        f"{len(accepted)} classified byte-identical survivors, "
        f"{len(results) - len(accepted)} semantic rejections"
    )
    print(f"compiler: {compiler}")
    print(f"census: {output_dir / 'mutation-census.tsv'}")


if __name__ == "__main__":
    main()
