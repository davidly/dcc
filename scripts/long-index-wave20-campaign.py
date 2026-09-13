#!/usr/bin/env python3
"""Exercise every accepted long-index exact-schedule payload."""

import argparse
import concurrent.futures
import os
import re
import subprocess
from pathlib import Path


FIELD_VALUES = {
    "type": 127,
    "immediate": 999999,
    "memory_size": 7,
    "src1": 9999,
    "src2": 9998,
    "identity": 120,
}
OPCODE_FIELDS = {
    "const": ("type", "immediate"),
    "straddr": ("type", "immediate"),
    "address": ("type", "identity"),
    "indexaddr": (
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
}
LAYOUTS = {
    "canonical": ("tests/mir-clobber/w12lidx.c", ()),
    "external-storage": (
        "tests/mir-clobber/w12lidx.c",
        ("-DLONG_INDEX_EXTERNAL_STORAGE",),
    ),
    "larger-array": (
        "tests/mir-clobber/w12lidx.c",
        ("-DLONG_INDEX_LARGER_ARRAY",),
    ),
    "production": ("tests/tbcloop.c", ()),
}
VARIANTS = {
    "no-stack": (),
    "stack": ("-fstack-check",),
    "canonical-io": ("-ffloatio", "-flongio"),
    "canonical-io-stack": (
        "-fstack-check", "-ffloatio", "-flongio"
    ),
    "line-debug": ("-gline",),
    "line-debug-stack": ("-gline", "-fstack-check"),
    "module": ("-c",),
    "module-stack": ("-c", "-fstack-check"),
}
EXACT_ACCEPT = (
    "function=main template=long-index-call-runner accept=emitted"
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


def compiler_command(compiler, flags, source, defines):
    return [
        str(compiler),
        *flags,
        *defines,
        "-stack",
        "512",
        "-I",
        ".",
        source,
        "-o",
        os.devnull,
    ]


def baseline_instructions(
    root, compiler, flags, source, defines
):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION="main",
        DCC_MIR_MACHINE_REPORT="1",
    )
    output = run(
        compiler_command(compiler, flags, source, defines), root, env
    )
    if EXACT_ACCEPT not in output:
        raise RuntimeError(
            f"long-index exact control rejected for "
            f"{flags} {defines}\n{output}"
        )
    instructions = []
    inside = False
    for line in output.splitlines():
        if line.startswith("; MIR function=main "):
            inside = True
            instructions = []
            continue
        if inside and line.startswith("; MIR summary function=main"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if len(instructions) != 148:
        raise RuntimeError(
            f"expected 148 main instructions, got "
            f"{len(instructions)}"
        )
    return instructions


def mutation_jobs(instructions):
    jobs = []
    for instruction, opcode, detail in instructions:
        for field in OPCODE_FIELDS.get(opcode, ()):
            value = FIELD_VALUES[field]
            if field == "type" and re.search(r"\btype=127\b", detail):
                value = 126
            if (
                field == "memory_size" and
                re.search(r"\bmem=7\b", detail)
            ):
                value = 6
            jobs.append((instruction, opcode, field, value))
    return jobs


def mutation_survives(
    root, compiler, flags, source, defines, job
):
    instruction, opcode, field, value = job
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION="main",
        DCC_MIR_MACHINE_MUTATE=(
            f"{instruction}:{field}:{value}"
        ),
    )
    output = run(
        compiler_command(compiler, flags, source, defines), root, env
    )
    return job if EXACT_ACCEPT in output else None


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "long-index-wave12",
            "-Jobs",
            str(jobs),
        ],
        root,
        timeout=900,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--jobs", type=int, default=min(os.cpu_count() or 1, 24)
    )
    parser.add_argument("--skip-runtime", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    compiler_name = os.environ.get("DCC")
    compiler = (
        Path(compiler_name)
        if compiler_name
        else root / ("dcc.exe" if os.name == "nt" else "dcc")
    )
    if not compiler.is_absolute():
        compiler = (root / compiler).resolve()

    if not args.skip_runtime:
        runtime_controls(root, args.jobs)
    for layout_name, (source, defines) in LAYOUTS.items():
        for variant_name, flags in VARIANTS.items():
            instructions = baseline_instructions(
                root, compiler, flags, source, defines
            )
            jobs = mutation_jobs(instructions)
            survivors = []
            with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.jobs
            ) as executor:
                futures = [
                    executor.submit(
                        mutation_survives,
                        root,
                        compiler,
                        flags,
                        source,
                        defines,
                        job,
                    )
                    for job in jobs
                ]
                for future in concurrent.futures.as_completed(futures):
                    survivor = future.result()
                    if survivor is not None:
                        survivors.append(survivor)
            name = f"{layout_name}-{variant_name}"
            if survivors:
                raise RuntimeError(
                    f"{name}: {len(survivors)} mutation survivors: "
                    f"{sorted(survivors)[:20]}"
                )
            print(f"{name}: {len(jobs)} mutations, zero survivors")
    print("long-index Wave 20 campaign passed")


if __name__ == "__main__":
    main()
