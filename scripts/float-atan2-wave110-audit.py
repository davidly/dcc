#!/usr/bin/env python3
"""Audit the retained float-atan2 exact schedule semantically."""

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
FUNCTION = "fixture_atan2"
SOURCE = "tests/mir-clobber/fatan2.c"
TEMPLATE = "float-atan2-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=fixture_atan2 "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=fixture_atan2 "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
GENERIC_SELECTION = (
    "MIR selection function=fixture_atan2 "
    "selector=spilled-scalar-cfg result=mir"
)
FORCED_GENERIC_COST = (
    "MIR cost-selected function=fixture_atan2 "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=fixture_atan2 "
    r"template=float-atan2-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=fixture_atan2 .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "98d3715cb1c1fcca5d9801b075cf97fda73eb079bbb35a0e8bbf37d24ab22213"
)
BASELINE_SELECTED_HASH = "77d2fef8"
EXPECTED_RUNTIME = ("float atan2 failures=0",)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=34)


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
    SourceControl("baseline", "FA110OK", expect_exact=True),
    SourceControl(
        "variadic-helper",
        "FA110VA",
        ("FATAN2_VARIADIC_ATAN",),
        expected_reject="call",
    ),
    SourceControl(
        "volatile-x",
        "FA110VX",
        ("FATAN2_VOLATILE_X",),
        expected_reject="opcodes",
    ),
    SourceControl(
        "volatile-ratio",
        "FA110VR",
        ("FATAN2_VOLATILE_RATIO",),
        expected_reject="opcodes",
    ),
    SourceControl(
        "different-helper",
        "FA110DA",
        ("FATAN2_DIFFERENT_ATAN",),
        expected_reject="call-identity",
    ),
)
MUTATION_CASES = (
    MutationCase("parameter-type", "1:type:2", "parameters"),
    MutationCase("zero-constant-type", "4:type:2", "types"),
    MutationCase("zero-comparison-type", "5:type:5", "types"),
    MutationCase("zero-branch-source", "6:src1:3", "flow"),
    MutationCase("positive-return-source", "12:src1:13", "flow"),
    MutationCase("negative-comparison-type", "16:type:5", "types"),
    MutationCase("negative-unary-operator", "19:immediate:43", "types"),
    MutationCase("negative-return-source", "20:src1:12", "flow"),
    MutationCase("zero-return-source", "23:src1:13", "flow"),
    MutationCase("ratio-y-identity", "26:identity:120", "parameters"),
    MutationCase("ratio-x-identity", "27:identity:121", "parameters"),
    MutationCase("ratio-result-type", "28:type:2", "types"),
    MutationCase("ratio-left-source", "28:src1:16", "flow"),
    MutationCase("ratio-store-identity", "29:identity:120", "ratio-local"),
    MutationCase("ratio-store-width", "29:memory_size:2", "ratio-store"),
    MutationCase("positive-x-identity", "30:identity:121", "parameters"),
    MutationCase("positive-comparison-operator", "32:immediate:60", "constants"),
    MutationCase("positive-comparison-type", "32:type:5", "types"),
    MutationCase("positive-branch-source", "33:src1:19", "flow"),
    MutationCase("ratio-use-identity", "35:identity:120", "ratio-uses"),
    MutationCase("first-argument-type", "36:type:2", "call"),
    MutationCase("first-call-indirect", "37:src1:17", "call"),
    MutationCase("first-call-result-type", "37:type:2", "call"),
    MutationCase("first-return-source", "38:src1:17", "results"),
    MutationCase("quadrant-y-identity", "42:identity:120", "parameters"),
    MutationCase("quadrant-comparison-operator", "44:immediate:62", "constants"),
    MutationCase("quadrant-comparison-type", "44:type:5", "types"),
    MutationCase("quadrant-branch-source", "45:src1:24", "flow"),
    MutationCase("second-call-indirect", "49:src1:17", "call"),
    MutationCase("pi-constant-type", "50:type:2", "types"),
    MutationCase("positive-result-type", "51:type:2", "types"),
    MutationCase("positive-result-source", "52:src1:27", "results"),
    MutationCase("third-call-identity", "58:identity:120", "call-symbol"),
    MutationCase("negative-result-source", "61:src1:31", "results"),
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


def reject_reason(report):
    matches = list(REJECT_REASON.finditer(report))
    return matches[-1].group("reason") if matches else None


def require_exact(report, context):
    if EXACT_SELECTION not in report or EXACT_COST not in report:
        raise RuntimeError(
            f"{context} did not select float atan2 exactly\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained float atan2\n{report}"
        )
    if GENERIC_SELECTION not in report:
        raise RuntimeError(
            f"{context} did not select generic spilled MIR\n{report}"
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
            "float atan2 baseline selected hash changed\n" + report
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
        actual_reject = reject_reason(build_output)
        if actual_reject != control.expected_reject:
            raise RuntimeError(
                f"{control.name} reject {actual_reject!r}, "
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
        reject_reason(build_output) or "",
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
    actual_reject = reject_reason(ordinary_report)
    if actual_reject != case.expected_reject:
        raise RuntimeError(
            f"{case.name} reject {actual_reject!r}, "
            f"expected {case.expected_reject!r}\n{ordinary_report}"
        )

    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(forced_report, f"{case.name} forced")
    if FORCED_GENERIC_COST not in forced_report:
        raise RuntimeError(
            f"{case.name} did not force {FORCED_CANDIDATE}\n{forced_report}"
        )
    if reject_reason(forced_report) != case.expected_reject:
        raise RuntimeError(
            f"{case.name} forced rejection changed\n{forced_report}"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        FORCED_CANDIDATE,
        actual_reject,
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
        default="build/float-atan2-wave110-audit",
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
        f"float atan2 Wave 110 mutations={len(mutation_rows)} "
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
