#!/usr/bin/env python3
"""Exhaustively audit the retained fixed wrapper initializer schedule."""

import argparse
import concurrent.futures
import collections
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
    "param": ("type", "identity"),
    "const": ("type", "immediate", "identity"),
    "load": ("type", "memory_size", "immediate", "identity"),
    "store": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "memberaddr": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "indexaddr": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "unary": ("type", "immediate", "src1", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "phi": ("type", "src1", "src2", "identity"),
    "brfalse": ("src1", "identity"),
    "jump": ("immediate", "identity"),
    "storeind": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "label": ("identity",),
    "nop": ("identity",),
}
BENIGN_IDENTITY_OPCODES = {
    "binary",
    "brfalse",
    "const",
    "indexaddr",
    "jump",
    "label",
    "memberaddr",
    "nop",
    "param",
    "phi",
    "storeind",
    "unary",
}
CONTROLS = {
    "production-rhs": ("tests/tptrrhs.c", "init_wrapper"),
    "production-conditions": ("tests/tptrcnd.c", "init_wrapper"),
    "fixture": ("tests/mir-clobber/wrinit22.c", "init_wrapper"),
    "renamed-fixture": (
        "tests/mir-clobber/wrinit22.c", "init_wrapper",
        ("-DWRAP22_RENAMED_LOCALS",)
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
        "template=fixed-wrapper-init accept=emitted"
    )
    rejection = (
        f"function={function} "
        "template=fixed-wrapper-init reject="
    )
    selection = (
        f"MIR selection function={function} "
        "selector=scheduled-machine-cfg result=mir"
    )
    return marker in output or (
        rejection not in output and selection in output
    )


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
            f"fixed wrapper exact control rejected for "
            f"{source} {flags}\n{output}"
        )
    instructions = []
    inside = False
    for line in output.splitlines():
        if line.startswith(f"; MIR function={function} "):
            inside = True
            instructions = []
            continue
        if inside and line.startswith(
            f"; MIR summary function={function}"
        ):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if len(instructions) != 527:
        raise RuntimeError(
            f"expected 527 {function} instructions, got "
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


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "wrapper-init-wave22,wrapper-init-wave22-debug",
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
                "tptrrhs,tptrcnd",
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
        source, function = control[:2]
        control_flags = control[2] if len(control) == 3 else ()
        for variant_name, flags in variants.items():
            baseline_instructions(
                root, compiler,
                (*control_flags, *flags), source, function
            )
            print(f"{control_name}-{variant_name}: exact control passed")

    source, function = CONTROLS["fixture"][:2]
    for variant_name, flags in variants.items():
        instructions = baseline_instructions(
            root, compiler, flags, source, function
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
        print(
            f"{variant_name}: {len(jobs)} mutations, zero meaningful "
            f"survivors, {benign} benign inactive-identity survivors"
        )
        if benign:
            classifications = collections.Counter(
                (job[1], job[2])
                for job in survivors if job not in meaningful
            )
            print(
                f"{variant_name}: benign classifications "
                f"{sorted(classifications.items())}"
            )
    print("fixed wrapper initializer Wave 22 campaign passed")


if __name__ == "__main__":
    main()
