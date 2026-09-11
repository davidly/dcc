#!/usr/bin/env python3
"""Exhaustively audit the retained cast/logical exact schedule."""

import argparse
import collections
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
    "label": ("identity",),
    "const": ("type", "immediate", "identity"),
    "nop": ("identity",),
    "store": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "phi": ("type", "src1", "src2", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "brfalse": ("src1", "identity"),
    "address": ("type", "immediate", "identity"),
    "indexaddr": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "jump": ("immediate", "identity"),
    "unary": ("type", "immediate", "src1", "identity"),
    "storeind": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "loadind": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "straddr": ("type", "immediate", "identity"),
    "arg": ("type", "immediate", "src1", "identity"),
    "call": ("type", "src1", "src2", "identity"),
    "return": ("type", "src1", "identity"),
}
BENIGN_IDENTITY_OPCODES = {
    "arg",
    "binary",
    "brfalse",
    "const",
    "indexaddr",
    "jump",
    "label",
    "loadind",
    "nop",
    "phi",
    "return",
    "storeind",
    "straddr",
    "unary",
}
CONTROLS = {
    "production": ("tests/tcastlog.c", "main", ()),
    "fixture": ("tests/mir-clobber/cast26.c", "main", ()),
    "renamed": (
        "tests/mir-clobber/cast26.c", "main",
        ("-DCAST26_RENAMED_LOCALS",)
    ),
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


def exact_accept(output, function):
    marker = (
        f"function={function} "
        "template=cast-logical-runner accept=emitted"
    )
    return marker in output


def baseline_instructions(root, compiler, flags, source, function):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION=function,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    if not exact_accept(output, function):
        raise RuntimeError(
            f"cast/logical exact control rejected for "
            f"{source} {flags}\n{output}"
        )
    sections = []
    instructions = None
    for line in output.splitlines():
        if line.startswith(f"; MIR function={function} "):
            instructions = []
            continue
        if instructions is not None and line.startswith(
            f"; MIR summary function={function}"
        ):
            if len(instructions) == 198:
                sections.append(instructions)
            instructions = None
            continue
        if instructions is None:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if not sections:
        raise RuntimeError(
            f"expected 198 {function} instructions, got no final MIR"
        )
    return sections[-1]


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
    root, compiler, flags, source, function, job
):
    instruction, _opcode, field, value = job
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=function,
        DCC_MIR_MACHINE_MUTATE=(
            f"{instruction}:{field}:{value}"
        ),
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    return job if exact_accept(output, function) else None


def fastcall_rejected(root, compiler, flags, source, function):
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_CAST_LOGICAL_MUTATE_FASTCALL="1",
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    if exact_accept(output, function):
        raise RuntimeError(
            f"{function} accepted a fastcall report mutation"
        )


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "cast-logical-wave26,cast-logical-wave26-debug",
            "-Jobs",
            str(jobs),
        ],
        root,
        timeout=900,
    )
    env = os.environ.copy()
    env.update(
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
    for stack_args in ((), ("-NoStackCheck",)):
        run(
            [
                "pwsh",
                str(root / "scripts" / "runall.ps1"),
                "-Apps",
                "tcastlog",
                "-Mode",
                "full",
                *stack_args,
                "-RunTimeout",
                "30",
                "-FailuresOnly",
            ],
            root,
            env,
            timeout=600,
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--jobs", type=int, default=min(os.cpu_count() or 1, 24)
    )
    parser.add_argument("--skip-runtime", action="store_true")
    parser.add_argument("--variant", choices=VARIANTS)
    parser.add_argument("--control", choices=CONTROLS)
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
    variants = (
        {args.variant: VARIANTS[args.variant]}
        if args.variant else VARIANTS
    )
    controls = (
        {args.control: CONTROLS[args.control]}
        if args.control else CONTROLS
    )
    for control_name, control in controls.items():
        source, function, control_flags = control
        for variant_name, flags in variants.items():
            baseline_instructions(
                root, compiler,
                (*control_flags, *flags), source, function
            )
            print(f"{control_name}-{variant_name}: exact control passed")

    source, function, control_flags = CONTROLS["fixture"]
    for variant_name, flags in variants.items():
        all_flags = (*control_flags, *flags)
        instructions = baseline_instructions(
            root, compiler, all_flags, source, function
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
                    all_flags,
                    source,
                    function,
                    job,
                )
                for job in jobs
            ]
            for future in concurrent.futures.as_completed(futures):
                survivor = future.result()
                if survivor is not None:
                    survivors.append(survivor)
        meaningful = [
            job for job in survivors
            if not (
                job[2] == "identity" and
                job[1] in BENIGN_IDENTITY_OPCODES
            )
        ]
        benign = len(survivors) - len(meaningful)
        if meaningful:
            raise RuntimeError(
                f"{variant_name}: {len(meaningful)} meaningful "
                f"mutation survivors: {sorted(meaningful)[:40]}"
            )
        fastcall_rejected(
            root, compiler, all_flags, source, function
        )
        print(
            f"{variant_name}: {len(jobs) + 1} mutations, "
            f"zero meaningful survivors, {benign} benign "
            "inactive-identity survivors"
        )
        if benign:
            classifications = collections.Counter(
                (job[1], job[2]) for job in survivors
            )
            print(
                f"{variant_name}: benign classifications "
                f"{sorted(classifications.items())}"
            )
    print("cast/logical runner Wave 26 campaign passed")


if __name__ == "__main__":
    main()
