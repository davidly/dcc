#!/usr/bin/env python3
"""Audit the packed-byte-report exact schedule semantically."""

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
FUNCTION = "main"
SOURCE = "tests/mir-clobber/pkbrpt.c"
TEMPLATE = "packed-byte-report-schedule"
EXACT_SELECTION = (
    "MIR selection function=main "
    "selector=scheduled-machine-cfg result=mir"
)
SELECTION = re.compile(
    r"MIR selection function=main "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=main "
    r"template=packed-byte-report-schedule reject=(?P<reason>\S+)"
)
EXACT_COST = (
    "MIR cost-candidate function=main candidate=incumbent "
    "selector=scheduled-machine-cfg emitted=1 selectable=1 selected=1"
)
HYBRID_SELECTED_COST = re.compile(
    r"MIR cost-candidate function=main candidate=hybrid "
    r"selector=hybrid-homed-scalar-cfg emitted=1 selectable=1 "
    r"selected=1"
)
BASELINE_SHA256 = (
    "6c82b5625ae46dd0fef759670274f489aa0656b8b9a0ffc77e17b1f921c1e47b"
)
BASELINE_SELECTED_HASH = "c1803b97"
PACKED_BYTES = (0x12, 0xA5, 0x5A, 0xE7)
PACKED_VALUE = sum(
    value << (8 * (len(PACKED_BYTES) - index - 1))
    for index, value in enumerate(PACKED_BYTES)
)
EXPECTED_RUNTIME = (f"packed byte report value={PACKED_VALUE}",)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=26)


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
    SourceControl("baseline", "PKB82OK", expect_exact=True),
    SourceControl(
        "renamed-pack",
        "PKB82RN",
        ("PKBRPT_RENAMED_PACK",),
        expect_exact=True,
    ),
    SourceControl(
        "volatile-buffer",
        "PKB82VB",
        ("PKBRPT_VOLATILE_BUFFER",),
        expected_reject="buffer",
    ),
    SourceControl(
        "word-buffer",
        "PKB82WB",
        ("PKBRPT_WORD_BUFFER",),
        expected_reject="shape",
    ),
    SourceControl(
        "vla-buffer",
        "PKB82VL",
        ("PKBRPT_VLA_BUFFER",),
        expected_reject="shape",
    ),
)

MUTATION_CASES = (
    MutationCase("buffer-pointer-type", "1:type:18", "buffer-type"),
    MutationCase("buffer-identity", "1:identity:88", "buffer"),
    MutationCase("second-address-identity", "7:identity:88", "stores"),
    MutationCase("index-one-value", "8:immediate:2", "stores"),
    MutationCase("index-address-base", "9:src1:0", "stores"),
    MutationCase("index-address-index", "9:src2:0", "stores"),
    MutationCase("index-stride", "9:immediate:2", "stores"),
    MutationCase("index-width", "9:memory_size:2", "stores"),
    MutationCase("byte-range", "11:immediate:256", "stores"),
    MutationCase("byte-type", "11:type:2", "stores"),
    MutationCase("store-address", "12:src1:0", "stores"),
    MutationCase("store-value", "12:src2:4", "stores"),
    MutationCase("store-width", "12:memory_size:2", "stores"),
    MutationCase("final-byte-negative", "23:immediate:-1", "stores"),
    MutationCase("pack-address-identity", "27:identity:88", "pack-call"),
    MutationCase("pack-argument", "28:src1:20", "pack-call"),
    MutationCase("pack-indirect", "29:src1:20", "pack-call"),
    MutationCase("pack-target", "29:identity:88", "pack-call"),
    MutationCase("pack-return-type", "29:type:2", "pack-call"),
    MutationCase("format-type", "25:type:18", "print-call"),
    MutationCase("format-argument", "26:src1:21", "print-call"),
    MutationCase("print-value", "30:src1:20", "print-call"),
    MutationCase("print-indirect", "31:src1:20", "print-call"),
    MutationCase("print-return-type", "31:type:4", "print-call"),
    MutationCase("zero-return", "32:immediate:1", "print-call"),
    MutationCase("return-source", "33:src1:22", "print-call"),
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
    command.extend(f"-D{define}" for define in defines)
    command.extend([SOURCE, "-o", str(output)])
    return command


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
            "baseline did not select packed-byte-report-schedule\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "packed-byte-report selected hash changed\n" + report
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
        DCC_MIR_SELECT_CANDIDATE="hybrid",
        DCC_MIR_COST_REPORT="1",
    )
    output = output_dir / "forced-hybrid.MAC"
    report = run(compiler_command(compiler, output), environment)
    if EXACT_COST not in report:
        raise RuntimeError(
            "cost report did not retain the exact scheduled incumbent\n"
            + report
        )
    if HYBRID_SELECTED_COST.search(report) is None:
        raise RuntimeError(
            "forced hybrid cost candidate was not selected\n" + report
        )
    if selector_from(report) != "hybrid-homed-scalar-cfg":
        raise RuntimeError(
            "forced hybrid control selected the wrong emitter\n" + report
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
            "expected 'spilled-scalar-cfg'\n" + report
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
        default="build/packed-byte-report-wave82-audit",
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
        f"packed-byte-report Wave 82 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"runtime oracle={EXPECTED_RUNTIME[0]}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
