#!/usr/bin/env python3
"""Audit remaining exec-recursion exact-schedule proof boundaries."""

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
FUNCTION = "rw14_recurse"
SOURCE = "tests/mir-clobber/exrw14.c"
TEMPLATE = "exec-recursion-schedule"
EXACT_ACCEPT = (
    "MIR machine function=rw14_recurse "
    "template=exec-recursion-schedule accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=rw14_recurse "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=rw14_recurse "
    r"selector=(?P<selector>hybrid-homed-scalar-cfg|"
    r"homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=rw14_recurse "
    r"template=exec-recursion-schedule reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "28bb381ce9849a6df141705e538c20d53dc48d7588199ec41b0f3fda37a5765a"
)
BASELINE_SELECTED_HASH = "794b3954"
EXPECTED_RUNTIME = (
    "RW14 oracle=3550463 first=12345 second=22222 "
    "exec=1 execv=1 failures=0",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=35)


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
    SourceControl("baseline", "ER64BASE", expect_exact=True),
    SourceControl(
        "renamed-helpers",
        "ER64RH",
        ("RW64_RENAME_HELPERS",),
        expect_exact=True,
    ),
    SourceControl(
        "renamed-failure-global",
        "ER64RF",
        ("RW64_RENAME_FAILURES",),
        expect_exact=True,
    ),
    SourceControl(
        "execv-void-pointer",
        "ER64VP",
        ("RW64_EXECV_VOID_POINTERS",),
        expected_reject="exec-calls",
    ),
    SourceControl(
        "fixed-reporters",
        "ER64FP",
        ("RW64_FIXED_REPORTS",),
        expected_reject="base-report",
    ),
    SourceControl(
        "unsigned-execv-return",
        "ER64UE",
        ("RW64_UNSIGNED_EXECV_RETURN",),
        expected_reject="shape",
    ),
    SourceControl(
        "unsigned-recursion-return",
        "ER64UR",
        ("RW64_UNSIGNED_RECURSION_RETURN",),
        expected_reject="shape",
    ),
    SourceControl(
        "unsigned-marker",
        "ER64UM",
        ("RW64_UNSIGNED_MARKER",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-local-check",
        "ER64LC",
        ("RW64_VOLATILE_LOCAL_CHECK",),
        expected_reject="recursive-state",
    ),
    SourceControl(
        "volatile-result",
        "ER64VR",
        ("RW64_VOLATILE_RESULT",),
        expected_reject="shape",
    ),
    SourceControl(
        "cfg-guard",
        "ER64CG",
        ("RW64_CFG_GUARD",),
        expected_reject="shape",
    ),
    SourceControl(
        "vla-guard",
        "ER64VL",
        ("RW64_VLA_GUARD",),
        expected_reject="shape",
    ),
)

