#!/usr/bin/env python3
"""Exhaustively audit the retained promotion-check runner."""

import argparse
import collections
import concurrent.futures
import csv
import os
import re
import shutil
import subprocess
from pathlib import Path


FIELD_VALUES = {
    "type": 127,
    "memory_size": 7,
    "src1": 9999,
    "src2": 9998,
    "immediate": 999998,
    "identity": 120,
}
CONTROLS = {
    "production": ("tests/tpromo2.c", "main", ()),
    "usual-conversions": ("tests/tc89uac.c", "main", ()),
    "wide-unary": ("tests/tunary32.c", "main", ()),
    "fixture": (
        "tests/mir-clobber/promo25.c", "promotion_runner", ()
    ),
    "unsigned-failure": (
        "tests/mir-clobber/promo25.c", "promotion_runner",
        ("-DP25_UNSIGNED_FAILURE",)
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
MUTATION_CASES = {
    "production": ("tests/tpromo2.c", "main", ()),
    "usual-conversions": ("tests/tc89uac.c", "main", ()),
    "wide-unary": ("tests/tunary32.c", "main", ()),
    "fixture": (
        "tests/mir-clobber/promo25.c", "promotion_runner", ()
    ),
    "unsigned-failure": (
        "tests/mir-clobber/promo25.c", "promotion_runner",
        ("-DP25_UNSIGNED_FAILURE",)
    ),
}
ABI_MUTATIONS = ("checker", "print")


def run(command, root, env=None, timeout=120):
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


def compiler_command(compiler, flags, source, output):
    return [
        str(compiler),
        *flags,
        "-stack",
        "512",
        "-I",
        ".",
        source,
        "-o",
        str(output),
    ]


def exact_accept(output, function):
    return (
        f"function={function} "
        "template=promotion-check-runner accept=emitted"
    ) in output


def generic_accept(output, function):
    return re.search(
        rf"MIR selection function={re.escape(function)} "
        r"selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|"
        r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir",
        output,
    ) is not None


def baseline(
    root, compiler, work, name, flags, source, function
):
    output = work / f"{name}-baseline.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_REPORT="1",
        DCC_MIR_FUNCTION=function,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(
        compiler_command(compiler, flags, source, output),
        root,
        env,
    )
    if not exact_accept(report, function):
        raise RuntimeError(
            f"{name}: promotion exact control rejected\n{report}"
        )
    instructions = []
    inside = False
    for line in report.splitlines():
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
        match = re.match(r";\s+(\d+)\s+(\w+)\s+", line)
        if match:
            instructions.append(
                (int(match.group(1)), match.group(2))
            )
    if instructions != [
        (index, opcode)
        for index, (_number, opcode) in enumerate(instructions)
    ] or len(instructions) not in (114, 268, 432):
        raise RuntimeError(
            f"{name}: unexpected instruction profile, "
            f"got {len(instructions)}"
        )
    return instructions, output.read_bytes()


def mutation_jobs(instructions):
    return [
        (instruction, opcode, field, value)
        for instruction, opcode in instructions
        for field, value in FIELD_VALUES.items()
    ]


def mutate(
    root,
    compiler,
    work,
    name,
    flags,
    source,
    function,
    baseline_bytes,
    job,
):
    instruction, opcode, field, value = job
    output = work / f"{name}-{instruction}-{field}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=function,
        DCC_MIR_MACHINE_MUTATE=(
            f"{instruction}:{field}:{value}"
        ),
    )
    report = run(
        compiler_command(compiler, flags, source, output),
        root,
        env,
    )
    if not exact_accept(report, function):
        if not generic_accept(report, function):
            raise RuntimeError(
                f"{name} {instruction}:{field}:{value} "
                "selected neither exact nor generic MIR"
            )
        output.unlink()
        return (*job, "rejected", "")
    accepted_bytes = output.read_bytes()
    output.unlink()
    if accepted_bytes != baseline_bytes:
        return (*job, "accepted", "meaningful-output-change")
    if field == "identity":
        return (*job, "accepted", "source-spelling-only")
    return (*job, "accepted", "unclassified")


def abi_mutations(root, compiler, work):
    count = 0
    for case_name, (
        source,
        function,
        flags,
    ) in MUTATION_CASES.items():
        for role in ABI_MUTATIONS:
            output = work / f"abi-{case_name}-{role}.MAC"
            env = os.environ.copy()
            env.update(
                DCC_MIR_MACHINE_REPORT="1",
                DCC_MIR_SELECT_REPORT="1",
                DCC_MIR_PROMOTION_MUTATE_FASTCALL=role,
            )
            report = run(
                compiler_command(
                    compiler, flags, source, output
                ),
                root,
                env,
            )
            if exact_accept(report, function) or not generic_accept(
                report, function
            ):
                raise RuntimeError(
                    f"{case_name}: {role} fastcall ABI mutation "
                    "did not reject into generic MIR"
                )
            output.unlink()
            count += 1
    print(
        f"{count} fastcall ABI mutations, "
        "zero accepted survivors"
    )


def runtime_controls(root, jobs):
    run(
        [
            "pwsh",
            str(root / "scripts" / "run-mir-clobber-tests.ps1"),
            "-Cases",
            "promotion-wave25,promotion-wave25-debug",
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
                "tpromo2,tc89uac,tunary32",
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
    parser.add_argument(
        "--output-dir",
        default="build/promotion-wave25-audit",
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
            baseline(
                root,
                compiler,
                work,
                f"control-{control_name}-{variant_name}",
                (*defines, *flags),
                source,
                function,
            )
            print(
                f"{control_name}-{variant_name}: "
                "exact control passed"
            )
    abi_mutations(root, compiler, work)

    all_results = []
    try:
        for case_name, (
            source,
            function,
            flags,
        ) in MUTATION_CASES.items():
            instructions, baseline_bytes = baseline(
                root,
                compiler,
                work,
                case_name,
                flags,
                source,
                function,
            )
            jobs = mutation_jobs(instructions)
            results = []
            with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.jobs
            ) as executor:
                futures = [
                    executor.submit(
                        mutate,
                        root,
                        compiler,
                        work,
                        case_name,
                        flags,
                        source,
                        function,
                        baseline_bytes,
                        job,
                    )
                    for job in jobs
                ]
                for future in concurrent.futures.as_completed(
                    futures
                ):
                    results.append(future.result())
            meaningful = [
                result for result in results
                if result[4] == "accepted" and
                result[5] != "source-spelling-only"
            ]
            if meaningful:
                raise RuntimeError(
                    f"{case_name}: {len(meaningful)} meaningful "
                    f"survivors: {sorted(meaningful)[:40]}"
                )
            benign = collections.Counter(
                result[5] for result in results
                if result[4] == "accepted"
            )
            print(
                f"{case_name}: {len(jobs)} mutations, "
                "zero meaningful survivors, "
                f"{sum(benign.values())} benign survivors "
                f"{dict(sorted(benign.items()))}"
            )
            all_results.extend(
                (case_name, *result) for result in results
            )
    finally:
        shutil.rmtree(work, ignore_errors=True)

    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / "mutation-census.tsv").open(
        "w", newline="", encoding="utf-8"
    ) as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            (
                "case",
                "instruction",
                "opcode",
                "field",
                "value",
                "outcome",
                "classification",
            )
        )
        writer.writerows(
            sorted(
                all_results,
                key=lambda row: (
                    row[0], row[1], row[3], row[4]
                ),
            )
        )
    print("promotion Wave 25 campaign passed")


if __name__ == "__main__":
    main()
