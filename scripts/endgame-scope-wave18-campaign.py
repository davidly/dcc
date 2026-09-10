#!/usr/bin/env python3
"""Exercise both retained endgame-scope declaration-policy variants."""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
from pathlib import Path


FIELD_VALUES = {
    "type": 1,
    "immediate": 999,
    "memory_size": 1,
    "src1": 999,
    "src2": 999,
    "identity": 120,
}
OPCODE_FIELDS = {
    "const": ("type", "immediate"),
    "float": ("type", "immediate"),
    "straddr": ("type", "immediate"),
    "address": ("type", "immediate", "identity"),
    "idxaddr": (
        "type", "memory_size", "immediate", "src1", "src2", "identity"
    ),
    "load": ("type", "memory_size", "immediate", "identity"),
    "loadind": ("type", "memory_size", "src1"),
    "store": ("type", "memory_size", "immediate", "src1", "identity"),
    "storeind": ("type", "memory_size", "src1", "src2"),
    "unary": ("type", "immediate", "src1"),
    "binary": ("type", "immediate", "src1", "src2"),
    "arg": ("type", "immediate", "src1"),
    "call": ("type", "immediate", "src1", "identity"),
    "brfalse": ("src1",),
    "jump": ("immediate",),
    "phi": ("type", "src1", "src2", "identity"),
    "return": ("type", "src1"),
    "param": ("type", "identity"),
}
VARIANTS = {
    "default": (),
    "canonical-io": ("-fstack-check", "-ffloatio", "-flongio"),
}
EXACT_ACCEPT = (
    "function=main template=endgame-scope-runner accept=emitted"
)


def run(command, root, env=None, timeout=60):
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
    if completed.returncode:
        raise RuntimeError(
            f"{' '.join(map(str, command))} failed\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return completed.stdout + completed.stderr


def compiler_command(compiler, flags, source):
    return [
        str(compiler),
        *flags,
        "-stack",
        "512",
        "-I",
        ".",
        source,
        "-o",
        os.devnull,
    ]


def baseline_instructions(root, compiler, flags):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION="main",
        DCC_MIR_MACHINE_REPORT="1",
    )
    output = run(
        compiler_command(
            compiler, flags, "tests/mir-clobber/scopw11.c"
        ),
        root,
        env,
    )
    if EXACT_ACCEPT not in output:
        raise RuntimeError(
            f"scope exact control rejected for flags {flags}\n{output}"
        )
    instructions = []
    inside = False
    for line in output.splitlines():
        if line.startswith("; MIR function=main "):
            inside = True
            continue
        if line.startswith("; MIR summary function=main"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if len(instructions) != 1344:
        raise RuntimeError(
            f"expected 1344 main instructions, got {len(instructions)}"
        )
    return instructions


def mutation_jobs(instructions):
    jobs = []
    for instruction, opcode, detail in instructions:
        for field in OPCODE_FIELDS.get(opcode, ()):
            value = FIELD_VALUES[field]
            if field == "type" and re.search(r"\btype=1\b", detail):
                value = 2
            if field == "memory_size" and re.search(r"\bmem=1\b", detail):
                value = 2
            jobs.append((instruction, opcode, field, value))
    return jobs


def mutation_survives(root, compiler, flags, job):
    instruction, opcode, field, value = job
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION="main",
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
    )
    output = run(
        compiler_command(
            compiler, flags, "tests/mir-clobber/scopw11.c"
        ),
        root,
        env,
    )
    return job if EXACT_ACCEPT in output else None


def runtime_controls(root, dccmake):
    campaign = root / "build" / "endgame-scope-wave18-campaign"
    shutil.rmtree(campaign, ignore_errors=True)
    for stack_check in (True, False):
        for peep in (True, False):
            name = (
                f"{'stack' if stack_check else 'nostack'}-"
                f"{'peep' if peep else 'nopeep'}"
            )
            build_dir = campaign / name
            build_dir.mkdir(parents=True)
            output = run(
                [
                    str(dccmake),
                    "dcc-input=tests/mir-clobber/scopw11.c",
                    "dcc-output=ESW18",
                    f"dcc-build-dir={build_dir}",
                    f"dcc-peep={str(peep).lower()}",
                    f"dcc-stack-check={str(stack_check).lower()}",
                    "dcc-stack-bytes=512",
                    "dcc-floatio=true",
                    "dcc-flongio=true",
                ],
                root,
                dict(
                    os.environ,
                    DCC_MIR_MACHINE_REPORT="1",
                    DCC_MIR_SELECT_REPORT="1",
                ),
            )
            if EXACT_ACCEPT not in output:
                raise RuntimeError(
                    f"canonical runtime control rejected ({name})\n{output}"
                )
            runtime = run(
                ["ntvcm", "-p", "-s:0", str(build_dir / "ESW18.COM")],
                root,
                timeout=30,
            )
            if "tforsco passed with great success" not in runtime:
                raise RuntimeError(
                    f"canonical runtime control failed ({name})\n{runtime}"
                )
    shutil.rmtree(campaign, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 24))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler = root / ("dcc.exe" if os.name == "nt" else "dcc")
    dccmake = root / ("dccmake.exe" if os.name == "nt" else "dccmake")

    runtime_controls(root, dccmake)
    for name, flags in VARIANTS.items():
        instructions = baseline_instructions(root, compiler, flags)
        jobs = mutation_jobs(instructions)
        survivors = []
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.jobs
        ) as executor:
            futures = [
                executor.submit(
                    mutation_survives, root, compiler, flags, job
                )
                for job in jobs
            ]
            for future in concurrent.futures.as_completed(futures):
                survivor = future.result()
                if survivor is not None:
                    survivors.append(survivor)
        if survivors:
            raise RuntimeError(
                f"{name}: {len(survivors)} mutation survivors: "
                f"{sorted(survivors)[:20]}"
            )
        print(f"{name}: {len(jobs)} mutations, zero survivors")
    print("endgame-scope Wave 18 campaign passed")


if __name__ == "__main__":
    main()
