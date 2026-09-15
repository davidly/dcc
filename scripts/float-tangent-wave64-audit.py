#!/usr/bin/env python3
"""Audit the retained float-tangent exact schedule semantically."""

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
FUNCTION = "tanrat"
SOURCE = "tests/mir-clobber/tanrat.c"
EXACT_SELECTION = (
    "MIR selection function=tanrat "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=tanrat "
    r"selector=(?P<selector>homed-scalar-cfg|"
    r"hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=tanrat "
    r"template=float-tangent-rational reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "e0acbc154ecc7efc3491c561749c56a7ea4ece401de8e14b89955a11421db41c"
)
BASELINE_SELECTED_HASH = "948521bf"
BASELINE_RUNTIME = ("float tangent failures=0",)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=13)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
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
    SourceControl(
        "baseline",
        "TAN64OK",
        expect_exact=True,
        expected_selector="scheduled-machine-cfg",
    ),
    SourceControl(
        "extra-arithmetic",
        "TAN64NM",
        ("TANRAT_OPCODE_NEAR_MATCH",),
        expected_reject="shape",
    ),
    SourceControl(
        "variadic-remainder",
        "TAN64VR",
        ("TANRAT_VARIADIC_REMAINDER",),
        expected_reject="call",
    ),
)
MUTATION_CASES = (
    MutationCase(
        "parameter-type",
        "1:type:2",
        expected_reject="parameter",
    ),
    MutationCase(
        "invert-store-width",
        "3:memory_size:1",
        expected_reject="initializers",
    ),
    MutationCase(
        "call-argument-source",
        "5:src1:1",
        expected_reject="call",
    ),
    MutationCase(
        "call-return-type",
        "8:type:2",
        expected_reject="call",
    ),
    MutationCase(
        "reduced-load-identity",
        "36:identity:120",
        expected_reject="reduced-access",
    ),
    MutationCase(
        "repeated-pi-constant",
        "16:immediate:0",
        expected_reject="constants",
    ),
    MutationCase(
        "negation-source",
        "25:src1:12",
        expected_reject="negations",
    ),
    MutationCase(
        "period-operator",
        "17:immediate:43",
        expected_reject="period-reduction",
    ),
    MutationCase(
        "quadrant-operand",
        "38:src2:20",
        expected_reject="quadrants",
    ),
    MutationCase(
        "rational-operator",
        "71:immediate:43",
        expected_reject="rational",
    ),
    MutationCase(
        "denominator-zero-source",
        "87:src1:47",
        expected_reject="result",
    ),
    MutationCase(
        "result-operator",
        "94:immediate:42",
        expected_reject="result",
    ),
    MutationCase(
        "phi-source",
        "112:src1:67",
        expected_reject="result",
    ),
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
    matches = list(REJECT_REASON.finditer(report))
    return matches[-1].group("reason") if matches else None


def baseline_compile(compiler, output_dir):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="float-tangent-rational",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), env)
    if EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select float-tangent-rational\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "float-tangent baseline selected hash changed\n" + report
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
        DCC_MIR_MACHINE_TEMPLATE="float-tangent-rational",
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
    selector = selector_from(build_output)
    reject_reason = reject_reason_from(build_output)
    if control.expect_exact:
        if selector != "scheduled-machine-cfg":
            raise RuntimeError(
                f"{control.name} did not retain exact selection\n"
                f"{build_output}"
            )
    else:
        if selector != control.expected_selector:
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
    for text in BASELINE_RUNTIME:
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
        DCC_MIR_MACHINE_TEMPLATE="float-tangent-rational",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(compiler_command(compiler, output), env)
    selector = selector_from(report)
    reject_reason = reject_reason_from(report)
    output.unlink(missing_ok=True)
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
        default="build/float-tangent-wave64-audit",
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
        f"float-tangent Wave 64 mutations={len(mutation_rows)} {outcomes}"
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
