#!/usr/bin/env python3
"""Audit the retained random-wide-fill exact schedule semantically."""

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
FUNCTION = "fill_wide_values"
SOURCE = "tests/mir-clobber/rndwide.c"
TEMPLATE = "random-wide-fill"
EXACT_SELECTION = (
    "MIR selection function=fill_wide_values "
    "selector=scheduled-machine-cfg result=mir"
)
SELECTION = re.compile(
    r"MIR selection function=fill_wide_values "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=fill_wide_values "
    r"template=random-wide-fill reject=(?P<reason>\S+)"
)
EXACT_COST = (
    "MIR cost-candidate function=fill_wide_values candidate=incumbent "
    "selector=scheduled-machine-cfg emitted=1 selectable=1 selected=1"
)
SPILLED_SELECTED_COST = re.compile(
    r"MIR cost-candidate function=fill_wide_values "
    r"candidate=spilled-phi-slot selector=spilled-scalar-cfg "
    r"emitted=1 selectable=1 selected=1"
)
BASELINE_SHA256 = (
    "464c9dc3af8d8081d2548e3049bae1cf16623bf6309f31a387c95db0f7662a7c"
)
BASELINE_SELECTED_HASH = "2dc38a8d"
EXPECTED_RUNTIME = ("random wide fill failures=0",)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=38)


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
    SourceControl("baseline", "RW71OK", expect_exact=True),
    SourceControl(
        "renamed-helper",
        "RW71RN",
        ("RNDWIDE_RENAMED_HELPER",),
        expect_exact=True,
    ),
    SourceControl(
        "long-helper",
        "RW71LH",
        ("RNDWIDE_LONG_HELPER",),
        expected_reject="shape",
    ),
    SourceControl(
        "unsigned-count",
        "RW71UC",
        ("RNDWIDE_UNSIGNED_COUNT",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-destination",
        "RW71VD",
        ("RNDWIDE_VOLATILE_DESTINATION",),
        expected_reject="body",
    ),
    SourceControl(
        "volatile-temporary",
        "RW71VT",
        ("RNDWIDE_VOLATILE_TEMP",),
        expected_reject="opcode",
    ),
)

MUTATION_CASES = (
    MutationCase("pointer-type", "1:type:18", "loop"),
    MutationCase("count-type", "2:type:34", "loop"),
    MutationCase("initial-constant-type", "3:type:4", "loop"),
    MutationCase("initial-store-source", "5:src1:1", "loop"),
    MutationCase("initial-store-width", "5:memory_size:1", "loop"),
    MutationCase("phi-type", "9:type:18", "loop"),
    MutationCase("phi-initial-source", "9:src1:1", "loop"),
    MutationCase("phi-backedge-source", "9:src2:24", "loop"),
    MutationCase("comparison-operator", "12:immediate:61", "loop"),
    MutationCase("comparison-left", "12:src1:1", "loop"),
    MutationCase("comparison-right", "12:src2:6", "loop"),
    MutationCase("branch-source", "13:src1:6", "loop"),
    MutationCase("helper-return-type", "14:type:4", "body"),
    MutationCase("helper-identity", "14:identity:120", "result"),
    MutationCase("mask-value", "15:immediate:254", "body"),
    MutationCase("mask-type", "15:type:4", "body"),
    MutationCase("mask-operator", "16:immediate:37", "body"),
    MutationCase("mask-source", "16:src1:6", "body"),
    MutationCase("bias-value", "17:immediate:127", "body"),
    MutationCase("bias-type", "17:type:4", "body"),
    MutationCase("bias-operator", "18:immediate:43", "body"),
    MutationCase("bias-source", "18:src1:10", "body"),
    MutationCase("temporary-identity", "20:identity:120", "body"),
    MutationCase("temporary-source", "20:src1:12", "body"),
    MutationCase("index-pointer", "23:src1:1", "body"),
    MutationCase("index-value", "23:src2:1", "body"),
    MutationCase("index-stride", "23:immediate:2", "body"),
    MutationCase("index-type", "23:type:18", "body"),
    MutationCase("wide-cast-source", "25:src1:12", "body"),
    MutationCase("wide-cast-type", "25:type:2", "body"),
    MutationCase("scale-value", "26:immediate:128", "body"),
    MutationCase("multiply-operator", "27:immediate:43", "body"),
    MutationCase("multiply-type", "27:type:2", "body"),
    MutationCase("wide-store-address", "28:src1:0", "body"),
    MutationCase("wide-store-value", "28:src2:20", "body"),
    MutationCase("wide-store-width", "28:memory_size:2", "body"),
    MutationCase("increment-step", "33:src2:2", "result"),
    MutationCase("increment-store-identity", "34:identity:120", "result"),
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


def compiler_command(compiler, output):
    return [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        SOURCE, "-o", str(output),
    ]


def report_environment():
    environment = os.environ.copy()
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


def baseline_compile(compiler, output_dir):
    environment = report_environment()
    environment.pop("DCC_MIR_MACHINE_MUTATE", None)
    environment.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), environment)
    if EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select random-wide-fill\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "random-wide-fill baseline selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def forced_cost_control(compiler, output_dir):
    environment = report_environment()
    environment.update(
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_SELECT_CANDIDATE="spilled-phi-slot",
        DCC_MIR_COST_REPORT="1",
    )
    output = output_dir / "forced-spilled.MAC"
    report = run(compiler_command(compiler, output), environment)
    if EXACT_COST not in report:
        raise RuntimeError(
            "cost report did not retain the exact scheduled incumbent\n"
            + report
        )
    if SPILLED_SELECTED_COST.search(report) is None:
        raise RuntimeError(
            "forced spilled cost candidate was not selected\n" + report
        )
    if selector_from(report) != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced spilled control selected the wrong emitter\n" + report
        )


