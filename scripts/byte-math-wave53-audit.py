#!/usr/bin/env python3
"""Audit the retained byte-math exact schedule semantically."""

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
FUNCTION = "op_math"
SOURCE = "tests/mir-clobber/bytemath.c"
EXACT_ACCEPT = (
    "MIR machine function=op_math "
    "template=byte-math-flags accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=op_math "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=op_math "
    r"selector=(?P<selector>homed-scalar-cfg|"
    r"hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=op_math "
    r"template=byte-math-flags reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "447f62b00fc19ac8f9317ff531224c4574dc4b54b875120c14b0f44eb46625c2"
)
BASELINE_SELECTED_HASH = "f3cbefc6"
BASELINE_RUNTIME = ("byte math failures=0",)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=7)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expected_runtime: tuple[str, ...] = BASELINE_RUNTIME
    expect_exact: bool = False
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str = ""


SOURCE_CONTROLS = (
    SourceControl("baseline", "BMW53OK", expect_exact=True,
                  expected_selector="scheduled-machine-cfg"),
    SourceControl("ansi-helpers", "BMW53AN",
                  ("MIR_CLOBBER_BYTE_MATH_ANSI_HELPERS",),
                  expect_exact=True,
                  expected_selector="scheduled-machine-cfg"),
    SourceControl("compare-variadic", "BMW53CV",
                  ("MIR_CLOBBER_BYTE_MATH_COMPARE_VARIADIC",),
                  expected_reject="instruction-metadata"),
    SourceControl("decimal-variadic", "BMW53DV",
                  ("MIR_CLOBBER_BYTE_MATH_DECIMAL_VARIADIC",),
                  expected_reject="instruction-metadata"),
    SourceControl("return-int", "BMW53RT",
                  ("MIR_CLOBBER_BYTE_MATH_RETURN_INT",),
                  expected_reject="shape"),
    SourceControl("vla-shape", "BMW53VL",
                  ("MIR_CLOBBER_BYTE_MATH_VLA",),
                  expected_reject="shape"),
)
MUTATION_CASES = (
    MutationCase("instruction-metadata-type", "99:type:33",
                 expected_reject="instruction-metadata"),
    MutationCase("wide-store-width", "106:memory_size:1",
                 expected_reject="wide-store"),
    MutationCase("state-pointer-type", "15:type:18",
                 expected_reject="state-pointer-type"),
    MutationCase("compare-call-indirect", "21:src1:18",
                 expected_reject="compare-call-indirect"),
    MutationCase("compare-return-value", "22:src1:17",
                 expected_reject="return-value"),
    MutationCase("decimal-call-indirect", "67:src1:64",
                 expected_reject="decimal-call-indirect"),
    MutationCase("decimal-return-value", "68:src1:65",
                 expected_reject="return-value"),
)


def run(command, env=None, timeout=180):
    if env is None:
        env = os.environ.copy()
    else:
        env = env.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=env,
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


def baseline_compile(compiler, output_dir):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="byte-math-flags",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), env)
    if EXACT_ACCEPT not in report or EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select byte-math-flags\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "byte-math baseline selected hash changed\n" + report
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
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="byte-math-flags",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
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
    build_output = run(command, env, timeout=300)
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
        if exact_selected or selector != control.expected_selector:
            raise RuntimeError(
                f"{control.name} did not fall back to "
                f"{control.expected_selector}\n{build_output}"
            )
        if reject_reason != control.expected_reject:
            raise RuntimeError(
                f"{control.name} reject {reject_reason!r}, "
                f"expected {control.expected_reject!r}\n{build_output}"
            )
    runtime = run(
        ["ntvcm", "-p", "-s:0", str(build_dir / f"{control.output_name}.COM")],
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
    env = os.environ.copy()
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="byte-math-flags",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), env)
    selector = selector_from(report)
    reject_reason = reject_reason_from(report)
    output.unlink(missing_ok=True)
    if EXACT_ACCEPT in report:
        raise RuntimeError(
            f"{case.name} unexpectedly kept the exact schedule\n{report}"
        )
    if selector != case.expected_selector:
        raise RuntimeError(
            f"{case.name} selected {selector!r}, "
            f"expected {case.expected_selector!r}\n{report}"
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
        reject_reason or "",
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
                executor.submit(
                    run_mutation, compiler, work_dir, case
                )
                for case in MUTATION_CASES
            ]
            return [future.result() for future in futures]
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)


def write_rows(path, rows):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            (
                "name", "kind", "spec", "result",
                "selector", "reject_reason", "mode",
            )
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir", default="build/byte-math-wave53-audit"
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

    baseline_compile(compiler, output_dir)
    rows = run_source_controls(compiler, dccmake, output_dir)
    rows.extend(run_mutation_cases(compiler, output_dir, args.jobs))
    rows.sort(key=lambda row: (row[1], row[0], row[6]))
    write_rows(output_dir / "audit.tsv", rows)

    mutation_outcomes = Counter(row[3] for row in rows if row[1] == "mutation")
    if mutation_outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            "mutation outcomes changed: "
            f"{mutation_outcomes} != {EXPECTED_MUTATION_OUTCOMES}"
        )

    runtime_rows = [row for row in rows if row[1] == "runtime"]
    exact_runtime = sum(row[3] == "exact" for row in runtime_rows)
    generic_runtime = sum(row[3] == "generic" for row in runtime_rows)
    print(
        "byte-math Wave 53 audit passed: "
        f"{exact_runtime} exact runtime controls, "
        f"{generic_runtime} generic runtime controls, "
        f"{mutation_outcomes['rejected']}/{len(MUTATION_CASES)} "
        "targeted mutations rejected."
    )


if __name__ == "__main__":
    main()
