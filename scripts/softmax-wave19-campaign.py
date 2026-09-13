#!/usr/bin/env python3
"""Audit the retained variable-softmax exact schedule semantically."""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
from collections import Counter
from pathlib import Path


FIELDS = {
    "param": ("type", "identity"),
    "load": ("type", "memory_size", "immediate", "identity"),
    "arg": ("type", "immediate", "src1"),
    "address": ("type", "immediate", "identity"),
    "call": ("type", "immediate", "src1", "identity"),
    "store": ("type", "memory_size", "immediate", "src1", "identity"),
    "const": ("type", "immediate"),
    "phi": ("type", "src1", "src2", "identity"),
    "unary": ("type", "immediate", "src1"),
    "binary": ("type", "immediate", "src1", "src2"),
    "brfalse": ("src1",),
    "loadind": ("type", "memory_size", "src1"),
    "idxaddr": (
        "type", "memory_size", "immediate", "src1", "src2", "identity"
    ),
    "storeind": ("type", "memory_size", "src1", "src2"),
    "jump": ("immediate",),
}
FUNCTION = "softmax_wave10"
SOURCE = "tests/mir-clobber/smxw10.c"
ACCEPT = (
    "function=softmax_wave10 template=softmax-schedule accept=emitted"
)
DEAD_LOADS = {23, 25, 93, 95}


