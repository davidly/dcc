#!/usr/bin/env python3
"""Audit the retained BCD byte-math exact schedule semantically."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import hashlib
import os
import re
import shutil
import subprocess
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTION = "op_bcd_math"
SOURCE = "tests/mir-clobber/bmw9.c"
TEMPLATE = "bcd-byte-math-schedule"
FORCED_CANDIDATE = "spilled-boolean-phi-branch"
EXACT_SELECTION = (
    "MIR selection function=op_bcd_math "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=op_bcd_math "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
GENERIC_SELECTION = (
    "MIR selection function=op_bcd_math "
    "selector=spilled-scalar-cfg result=mir"
)
GENERIC_COST = (
    "MIR cost-selected function=op_bcd_math "
    "candidate=spilled-boolean-phi-branch selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=op_bcd_math "
    r"template=bcd-byte-math-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=op_bcd_math .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "4c1cc708a5bba78f085bd74af9e9ac13293e3d22b6e00587a2666a8cc202cafb"
)
BASELINE_SELECTED_HASH = "14ace686"
EXPECTED_RUNTIME = (
    "BMW9 oracle checks=2048 failures=0 hash=2065919540",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=29)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expect_exact: bool = False
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_reject: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "BCD110OK", expect_exact=True),
    SourceControl(
        "volatile-result",
        "BCD110VR",
        ("BMW9_BCD_VOLATILE_RESULT",),
        expected_reject="shape",
    ),
)
MUTATION_CASES = (
    MutationCase("parameter-type", "1:type:2", "types"),
    MutationCase("accumulator-member-type", "4:type:17", "types"),
    MutationCase("accumulator-load-source", "5:src1:0", "value-relations"),
    MutationCase("digit-mask-operator", "8:immediate:124", "operators"),
    MutationCase("digit-mask-source", "8:src1:5", "value-relations"),
    MutationCase("low-store-source", "10:src1:15", "value-relations"),
    MutationCase("accumulator-load-width", "13:memory_size:2", "memory-access"),
    MutationCase("zero-store-value", "35:src2:8", "value-relations"),
    MutationCase("digit-compare-operator", "39:immediate:279", "operators"),
    MutationCase("digit-branch-condition", "40:src1:38", "value-relations"),
    MutationCase("phi-source", "56:src1:34", "value-relations"),
    MutationCase("phi-type", "56:type:2", "types"),
    MutationCase("decimal-multiply-operator", "109:immediate:43", "operators"),
    MutationCase("decimal-add-source", "112:src2:14", "value-relations"),
    MutationCase("math-compare-operator", "127:immediate:279", "operators"),
    MutationCase("carry-member-width", "131:memory_size:2", "memory-access"),
    MutationCase("carry-negation-operator", "133:immediate:0", "operators"),
    MutationCase("borrow-increment-source", "138:src1:113", "value-relations"),
    MutationCase("subtract-compare-source", "147:src2:68", "value-relations"),
    MutationCase("subtract-operator", "154:immediate:43", "operators"),
    MutationCase("carry-store-value", "161:src2:177", "value-relations"),
    MutationCase("addition-carry-source", "193:src2:132", "value-relations"),
    MutationCase("carry-bound", "197:immediate:98", "constants"),
    MutationCase("wrap-subtract-operator", "205:immediate:43", "operators"),
    MutationCase("result-store-identity", "208:identity:120", "named-memory"),
    MutationCase("result-load-width", "227:memory_size:2", "memory-access"),
    MutationCase("pack-divide-operator", "230:immediate:37", "operators"),
    MutationCase("pack-shift-operator", "232:immediate:43", "operators"),
    MutationCase("final-store-value", "239:src2:149", "value-relations"),
)


def run(command, env=None, timeout=180):
    environment = os.environ.copy() if env is None else env.copy()
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
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


def compiler_path():
    configured = os.environ.get("DCC")
    compiler = (
        Path(configured)
        if configured
        else ROOT / ("dcc.exe" if os.name == "nt" else "dcc")
    )
    if not compiler.is_absolute():
        compiler = (ROOT / compiler).resolve()
    if not compiler.is_file():
        raise RuntimeError(f"DCC compiler not found: {compiler}")
    return compiler


def dccmake_path():
    tool = ROOT / ("dccmake.exe" if os.name == "nt" else "dccmake")
    if not tool.is_file():
        raise RuntimeError(f"dccmake not found: {tool}")
    return tool


def diagnostic_environment():
    environment = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
        "DCC_MIR_SELECT_FUNCTION",
    ):
        environment.pop(name, None)
    environment.update(
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
    )
    return environment


def compiler_command(compiler, output, defines=()):
    command = [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
    ]
    for define in defines:
        command.append(f"-D{define}")
    command.extend([SOURCE, "-o", str(output)])
    return command


def selected_hash(report):
    match = SELECTED_HASH.search(report)
    return match.group("hash") if match is not None else None


def reject_reasons(report):
    return [match.group("reason") for match in REJECT_REASON.finditer(report)]


def require_exact(report, context):
    if EXACT_SELECTION not in report or EXACT_COST not in report:
        raise RuntimeError(
            f"{context} did not select the BCD byte-math schedule\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained the BCD byte-math schedule\n"
            f"{report}"
        )
    if GENERIC_SELECTION not in report or GENERIC_COST not in report:
        raise RuntimeError(
            f"{context} did not select {FORCED_CANDIDATE}\n{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, "baseline")
    if selected_hash(report) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "BCD byte-math baseline selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def run_runtime_control(
    compiler, dccmake, output_dir, control, stack_check, peep
):
    build_dir = output_dir / "runtime" / (
        f"{control.name}-{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    shutil.rmtree(build_dir, ignore_errors=True)
    build_dir.mkdir(parents=True)
    command = [
        str(dccmake),
        f"dcc-input={SOURCE}",
        f"dcc-output={control.output_name}",
        f"dcc-build-dir={build_dir}",
        f"dcc-tool={compiler}",
        f"dcc-peep={str(peep).lower()}",
        f"dcc-stack-check={str(stack_check).lower()}",
        "dcc-stack-bytes=512",
    ]
    if control.defines:
        command.append(f"dcc-define={','.join(control.defines)}")
    build_output = run(
        command, diagnostic_environment(), timeout=300
    )
    if control.expect_exact:
        require_exact(build_output, control.name)
        outcome = "exact"
    else:
        require_generic(build_output, control.name)
        reasons = reject_reasons(build_output)
        if control.expected_reject not in reasons:
            raise RuntimeError(
                f"{control.name} rejects {reasons!r}, "
                f"expected {control.expected_reject!r}\n{build_output}"
            )
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=30,
    )
    missing = [text for text in EXPECTED_RUNTIME if text not in runtime]
    if missing:
        raise RuntimeError(
            f"{control.name} runtime missing {missing!r}\n{runtime}"
        )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        outcome,
        "scheduled-machine-cfg"
        if control.expect_exact else "spilled-scalar-cfg",
        selected_hash(build_output) or "",
        f"stack={int(stack_check)} peep={int(peep)}",
    )


def run_source_controls(compiler, dccmake, output_dir):
    rows = []
    for control in SOURCE_CONTROLS:
        for stack_check in (True, False):
            for peep in (True, False):
                rows.append(
                    run_runtime_control(
                        compiler, dccmake, output_dir,
                        control, stack_check, peep,
                    )
                )
    return rows


def run_mutation(compiler, work_dir, case):
    ordinary_path = work_dir / f"{case.name}-ordinary.MAC"
    forced_path = work_dir / f"{case.name}-forced.MAC"
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), environment
    )
    require_generic(ordinary_report, case.name)
    reasons = reject_reasons(ordinary_report)
    if case.expected_reject not in reasons:
        raise RuntimeError(
            f"{case.name} rejects {reasons!r}, "
            f"expected {case.expected_reject!r}\n{ordinary_report}"
        )

    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(forced_report, f"{case.name} forced")
    if ordinary_path.read_bytes() != forced_path.read_bytes():
        raise RuntimeError(
            f"{case.name} forced fallback differs from ordinary fallback"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        FORCED_CANDIDATE,
        case.expected_reject,
        "forced=verified",
    )


def run_mutation_cases(compiler, output_dir, jobs):
    work_dir = output_dir / "work"
    shutil.rmtree(work_dir, ignore_errors=True)
    work_dir.mkdir(parents=True)
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=jobs
        ) as executor:
            futures = [
                executor.submit(run_mutation, compiler, work_dir, case)
                for case in MUTATION_CASES
            ]
            results = [
                future.result()
                for future in concurrent.futures.as_completed(futures)
            ]
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)
    return sorted(results)


def write_tsv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            (
                "name", "kind", "spec", "outcome",
                "candidate", "detail", "mode",
            )
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/bcd-byte-math-wave200-audit",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    compiler = compiler_path()
    dccmake = dccmake_path()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True)

    baseline_digest = baseline_compile(compiler, output_dir)
    source_rows = run_source_controls(
        compiler, dccmake, output_dir
    )
    mutation_rows = run_mutation_cases(
        compiler, output_dir, args.jobs
    )
    write_tsv(output_dir / "source-controls.tsv", source_rows)
    write_tsv(output_dir / "mutation-census.tsv", mutation_rows)

    outcomes = Counter(row[3] for row in mutation_rows)
    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"BCD byte math Wave 200 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