def run_runtime_control(
    compiler, dccmake, output_dir, control, stack_check, peep
):
    build_dir = output_dir / "runtime" / (
        f"{control.name}-{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    shutil.rmtree(build_dir, ignore_errors=True)
    build_dir.mkdir(parents=True)
    environment = report_environment()
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
    build_output = run(command, environment, timeout=300)
    selector = selector_from(build_output)
    reject_reason = reject_reason_from(build_output)
    if control.expect_exact:
        if selector != "scheduled-machine-cfg":
            raise RuntimeError(
                f"{control.name} did not retain exact selection\n"
                f"{build_output}"
            )
    else:
        if selector != "spilled-scalar-cfg":
            raise RuntimeError(
                f"{control.name} did not use spilled fallback\n"
                f"{build_output}"
            )
        if reject_reason != control.expected_reject:
            raise RuntimeError(
                f"{control.name} reject {reject_reason!r}, "
                f"expected {control.expected_reject!r}\n{build_output}"
            )
    runtime = run(
        ["ntvcm", "-p", "-s:0",
         str(build_dir / f"{control.output_name}.COM")],
        timeout=30,
    )
    for text in EXPECTED_RUNTIME:
        if text not in runtime:
            raise RuntimeError(
                f"{control.name} runtime missing {text!r}\n{runtime}"
            )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        "exact" if control.expect_exact else "generic",
        selector or "",
        reject_reason or "",
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
                        control, stack_check, peep
                    )
                )
    return rows


def run_mutation(compiler, work_dir, case):
    output = work_dir / f"{case.name}.MAC"
    environment = report_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(compiler_command(compiler, output), environment)
    selector = selector_from(report)
    reject_reason = reject_reason_from(report)
    output.unlink(missing_ok=True)
    if selector != "spilled-scalar-cfg":
        raise RuntimeError(
            f"{case.name} selected {selector!r}, "
            f"expected 'spilled-scalar-cfg'\n{report}"
        )
    if reject_reason != case.expected_reject:
        raise RuntimeError(
            f"{case.name} reject {reject_reason!r}, "
            f"expected {case.expected_reject!r}\n{report}"
        )
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        selector,
        reject_reason,
        "",
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
            ("name", "kind", "spec", "outcome", "selector",
             "reject_reason", "mode")
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/random-wide-fill-wave71-audit",
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
    forced_cost_control(compiler, output_dir)
    source_rows = run_source_controls(
        compiler, dccmake, output_dir
    )
    mutation_rows = run_mutation_cases(compiler, output_dir, args.jobs)
    write_tsv(output_dir / "source-controls.tsv", source_rows)
    write_tsv(output_dir / "mutation-census.tsv", mutation_rows)

    outcomes = Counter(row[3] for row in mutation_rows)
    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"random wide fill Wave 71 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print("forced candidate cost control=spilled-phi-slot")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
