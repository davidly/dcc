#!/usr/bin/env python3
"""Audit the retained call-safe member-sum exact schedule semantically."""

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
FUNCTION = "callw17_member_sum"
SOURCE = "tests/mir-clobber/callw17.c"
TEMPLATE = "call-safe-member-sum-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=callw17_member_sum "
    "selector=scheduled-machine-cfg result=mir"
)
SELECTION = re.compile(
    r"MIR selection function=callw17_member_sum "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=callw17_member_sum "
    r"template=call-safe-member-sum-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=callw17_member_sum .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "865bd210a681c96c6ee1d767a513ab0ca24e169818f8d86e378a3de9c8be0534"
)
BASELINE_SELECTED_HASH = "99a108e6"
BASELINE_RUNTIME = (
    "CALLW17 positive=88 zero=0 negative=0 oracle=1496",
)
ALIAS_RUNTIME = (
    "CALLW17 positive=104 zero=0 negative=0 oracle=1768",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=47)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expected_runtime: tuple[str, ...] = BASELINE_RUNTIME
    expect_exact: bool = False
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_reject: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "CS90OK", expect_exact=True),
    SourceControl(
        "local-callee",
        "CS90LC",
        ("CALLW17_LOCAL_CALLEE",),
        expect_exact=True,
    ),
    SourceControl(
        "alias-callee",
        "CS90AL",
        ("CALLW17_ALIAS_CALLEE",),
        expected_runtime=ALIAS_RUNTIME,
        expect_exact=True,
    ),
    SourceControl(
        "variadic-callee",
        "CS90VA",
        ("CALLW17_VARIADIC_CALLEE",),
        expected_reject="calls",
    ),
    SourceControl(
        "second-callee",
        "CS90SC",
        ("CALLW17_SECOND_CALLEE",),
        expected_reject="calls",
    ),
    SourceControl(
        "padded-record",
        "CS90PD",
        ("CALLW17_PADDED_RECORD",),
        expected_reject="layout",
    ),
    SourceControl(
        "volatile-index",
        "CS90VI",
        ("CALLW17_VOLATILE_INDEX",),
    ),
    SourceControl(
        "volatile-total",
        "CS90VT",
        ("CALLW17_VOLATILE_TOTAL",),
    ),
)

