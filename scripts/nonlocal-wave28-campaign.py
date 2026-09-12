#!/usr/bin/env python3
"""Exhaustively audit the retained non-local exact schedules."""

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
    "param": ("type", "identity"),
    "const": ("type", "immediate", "identity"),
    "nop": ("identity",),
    "store": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "phi": ("type", "src1", "src2", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "brfalse": ("src1", "identity"),
    "load": ("type", "memory_size", "immediate", "identity"),
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
    "address",
    "arg",
    "binary",
    "brfalse",
    "const",
    "indexaddr",
    "jump",
    "label",
    "loadind",
    "nop",
    "param",
    "phi",
    "return",
    "storeind",
    "straddr",
    "unary",
}
SCHEDULES = {
    "descent": {
        "template": "nonlocal-descent",
        "production": ("tests/tsjdeep.c", "deep"),
        "fixture": ("tests/mir-clobber/nl28.c", "nl28_descent"),
        "count": 120,
        "abi_roles": ("jump", "recursive"),
    },
    "runner": {
        "template": "nonlocal-runner",
        "production": ("tests/tsjdeep.c", "main"),
        "fixture": ("tests/mir-clobber/nl28.c", "main"),
        "count": 100,
        "abi_roles": ("save", "descent", "check", "print"),
    },
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


def exact_accept(output, function, template):
    return (
        f"function={function} template={template} accept=emitted"
    ) in output


def baseline_instructions(
    root, compiler, flags, source, function, template, count
):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION=function,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    if not exact_accept(output, function, template):
        raise RuntimeError(
            f"{template} exact control rejected for "
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
            if len(instructions) == count:
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
            f"expected {count} {function} instructions, got no final MIR"
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
    root, compiler, flags, source, function, template, job
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
    return job if exact_accept(output, function, template) else None


def abi_mutation_survives(
    root, compiler, flags, source, function, template, role
):
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_NONLOCAL_MUTATE_ABI=role,
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    return role if exact_accept(output, function, template) else None


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "nonlocal-wave28,nonlocal-wave28-debug",
            "-Jobs",
            str(jobs),
        ],
        root,
        timeout=1200,
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
                "tsjdeep,tsetjmp,tsetjiy",
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
    parser.add_argument("--schedule", choices=SCHEDULES)
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
    schedules = (
        {args.schedule: SCHEDULES[args.schedule]}
        if args.schedule else SCHEDULES
    )
    for schedule_name, schedule in schedules.items():
        template = schedule["template"]
        for control_name in ("production", "fixture"):
            source, function = schedule[control_name]
            for variant_name, flags in variants.items():
                baseline_instructions(
                    root, compiler, flags, source, function,
                    template, schedule["count"]
                )
                print(
                    f"{schedule_name}-{control_name}-{variant_name}: "
                    "exact control passed"
                )

        source, function = schedule["fixture"]
        for variant_name, flags in variants.items():
            instructions = baseline_instructions(
                root, compiler, flags, source, function,
                template, schedule["count"]
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
                        function,
                        template,
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
            if meaningful:
                raise RuntimeError(
                    f"{schedule_name}-{variant_name}: "
                    f"{len(meaningful)} meaningful mutation survivors: "
                    f"{sorted(meaningful)[:40]}"
                )
            abi_survivors = [
                role for role in schedule["abi_roles"]
                if abi_mutation_survives(
                    root, compiler, flags, source,
                    function, template, role
                ) is not None
            ]
            if abi_survivors:
                raise RuntimeError(
                    f"{schedule_name}-{variant_name}: "
                    f"ABI mutation survivors {abi_survivors}"
                )
            classifications = collections.Counter(
                (job[1], job[2]) for job in survivors
            )
            print(
                f"{schedule_name}-{variant_name}: "
                f"{len(jobs) + len(schedule['abi_roles'])} mutations, "
                f"zero meaningful survivors, {len(survivors)} benign "
                "inactive-identity survivors"
            )
            if survivors:
                print(
                    f"{schedule_name}-{variant_name}: "
                    f"benign classifications "
                    f"{sorted(classifications.items())}"
                )
    print("non-local Wave 28 campaign passed")


if __name__ == "__main__":
    main()
