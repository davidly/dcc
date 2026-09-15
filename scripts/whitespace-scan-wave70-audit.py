#!/usr/bin/env python3
"""Audit the bounded whitespace-scan exact schedule semantically."""

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
FUNCTION = "whitespace_scan"
SOURCE = "tests/mir-clobber/wsscan.c"
TEMPLATE = "whitespace-scan-schedule"
EXACT_SELECTION = (
    "MIR selection function=whitespace_scan "
    "selector=scheduled-machine-cfg result=mir"
)
SELECTION = re.compile(
    r"MIR selection function=whitespace_scan "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=whitespace_scan "
    r"template=whitespace-scan-schedule reject=(?P<reason>\S+)"
)
EXACT_COST = (
    "MIR cost-candidate function=whitespace_scan candidate=incumbent "
    "selector=scheduled-machine-cfg emitted=1 selectable=1 selected=1"
)
HYBRID_SELECTED_COST = re.compile(
    r"MIR cost-candidate function=whitespace_scan candidate=hybrid "
    r"selector=hybrid-homed-scalar-cfg emitted=1 selectable=1 "
    r"selected=1"
)
BASELINE_SHA256 = (
    "31c83b5f4d79640c9908a480717fa7a952afd7d570e3069daebc3860cb92850b"
)
BASELINE_SELECTED_HASH = "0259e664"
EXPECTED_RUNTIME = (
    "whitespace scan cursor=1 line=41 calls=2 failures=0",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=25)


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
    SourceControl("baseline", "WSS70OK", expect_exact=True),
    SourceControl(
        "renamed-helper",
        "WSS70RN",
        ("WSSCAN_RENAMED_HELPER",),
        expect_exact=True,
    ),
    SourceControl(
        "variadic-helper",
        "WSS70VA",
        ("WSSCAN_VARIADIC_HELPER",),
        expected_reject="helper-function",
    ),
    SourceControl(
        "extra-branch",
        "WSS70BR",
        ("WSSCAN_EXTRA_BRANCH",),
    ),
)

MUTATION_CASES = (
    MutationCase("bound-load-type", "4:type:36", "bound"),
    MutationCase("bound-operator", "8:immediate:61", "bound"),
    MutationCase("bound-branch-source", "9:src1:5", "bound"),
    MutationCase("source-load-type", "12:type:1", "helper-call"),
    MutationCase("condition-index-stride", "16:immediate:2", "helper-call"),
    MutationCase("condition-byte-width", "17:memory_size:2", "helper-call"),
    MutationCase("unsigned-byte-cast", "18:type:1", "helper-call"),
    MutationCase("helper-argument-flow", "20:src1:14", "helper-call"),
    MutationCase("helper-identity", "21:identity:89", "helper-function"),
    MutationCase("helper-return-type", "21:type:17", "helper-function"),
    MutationCase("short-circuit-true", "24:immediate:0", "short-circuit"),
    MutationCase("short-circuit-phi", "29:src1:19", "short-circuit"),
    MutationCase("body-source-type", "33:type:1", "body-read"),
    MutationCase("body-index-stride", "37:immediate:2", "body-read"),
    MutationCase("body-byte-width", "38:memory_size:2", "body-read"),
    MutationCase("newline-value", "39:immediate:13", "body-read"),
    MutationCase("newline-operator", "41:immediate:275", "body-read"),
    MutationCase("line-load-type", "45:type:34", "updates"),
    MutationCase("line-increment", "46:immediate:2", "updates"),
    MutationCase("line-store-width", "48:memory_size:1", "updates"),
    MutationCase("cursor-load-type", "52:type:36", "updates"),
    MutationCase("cursor-increment", "53:immediate:2", "updates"),
    MutationCase("cursor-store-width", "55:memory_size:2", "updates"),
    MutationCase("state-member-overlap", "44:immediate:6", "state"),
    MutationCase("state-member-range", "44:immediate:128", "state"),
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
    reasons = [
        match.group("reason") for match in REJECT_REASON.finditer(report)
    ]
    return next((reason for reason in reasons if reason != "opcodes"), None)


def baseline_compile(compiler, output_dir):
    environment = report_environment()
    environment.pop("DCC_MIR_MACHINE_MUTATE", None)
    environment.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), environment)
    if EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select whitespace-scan-schedule\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "whitespace-scan baseline selected hash changed\n" + report
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
        if selector != "hybrid-homed-scalar-cfg":
            raise RuntimeError(
                f"{control.name} did not use hybrid fallback\n"
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
    if EXACT_SELECTION in report:
        raise RuntimeError(
            f"{case.name} unexpectedly kept the exact schedule\n{report}"
        )
    if selector != "hybrid-homed-scalar-cfg":
        raise RuntimeError(
            f"{case.name} selected {selector!r}, "
            "expected 'hybrid-homed-scalar-cfg'\n" + report
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
        default="build/whitespace-scan-wave70-audit",
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
        f"whitespace scan Wave 70 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print("forced candidate cost control=hybrid")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