MUTATION_CASES = (
    MutationCase("load-type", "20:type:4", "types-widths"),
    MutationCase("call-type", "22:type:4", "types-widths"),
    MutationCase("later-store-width", "25:memory_size:1", "types-widths"),
    MutationCase("binary-type", "23:type:4", "types-widths"),
    MutationCase("count-type", "2:type:34", "parameters"),
    MutationCase("pointer-identity", "1:identity:120", "parameters"),
    MutationCase("pointer-nop-identity", "9:identity:120", "parameters"),
    MutationCase("count-nop-identity", "10:identity:120", "parameters"),
    MutationCase(
        "condition-count-identity", "14:identity:120", "parameters"
    ),
    MutationCase("total-zero", "3:immediate:1", "loop-state"),
    MutationCase("index-zero", "5:immediate:1", "loop-state"),
    MutationCase("total-initial-source", "4:src1:3", "loop-state"),
    MutationCase("index-initial-source", "7:src1:2", "loop-state"),
    MutationCase("total-store-identity", "4:identity:120", "loop-state"),
    MutationCase("index-store-identity", "7:identity:120", "loop-state"),
    MutationCase("total-phi-identity", "11:identity:120", "loop-state"),
    MutationCase("index-phi-identity", "12:identity:120", "loop-state"),
    MutationCase("total-phi-entry", "11:src1:3", "loop-state"),
    MutationCase("index-phi-entry", "12:src1:2", "loop-state"),
    MutationCase("index-phi-backedge", "12:src2:45", "loop-state"),
    MutationCase("total-phi-type", "11:type:34", "loop-types"),
    MutationCase("index-phi-type", "12:type:34", "loop-types"),
    MutationCase("condition-left", "15:src1:1", "condition"),
    MutationCase("condition-right", "15:src2:8", "condition"),
    MutationCase("branch-source", "16:src1:8", "condition"),
    MutationCase("first-member-offset", "19:immediate:1", "layout"),
    MutationCase("second-member-offset", "28:immediate:3", "layout"),
    MutationCase("third-member-offset", "37:immediate:5", "layout"),
    MutationCase("member-base", "19:src1:1", "member-load"),
    MutationCase("member-load-address", "20:src1:21", "member-load"),
    MutationCase("second-argument-tag", "30:immediate:1", "calls"),
    MutationCase("second-call-identity", "31:identity:120", "calls"),
    MutationCase("first-call-sum-operator", "23:immediate:45", "call-sum"),
    MutationCase("first-call-store-source", "25:src1:16", "call-sum"),
    MutationCase("second-call-sum-operator", "32:immediate:45", "call-sum"),
    MutationCase("second-call-store-source", "34:src1:23", "call-sum"),
    MutationCase("third-call-sum-operator", "41:immediate:45", "call-sum"),
    MutationCase("third-call-store-source", "43:src1:30", "call-sum"),
    MutationCase("raw-first-sum-operator", "51:immediate:45", "tail"),
    MutationCase("raw-second-sum-operator", "55:immediate:45", "tail"),
    MutationCase("raw-total-source", "56:src1:24", "tail"),
    MutationCase("final-total-store-source", "58:src1:44", "tail"),
    MutationCase("increment-one", "62:immediate:2", "tail"),
    MutationCase("increment-left", "63:src1:7", "tail"),
    MutationCase("increment-right", "63:src2:2", "tail"),
    MutationCase("increment-operator", "63:immediate:45", "tail"),
    MutationCase("increment-store-source", "64:src1:48", "tail"),
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


def compiler_command(compiler, output, defines=()):
    command = [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
    ]
    for define in defines:
        command.append(f"-D{define}")
    command.extend([SOURCE, "-o", str(output)])
    return command


def report_environment():
    environment = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
        "DCC_MIR_SELECT_FUNCTION",
    ):
        environment.pop(name, None)
    environment.update(
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_SELECT_REPORT_FUNCTION=FUNCTION,
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
    return environment


def selector_from(report):
    matches = list(SELECTION.finditer(report))
    return matches[-1].group("selector") if matches else None


def reject_reason_from(report):
    matches = list(REJECT_REASON.finditer(report))
    return matches[-1].group("reason") if matches else None


def selected_hash(report):
    matches = list(SELECTED_HASH.finditer(report))
    return matches[-1].group("hash") if matches else None


def require_exact(report, context):
    if EXACT_SELECTION not in report:
        raise RuntimeError(
            f"{context} did not retain the exact schedule\n{report}"
        )


def require_generic(report, context, expected_reject=None):
    selector = selector_from(report)
    if selector != "spilled-scalar-cfg":
        raise RuntimeError(
            f"{context} selected {selector!r}, "
            f"expected 'spilled-scalar-cfg'\n{report}"
        )
    reject = reject_reason_from(report)
    if expected_reject is not None and reject != expected_reject:
        raise RuntimeError(
            f"{context} reject {reject!r}, "
            f"expected {expected_reject!r}\n{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        report_environment(),
    )
    require_exact(report, "baseline")
    if selected_hash(report) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "call-safe member-sum selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def forced_fallback_control(compiler, output_dir):
    environment = report_environment()
    environment.update(
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_SELECT_CANDIDATE=FORCED_CANDIDATE,
    )
    output = output_dir / "forced-spilled.MAC"
    report = run(compiler_command(compiler, output), environment)
    require_generic(report, "forced fallback")


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
    report = run(command, report_environment(), timeout=300)
    if control.expect_exact:
        require_exact(report, control.name)
        outcome = "exact"
    else:
        require_generic(
            report, control.name, control.expected_reject
        )
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=30,
    )
    for text in control.expected_runtime:
        if text not in runtime:
            raise RuntimeError(
                f"{control.name} runtime missing {text!r}\n{runtime}"
            )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        outcome,
        selector_from(report) or "",
        reject_reason_from(report) or "",
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
    environment = report_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), environment
    )
    require_generic(
        ordinary_report, case.name, case.expected_reject
    )

    forced_environment = environment.copy()
    forced_environment.update(
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_SELECT_CANDIDATE=FORCED_CANDIDATE,
    )
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(
        forced_report, f"{case.name} forced", case.expected_reject
    )
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
        "spilled-scalar-cfg",
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
                "selector", "reject_reason", "mode",
            )
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/call-safe-member-sum-wave90-audit",
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
    forced_fallback_control(compiler, output_dir)
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
        f"call-safe member sum Wave 90 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
