#!/usr/bin/env python3
"""Exercise the retained directory-enumeration exact schedule."""

import argparse
import concurrent.futures
import os
import re
import shutil
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
    "memberaddr": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
    "indexaddr": (
        "type", "memory_size", "immediate", "src1", "src2", "identity"
    ),
    "load": ("type", "memory_size", "immediate", "identity"),
    "loadind": ("type", "memory_size", "src1"),
    "store": (
        "type", "memory_size", "immediate", "src1", "identity"
    ),
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
LAYOUTS = {
    "wave3": {
        "source": "tests/mir-clobber/direnum3.c",
        "output": "D3W19",
        "fixtures": (
            "Q7ALPHA1.DAT",
            "Q7BETA22.D",
            "Q7C.D",
            "Q7NOPE.TXT",
            "R7IGNORE.DAT",
        ),
        "expected": (
            "found=Q7BETA22.D",
            "entry=0 name=Q7ALPHA1.DAT size=128",
            "entry=1 name=Q7BETA22.D size=256",
            "entry=2 name=Q7C.D size=384",
            "oracle ok=1 init=4 first=1 next=3 sizebdos=3 "
            "dup=3 sort=1 search=1 print=4 size=3 free=3",
        ),
    },
    "wave9": {
        "source": "tests/mir-clobber/direnum9.c",
        "output": "D9W19",
        "fixtures": (
            "W9ALPHA1.DAT",
            "W9BETA22.D",
            "W9C.D",
            "W9NOPE.TXT",
            "X9IGNORE.DAT",
        ),
        "expected": (
            "found=W9BETA22.D",
            "entry=0 name=W9ALPHA1.DAT size=128",
            "entry=1 name=W9BETA22.D size=256",
            "entry=2 name=W9C.D size=384",
            "oracle ok=1 init=4 first=1 next=3 sizebdos=3 "
            "dup=3 sort=1 search=1 print=4 size=3 free=3",
        ),
    },
}
EXACT_ACCEPT = (
    "function=enumerate "
    "template=directory-enumeration-runner accept=emitted"
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


def baseline_instructions(root, compiler, flags, source):
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION="enumerate",
        DCC_MIR_MACHINE_REPORT="1",
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    if EXACT_ACCEPT not in output:
        raise RuntimeError(
            f"directory exact control rejected for flags {flags}\n"
            f"{output}"
        )
    instructions = []
    inside = False
    for line in output.splitlines():
        if line.startswith("; MIR function=enumerate "):
            inside = True
            instructions = []
            continue
        if inside and line.startswith("; MIR summary function=enumerate"):
            break
        if not inside:
            continue
        match = re.match(r";\s+(\d+)\s+(\w+)\s+(.*)", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2), match.group(3))
            )
    if len(instructions) != 318:
        raise RuntimeError(
            f"expected 318 enumerate instructions, got "
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


def mutation_survives(root, compiler, flags, source, job):
    instruction, opcode, field, value = job
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION="enumerate",
        DCC_MIR_MACHINE_MUTATE=(
            f"{instruction}:{field}:{value}"
        ),
    )
    output = run(compiler_command(compiler, flags, source), root, env)
    return job if EXACT_ACCEPT in output else None


def runtime_controls(root, dccmake, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "directory-wave3,directory-wave9",
            "-Jobs",
            str(jobs),
        ],
        root,
        timeout=900,
    )
    campaign = root / "build" / "directory-wave19-campaign"
    shutil.rmtree(campaign, ignore_errors=True)
    for layout_name, layout in LAYOUTS.items():
        for stack_check in (True, False):
            for peep in (True, False):
                name = (
                    f"{layout_name}-"
                    f"{'stack' if stack_check else 'nostack'}-"
                    f"{'peep' if peep else 'nopeep'}"
                )
                build_dir = campaign / name
                build_dir.mkdir(parents=True)
                output = run(
                    [
                        str(dccmake),
                        f"dcc-input={layout['source']}",
                        f"dcc-output={layout['output']}",
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
                        f"canonical I/O exact control rejected "
                        f"({name})\n{output}"
                    )
                for fixture in layout["fixtures"]:
                    shutil.copy2(
                        root / "tests" / "mir-clobber" / fixture,
                        build_dir / fixture,
                    )
                runtime = run(
                    [
                        "ntvcm", "-p", "-s:0",
                        f"{layout['output']}.COM",
                    ],
                    build_dir,
                    timeout=30,
                )
                for line in layout["expected"]:
                    if line not in runtime:
                        raise RuntimeError(
                            f"canonical I/O runtime failed "
                            f"({name})\n{runtime}"
                        )
    shutil.rmtree(campaign, ignore_errors=True)


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
    dccmake = root / ("dccmake.exe" if os.name == "nt" else "dccmake")

    if not args.skip_runtime:
        runtime_controls(root, dccmake, args.jobs)
    for layout_name, layout in LAYOUTS.items():
        for variant_name, flags in VARIANTS.items():
            instructions = baseline_instructions(
                root, compiler, flags, layout["source"]
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
                        layout["source"],
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
    print("directory Wave 19 campaign passed")


if __name__ == "__main__":
    main()
