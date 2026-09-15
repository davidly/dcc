#!/usr/bin/env python3
"""Audit the retained Catalan exact schedule semantically."""

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
SOURCE = "tests/mir-clobber/catw23.c"
EXACT_ACCEPT = (
    "MIR machine function=main "
    "template=catalan-driver-schedule accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=main "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=main "
    r"selector=(?P<selector>homed-scalar-cfg|"
    r"hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=main "
    r"template=catalan-driver-schedule reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "8b8fc1805563e9abb5062103d1c5cebe68a8b98a8702c1310bfb5743a27f39d9"
)
BASELINE_SELECTED_HASH = "fab61cdb"
FULL_IO_SHA256 = (
    "120abc4bf684f61dda14b57f3ad897f612f157c8a61cae8d3cd9e772a428b996"
)
FULL_IO_SELECTED_HASH = "4728f29d"
EXPECTED_RUNTIME = (
    "0.9159655941772190150546035149323841107741493742816721342664981196217630197762547694793565129261151062",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=11)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expected_runtime: tuple[str, ...] = EXPECTED_RUNTIME
    expect_exact: bool = False
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    defines: tuple[str, ...] = ()
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str = ""


SOURCE_CONTROLS = (
    SourceControl("baseline", "CAT60OK", expect_exact=True,
                  expected_selector="scheduled-machine-cfg"),
    SourceControl(
        "zero-wrapper-4096",
        "CAT60ZW",
        ("CATALAN_ZERO_WRAPPER_4096",),
        expected_reject="zero-calls",
    ),
    SourceControl(
        "is-zero-wrapper-4096",
        "CAT60IW",
        ("CATALAN_IS_ZERO_WRAPPER_4096",),
        expected_reject="second-loop-header",
    ),
    SourceControl(
        "term-wrapper-4096",
        "CAT60TW",
        ("CATALAN_ADD_TERM_WRAPPER_4096",),
        expected_reject="second-terms",
    ),
    SourceControl(
        "div-small-wrapper-4096",
        "CAT60DW",
        ("CATALAN_DIV_SMALL_WRAPPER_4096",),
        expected_reject="second-loop-tail",
    ),
    SourceControl(
        "fixed-print-wrapper",
        "CAT60PW",
        ("CATALAN_FIXED_PRINT_WRAPPER",),
        expected_reject="metadata",
    ),
    SourceControl(
        "putchar-wrapper",
        "CAT60CW",
        ("CATALAN_PUTCHAR_WRAPPER",),
        expected_reject="putchar-function",
    ),
)
MUTATION_CASES = (
    MutationCase(
        "s16-index-width",
        "15:memory_size:2",
        expected_reject="metadata",
    ),
    MutationCase(
        "s16-first-value",
        "14:immediate:2",
        expected_reject="array-initializers",
    ),
    MutationCase(
        "first-loop-not-op",
        "36:immediate:0",
        expected_reject="first-loop-header",
    ),
    MutationCase(
        "first-loop-divisor",
        "158:immediate:15",
        expected_reject="first-loop-tail",
    ),
    MutationCase(
        "second-loop-scale",
        "186:immediate:7",
        expected_reject="second-loop-header",
    ),
    MutationCase(
        "second-loop-divisor",
        "305:immediate:2048",
        expected_reject="second-loop-tail",
    ),
    MutationCase(
        "report-argument-source",
        "322:src1:213",
        expected_reject="initial-report",
    ),
    MutationCase(
        "printed-limit",
        "358:immediate:99",
        expected_reject="outer-print-loop",
    ),
    MutationCase(
        "digit-start-divisor",
        "372:immediate:8",
        expected_reject="inner-print-loop",
    ),
    MutationCase(
        "digit-modulus",
        "404:immediate:11",
        expected_reject="digit-body",
    ),
    MutationCase(
        "newline-constant",
        "432:immediate:13",
        expected_reject="newline-return",
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


def compiler_command(compiler, output, defines=(), extra_args=()):
    command = [
        str(compiler), "-fstack-check", "-stack", "768", "-I", ".",
    ]
    for define in defines:
        command.append(f"-D{define}")
    command.extend(extra_args)
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


def compile_baseline(compiler, output_dir, extra_args=()):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="catalan-driver-schedule",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    output = output_dir / (
        "baseline-fullio.MAC" if extra_args else "baseline.MAC"
    )
    report = run(compiler_command(
        compiler, output, extra_args=extra_args
    ), env)
    if EXACT_ACCEPT not in report or EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select catalan-driver-schedule\n" + report
        )
    selected_hash = (
        FULL_IO_SELECTED_HASH if extra_args else BASELINE_SELECTED_HASH
    )
    if f"selected-hash={selected_hash}" not in report:
        raise RuntimeError(
            "Catalan baseline selected hash changed\n" + report
        )
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    expected_digest = FULL_IO_SHA256 if extra_args else BASELINE_SHA256
    if digest != expected_digest:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {expected_digest}"
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
        DCC_MIR_MACHINE_TEMPLATE="catalan-driver-schedule",
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
        "dcc-stack-bytes=768",
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
        if (control.expected_reject is not None and
                reject_reason != control.expected_reject):
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
        DCC_MIR_MACHINE_TEMPLATE="catalan-driver-schedule",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    report = run(
        compiler_command(compiler, output, case.defines),
        env,
    )
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
        reject_reason,
        ",".join(case.defines) or "-",
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
            results = [future.result()
                       for future in concurrent.futures.as_completed(futures)]
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
        default="build/catalan-wave60-audit",
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

    baseline_digest = compile_baseline(compiler, output_dir)
    full_io_digest = compile_baseline(
        compiler, output_dir, ("-ffloatio", "-flongio")
    )
    source_rows = run_source_controls(compiler, dccmake, output_dir)
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
        f"catalan Wave 60 mutations={len(mutation_rows)} {outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"baseline-sha256={baseline_digest}")
    print(f"full-io-sha256={full_io_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
