#!/usr/bin/env python3
"""Exercise every retained Fortran fatal exact-schedule layout."""

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
    "stringaddr": ("type", "immediate"),
    "param": ("type", "identity"),
    "load": ("type", "memory_size", "immediate", "identity"),
    "memberaddr": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "loadind": ("type", "memory_size", "src1"),
    "binary": ("type", "immediate", "src1", "src2"),
    "brfalse": ("src1",),
    "jump": ("immediate",),
    "phi": ("type", "src1", "src2", "identity"),
    "arg": ("type", "immediate", "src1", "identity"),
    "call": ("type", "src1", "identity"),
}
LAYOUTS = {
    "fixture": (
        "tests/mir-clobber/ffatal12.c", "ff12_die", ()
    ),
    "legacy": (
        "tests/mir-clobber/fatfor.c", "die", ()
    ),
    "static-print": (
        "tests/mir-clobber/ffatal12.c", "ff12_die",
        ("-DFF12_STATIC_PRINT",)
    ),
    "static-exit": (
        "tests/mir-clobber/ffatal12.c", "ff12_die",
        ("-DFF12_STATIC_EXIT",)
    ),
    "static-both": (
        "tests/mir-clobber/ffatal12.c", "ff12_die",
        ("-DFF12_STATIC_PRINT", "-DFF12_STATIC_EXIT")
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
BENIGN_MUTATION_FIELDS = {
    ("arg", "identity"),
    ("memberaddr", "identity"),
    ("phi", "identity"),
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


def exact_accept(function):
    return (
        f"function={function} "
        "template=fortran-fatal-schedule accept=emitted"
    )


def baseline_instructions(root, compiler, flags, source, function):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION=function,
        DCC_MIR_MACHINE_REPORT="1",
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    if exact_accept(function) not in output:
        raise RuntimeError(
            f"Fortran fatal exact control rejected for "
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
    if len(instructions) != 81:
        raise RuntimeError(
            f"expected 81 {function} instructions, got "
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
    instruction, opcode, field, value = job
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=function,
        DCC_MIR_MACHINE_MUTATE=(
            f"{instruction}:{field}:{value}"
        ),
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    return job if exact_accept(function) in output else None


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "fortran-fatal-wave12",
            "-Jobs",
            str(jobs),
        ],
        root,
        timeout=900,
    )
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "fatfor,fatforv,fatfore,fatfors,fatforr,fatforg",
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
                "forint",
                "-Mode",
                "full",
                *stack_args,
                "-RunTimeout",
                "30",
                "-FailuresOnly",
            ],
            root,
            env,
            timeout=300,
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--jobs", type=int, default=min(os.cpu_count() or 1, 24)
    )
    parser.add_argument("--skip-runtime", action="store_true")
    parser.add_argument("--layout", choices=LAYOUTS)
    parser.add_argument("--variant", choices=VARIANTS)
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
    layouts = (
        {args.layout: LAYOUTS[args.layout]}
        if args.layout else LAYOUTS
    )
    variants = (
        {args.variant: VARIANTS[args.variant]}
        if args.variant else VARIANTS
    )
    for layout_name, (source, function, defines) in layouts.items():
        for variant_name, variant_flags in variants.items():
            flags = (*defines, *variant_flags)
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
            name = f"{layout_name}-{variant_name}"
            meaningful = [
                job for job in survivors
                if (job[1], job[2]) not in BENIGN_MUTATION_FIELDS
            ]
            benign = len(survivors) - len(meaningful)
            if meaningful:
                raise RuntimeError(
                    f"{name}: {len(meaningful)} meaningful mutation "
                    f"survivors: {sorted(meaningful)[:40]}"
                )
            print(
                f"{name}: {len(jobs)} mutations, zero meaningful "
                f"survivors, {benign} benign metadata survivors"
            )
    print("Fortran fatal Wave 21 campaign passed")


if __name__ == "__main__":
    main()
