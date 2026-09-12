#!/usr/bin/env python3
"""Exhaustively audit the retained wide-string exact schedule."""

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
    "address": ("type", "immediate", "identity"),
    "arg": ("type", "immediate", "src1", "identity"),
    "binary": ("type", "immediate", "src1", "src2", "identity"),
    "brfalse": ("src1", "identity"),
    "call": ("type", "src1", "src2", "identity"),
    "const": ("type", "immediate", "identity"),
    "indexaddr": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "jump": ("immediate", "identity"),
    "label": ("identity",),
    "load": ("type", "memory_size", "immediate", "identity"),
    "loadind": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "nop": ("identity",),
    "phi": ("type", "src1", "src2", "identity"),
    "store": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "storeind": (
        "type", "memory_size", "immediate",
        "src1", "src2", "identity"
    ),
    "straddr": ("type", "immediate", "identity"),
    "unary": ("type", "immediate", "src1", "identity"),
}
CONTROLS = {
    "production": ("tests/tstr.c", "test_wide", ()),
    "fixture": (
        "tests/mir-clobber/wstr23.c", "probe_wide", ()
    ),
    "renamed-fixture": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-DW23_RENAMED_LOCALS",)
    ),
    "alternate-length": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-DW23_ALT_LENGTH",)
    ),
    "alternate-compare": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-DW23_ALT_COMPARE",)
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
BENIGN_IDENTITY_OPCODES = {
    "binary",
    "brfalse",
    "const",
    "indexaddr",
    "jump",
    "label",
    "nop",
    "phi",
    "storeind",
    "unary",
}
MUTATION_CASES = {
    "production-default": (
        "tests/tstr.c", "test_wide", ()
    ),
    "production-canonical-io": (
        "tests/tstr.c", "test_wide",
        ("-ffloatio", "-flongio")
    ),
    "fixture-default": (
        "tests/mir-clobber/wstr23.c", "probe_wide", ()
    ),
    "fixture-canonical-io": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-ffloatio", "-flongio")
    ),
    "alternate-compare": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-DW23_ALT_COMPARE",)
    ),
    "alternate-compare-canonical-io": (
        "tests/mir-clobber/wstr23.c", "probe_wide",
        ("-DW23_ALT_COMPARE", "-ffloatio", "-flongio")
    ),
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
        "template=wide-string-call-runner accept=emitted"
    )
    rejection = (
        f"function={function} "
        "template=wide-string-call-runner reject="
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
            f"wide-string exact control rejected for "
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
    if len(instructions) not in (711, 712):
        raise RuntimeError(
            f"expected 711 or 712 {function} instructions, got "
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
            "wide-string-wave23,wide-string-wave23-debug",
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
                "tstr",
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
    for control_name, (source, function, defines) in controls.items():
        for variant_name, flags in variants.items():
            baseline_instructions(
                root, compiler, (*defines, *flags), source, function
            )
            print(f"{control_name}-{variant_name}: exact control passed")

    if args.variant:
        source, function, defines = next(iter(controls.values()))
        mutation_cases = {
            args.variant: (
                source, function,
                (*defines, *VARIANTS[args.variant])
            )
        }
    else:
        mutation_cases = MUTATION_CASES
    for variant_name, (
        source, function, combined_flags
    ) in mutation_cases.items():
        instructions = baseline_instructions(
            root, compiler, combined_flags, source, function
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
                    combined_flags,
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
    print("wide-string Wave 23 campaign passed")


if __name__ == "__main__":
    main()