MUTATION_CASES = (
    MutationCase("depth-parameter-type", "1:type:18", "parameters"),
    MutationCase("entry-zero", "4:immediate:1", "entry-control"),
    MutationCase("entry-compare-right", "6:src2:1", "entry-control"),
    MutationCase("mode-branch-value", "10:src1:1", "entry-control"),
    MutationCase("vector-base", "15:src1:8", "argument-vector"),
    MutationCase("vector-stride", "15:immediate:1", "argument-vector"),
    MutationCase("vector-store-value", "17:src2:14", "argument-vector"),
    MutationCase("execv-name-argument", "25:src1:17", "exec-calls"),
    MutationCase("execv-vector-argument", "27:src1:16", "exec-calls"),
    MutationCase("exec-call-return-type", "38:type:18", "exec-calls"),
    MutationCase("base-result-location", "44:identity:88", "base-result"),
    MutationCase("base-minus-one", "43:immediate:0", "base-result"),
    MutationCase("base-report-format", "48:src1:29", "base-report"),
    MutationCase("base-report-value", "50:src1:28", "base-report"),
    MutationCase("base-failure-location", "52:identity:88", "base-return"),
    MutationCase("base-failure-increment", "53:immediate:2", "base-return"),
    MutationCase("base-marker-location", "61:identity:88", "base-return"),
    MutationCase("local-store-location", "69:identity:88", "recursive-state"),
    MutationCase("depth-decrement", "71:immediate:2", "recursive-state"),
    MutationCase("recursive-depth-argument", "73:src1:44", "recursive-call"),
    MutationCase("recursive-marker-argument", "75:src1:43", "recursive-call"),
    MutationCase("recursive-mode-argument", "77:src1:44", "recursive-call"),
    MutationCase("result-store-location", "80:identity:88", "recursive-call"),
    MutationCase("result-report-format", "86:src1:52", "result-report"),
    MutationCase("result-report-value", "92:src1:53", "result-report"),
    MutationCase("result-failure-location", "94:identity:88", "result-report"),
    MutationCase("result-failure-increment", "95:immediate:2", "result-report"),
    MutationCase("local-load-location", "103:identity:88", "local-check"),
    MutationCase("local-check-branch", "108:src1:64", "local-check"),
    MutationCase(
        "local-report-format", "110:src1:67", "local-report-return"
    ),
    MutationCase(
        "local-report-expected", "116:src1:69", "local-report-return"
    ),
    MutationCase(
        "local-report-actual", "118:src1:70", "local-report-return"
    ),
    MutationCase(
        "local-failure-location", "120:identity:88",
        "local-report-return"
    ),
    MutationCase(
        "local-failure-increment", "121:immediate:2",
        "local-report-return"
    ),
    MutationCase(
        "final-result-location", "129:identity:88",
        "local-report-return"
    ),
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
        str(compiler), "-fstack-check", "-stack", "2048", "-I", ".",
    ]
    command.extend(f"-D{define}" for define in defines)
    command.extend([SOURCE, "-o", str(output)])
    return command


def selector_from(report):
    match = GENERIC_SELECTION.search(report)
    if match is not None:
        return match.group("selector")
    if EXACT_SELECTION in report:
        return "scheduled-machine-cfg"
    return None


def reject_reason_from(report):
    match = REJECT_REASON.search(report)
    return match.group("reason") if match is not None else None


def report_environment():
    environment = os.environ.copy()
    environment.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    return environment


def baseline_compile(compiler, output_dir):
    environment = report_environment()
    environment.pop("DCC_MIR_MACHINE_MUTATE", None)
    environment.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), environment)
    if EXACT_ACCEPT not in report or EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select exec-recursion-schedule\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "exec-recursion baseline selected hash changed\n" + report
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
    environment = report_environment()
    command = [
        str(dccmake),
        f"dcc-input={SOURCE}",
        f"dcc-output={control.output_name}",
        f"dcc-build-dir={build_dir}",
        f"dcc-tool={compiler}",
        f"dcc-peep={str(peep).lower()}",
        f"dcc-stack-check={str(stack_check).lower()}",
        "dcc-stack-bytes=2048",
    ]
    if control.defines:
        command.append(f"dcc-define={','.join(control.defines)}")
    build_output = run(command, environment, timeout=300)
    exact_selected = EXACT_ACCEPT in build_output
    selector = selector_from(build_output)
    reject_reason = reject_reason_from(build_output)
    if control.expect_exact:
        if not exact_selected or selector != "scheduled-machine-cfg":
            raise RuntimeError(
                f"{control.name} did not retain exact selection\n"
                f"{build_output}"
            )
    else:
        if exact_selected or selector != "spilled-scalar-cfg":
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
    if EXACT_ACCEPT in report:
        raise RuntimeError(
            f"{case.name} unexpectedly kept the exact schedule\n{report}"
        )
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
        default="build/exec-recursion-wave65-audit",
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
        f"exec recursion Wave 65 mutations={len(mutation_rows)} "
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