def run(command, root, env=None, timeout=60):
    completed = subprocess.run(
        command,
        cwd=root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    if completed.returncode:
        raise RuntimeError(
            f"{' '.join(map(str, command))} failed\n{completed.stdout}"
        )
    return completed.stdout


def compiler_command(compiler, flags=()):
    return [
        str(compiler), *flags, "-stack", "512", "-I", ".", SOURCE,
        "-o", os.devnull,
    ]


def baseline_instructions(root, compiler):
    env = dict(
        os.environ,
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_REPORT="1",
    )
    output = run(compiler_command(compiler), root, env)
    if ACCEPT not in output:
        raise RuntimeError(f"canonical softmax schedule rejected\n{output}")
    instructions = []
    inside = False
    for line in output.splitlines():
        if line.startswith(f"; MIR function={FUNCTION} "):
            inside = True
            continue
        if line.startswith(f"; MIR summary function={FUNCTION}"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if len(instructions) != 132:
        raise RuntimeError(
            f"expected 132 softmax instructions, got {len(instructions)}"
        )
    return instructions


def mutation_jobs(instructions):
    jobs = []
    for instruction, opcode, detail in instructions:
        for field in FIELDS.get(opcode, ()):
            if field == "type":
                current = re.search(r"\btype=(-?\d+)", detail)
                integer = 2 if current and int(current.group(1)) == 1 else 1
                values = (integer, 5)
            elif field == "memory_size":
                current = re.search(r"\bmem=(-?\d+)", detail)
                values = (
                    2 if current and int(current.group(1)) == 1 else 1,
                )
            elif field in ("src1", "src2"):
                values = (999,)
            elif field == "identity":
                values = (120,)
            else:
                values = (999,)
            for value in values:
                jobs.append((instruction, opcode, field, value))
    return jobs


def mutation_survives(root, compiler, job):
    instruction, _, field, value = job
    env = dict(
        os.environ,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
    )
    output = run(compiler_command(compiler), root, env)
    return job if ACCEPT in output else None


def benign_survivor(job):
    instruction, opcode, field, _ = job
    if opcode in ("store", "storeind") and field == "type":
        return "unused-store-result-type"
    if opcode == "load" and field == "memory_size":
        return "load-width-derived-from-object-and-type"
    if instruction in DEAD_LOADS and opcode == "load" and field in (
        "type", "immediate", "identity"
    ):
        return "dead-promoted-load"
    if opcode in ("call", "jump") and field == "immediate":
        return "unused-control-immediate"
    if instruction == 12 and opcode == "store" and field == "immediate":
        return "dead-local-copy-offset"
    return None


def runtime_controls(root, dccmake):
    campaign = root / "build" / "softmax-wave19-campaign"
    shutil.rmtree(campaign, ignore_errors=True)
    for stack_check in (True, False):
        for peep in (True, False):
            name = (
                f"{'stack' if stack_check else 'nostack'}-"
                f"{'peep' if peep else 'nopeep'}"
            )
            build = campaign / name
            build.mkdir(parents=True)
            output = run(
                [
                    str(dccmake),
                    f"dcc-input={SOURCE}",
                    "dcc-output=SMXW19",
                    f"dcc-build-dir={build}",
                    f"dcc-peep={str(peep).lower()}",
                    f"dcc-stack-check={str(stack_check).lower()}",
                    "dcc-stack-bytes=512",
                ],
                root,
                dict(
                    os.environ,
                    DCC_MIR_MACHINE_REPORT="1",
                    DCC_MIR_SELECT_REPORT="1",
                ),
            )
            if ACCEPT not in output:
                raise RuntimeError(
                    f"canonical schedule rejected ({name})\n{output}"
                )
            runtime = run(
                ["ntvcm", "-p", "-s:0", str(build / "SMXW19.COM")],
                root,
                timeout=30,
            )
            if "SMXW10 failures=0" not in runtime:
                raise RuntimeError(
                    f"canonical runtime failed ({name})\n{runtime}"
                )
    shutil.rmtree(campaign, ignore_errors=True)


def near_match_controls(root, compiler):
    rejected = (
        "SMXW19_VOLATILE_VECTOR",
        "SMXW19_VOLATILE_TABLE",
        "SMXW19_FASTCALL_MAXIMUM",
        "SMXW19_SUBTRACT_SUM",
        "SMXW19_HALF_SCALE",
        "SMXW19_SHIFT_TWO",
        "SMXW19_NE_LOOPS",
    )
    generic = rejected + (
        "SMXW19_INDIRECT_MAXIMUM",
        "SMXW19_RETURN_VALUE",
    )
    for define in generic:
        env = dict(
            os.environ,
            DCC_MIR_MACHINE_REPORT="1",
            DCC_MIR_SELECT_REPORT="1",
        )
        output = run(
            compiler_command(compiler, (f"-D{define}",)),
            root,
            env,
        )
        if ACCEPT in output or (
            f"selection function={FUNCTION} selector=spilled-scalar-cfg "
            "result=mir"
        ) not in output:
            raise RuntimeError(
                f"{define} did not use the generic MIR emitter\n{output}"
            )
        if define in rejected and (
            f"function={FUNCTION} template=softmax-schedule reject="
        ) not in output:
            raise RuntimeError(
                f"{define} did not exercise a matcher rejection\n{output}"
            )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--jobs", type=int, default=min(os.cpu_count() or 1, 24)
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = Path(os.environ.get("DCC", root / "dcc"))
    if not compiler.is_absolute():
        compiler = (root / compiler).resolve()
    dccmake = root / ("dccmake.exe" if os.name == "nt" else "dccmake")

    runtime_controls(root, dccmake)
    near_match_controls(root, compiler)
    instructions = baseline_instructions(root, compiler)
    jobs = mutation_jobs(instructions)
    survivors = []
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=args.jobs
    ) as executor:
        futures = [
            executor.submit(mutation_survives, root, compiler, job)
            for job in jobs
        ]
        for future in concurrent.futures.as_completed(futures):
            survivor = future.result()
            if survivor is not None:
                survivors.append(survivor)
    unexpected = [job for job in survivors if benign_survivor(job) is None]
    if unexpected:
        raise RuntimeError(
            f"{len(unexpected)} meaningful mutation survivors: "
            f"{sorted(unexpected)[:20]}"
        )
    classes = Counter(benign_survivor(job) for job in survivors)
    print(f"{len(jobs)} mutations, zero meaningful survivors")
    for name, count in sorted(classes.items()):
        print(f"benign {name}: {count}")


if __name__ == "__main__":
    main()
