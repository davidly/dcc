#!/usr/bin/env python3
"""Audit both retained matrix-product exact schedule kinds semantically."""

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
SOURCE = "tests/mir-clobber/matkind.c"
TEMPLATE = "matrix-product-schedule"
FORCED_CANDIDATE = "spilled-store-address"
BASELINE_SHA256 = (
    "b3d565637021d445de314147095b9e7aba6fcd7a5de036f89e130d952a263452"
)
EXPECTED_RUNTIME = (
    "matrix kind transposed failures=0 values=32767,32031,-31584,32767",
    "matrix kind outer failures=0 edges=30512,-32768,32767,-32768",
    "matrix product kind failures=0",
)


@dataclass(frozen=True)
class Schedule:
    function: str
    selected_hash: str


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...]
    exact_functions: tuple[str, ...]


@dataclass(frozen=True)
class MutationCase:
    function: str
    name: str
    spec: str


SCHEDULES = (
    Schedule("matrix_product_transposed", "88cdd4b5"),
    Schedule("matrix_product_outer", "eca25a44"),
)
SOURCE_CONTROLS = (
    SourceControl(
        "baseline",
        "MK140OK",
        (),
        ("matrix_product_transposed", "matrix_product_outer"),
    ),
    SourceControl(
        "transposed-near-match",
        "MK140TX",
        ("MATKIND_TRANSPOSED_EXTRA_ARITHMETIC",),
        ("matrix_product_outer",),
    ),
    SourceControl(
        "outer-near-match",
        "MK140OX",
        ("MATKIND_OUTER_EXTRA_ARITHMETIC",),
        ("matrix_product_transposed",),
    ),
)
MUTATION_CASES = (
    MutationCase("matrix_product_transposed", "parameter-type", "1:type:2"),
    MutationCase("matrix_product_transposed", "clear-argument", "8:src1:1"),
    MutationCase("matrix_product_transposed", "clear-zero-type", "9:type:4"),
    MutationCase("matrix_product_transposed", "clear-size-type", "12:type:4"),
    MutationCase("matrix_product_transposed", "clear-call-type", "18:type:2"),
    MutationCase("matrix_product_transposed", "outer-zero-type", "20:type:2"),
    MutationCase("matrix_product_transposed", "outer-store-width", "21:memory_size:2"),
    MutationCase("matrix_product_transposed", "outer-phi-backedge", "27:src2:67"),
    MutationCase("matrix_product_transposed", "outer-conversion-type", "30:type:4"),
    MutationCase("matrix_product_transposed", "outer-comparison-op", "32:immediate:61"),
    MutationCase("matrix_product_transposed", "outer-branch-source", "33:src1:21"),
    MutationCase("matrix_product_transposed", "source-step-constant-type", "35:type:2"),
    MutationCase("matrix_product_transposed", "source-step-type", "36:type:2"),
    MutationCase("matrix_product_transposed", "source-store-width", "37:memory_size:1"),
    MutationCase("matrix_product_transposed", "source-load-type", "38:type:4"),
    MutationCase("matrix_product_transposed", "scalar-store-width", "40:memory_size:1"),
    MutationCase("matrix_product_transposed", "inner-zero-type", "42:type:2"),
    MutationCase("matrix_product_transposed", "inner-store-width", "43:memory_size:2"),
    MutationCase("matrix_product_transposed", "inner-index-type", "52:type:2"),
    MutationCase("matrix_product_transposed", "inner-comparison-type", "56:type:4"),
    MutationCase("matrix_product_transposed", "inner-branch-source", "57:src1:21"),
    MutationCase("matrix_product_transposed", "matrix-step-constant-type", "59:type:2"),
    MutationCase("matrix_product_transposed", "matrix-step-type", "60:type:2"),
    MutationCase("matrix_product_transposed", "matrix-store-width", "61:memory_size:1"),
    MutationCase("matrix_product_transposed", "matrix-load-type", "62:type:4"),
    MutationCase("matrix_product_transposed", "matrix-temp-width", "63:memory_size:1"),
    MutationCase("matrix_product_transposed", "matrix-temp-type", "64:type:4"),
    MutationCase("matrix_product_transposed", "matrix-promotion-type", "65:type:2"),
    MutationCase("matrix_product_transposed", "product-operator", "68:immediate:43"),
    MutationCase("matrix_product_transposed", "convert-argument-type", "69:type:2"),
    MutationCase("matrix_product_transposed", "convert-call-identity", "70:identity:120"),
    MutationCase("matrix_product_transposed", "convert-temp-width", "71:memory_size:1"),
    MutationCase("matrix_product_transposed", "output-load-type", "72:type:4"),
    MutationCase("matrix_product_transposed", "output-index-type", "74:type:2"),
    MutationCase("matrix_product_transposed", "output-index-width", "74:memory_size:1"),
    MutationCase("matrix_product_transposed", "output-temp-width", "75:memory_size:1"),
    MutationCase("matrix_product_transposed", "output-temp-type", "76:type:4"),
    MutationCase("matrix_product_transposed", "output-loadind-type", "78:type:4"),
    MutationCase("matrix_product_transposed", "output-promotion-type", "79:type:2"),
    MutationCase("matrix_product_transposed", "sum-type", "82:type:2"),
    MutationCase("matrix_product_transposed", "clamp-argument-type", "83:type:2"),
    MutationCase("matrix_product_transposed", "clamp-call-identity", "84:identity:120"),
    MutationCase("matrix_product_transposed", "output-store-width", "85:memory_size:1"),
    MutationCase("matrix_product_transposed", "inner-one-type", "88:type:2"),
    MutationCase("matrix_product_transposed", "inner-increment-type", "89:type:2"),
    MutationCase("matrix_product_transposed", "inner-loop-store-width", "90:memory_size:2"),
    MutationCase("matrix_product_transposed", "outer-one-type", "96:type:2"),
    MutationCase("matrix_product_transposed", "outer-increment-type", "97:type:2"),
    MutationCase("matrix_product_transposed", "outer-loop-store-width", "98:memory_size:2"),
    MutationCase("matrix_product_outer", "parameter-type", "1:type:2"),
    MutationCase("matrix_product_outer", "outer-zero-type", "7:type:2"),
    MutationCase("matrix_product_outer", "outer-store-width", "8:memory_size:2"),
    MutationCase("matrix_product_outer", "outer-phi-backedge", "15:src2:59"),
    MutationCase("matrix_product_outer", "outer-conversion-type", "18:type:4"),
    MutationCase("matrix_product_outer", "outer-comparison-op", "20:immediate:61"),
    MutationCase("matrix_product_outer", "source-step-constant-type", "23:type:2"),
    MutationCase("matrix_product_outer", "source-step-type", "24:type:2"),
    MutationCase("matrix_product_outer", "source-store-width", "25:memory_size:1"),
    MutationCase("matrix_product_outer", "source-load-type", "26:type:4"),
    MutationCase("matrix_product_outer", "scalar-store-width", "28:memory_size:1"),
    MutationCase("matrix_product_outer", "inner-zero-type", "30:type:2"),
    MutationCase("matrix_product_outer", "inner-store-width", "31:memory_size:2"),
    MutationCase("matrix_product_outer", "inner-index-type", "41:type:2"),
    MutationCase("matrix_product_outer", "inner-comparison-type", "45:type:4"),
    MutationCase("matrix_product_outer", "inner-branch-source", "46:src1:12"),
    MutationCase("matrix_product_outer", "right-index-type", "49:type:2"),
    MutationCase("matrix_product_outer", "right-index-width", "49:memory_size:1"),
    MutationCase("matrix_product_outer", "right-load-type", "50:type:4"),
    MutationCase("matrix_product_outer", "right-load-width", "50:memory_size:1"),
    MutationCase("matrix_product_outer", "right-temp-width", "51:memory_size:1"),
    MutationCase("matrix_product_outer", "scalar-promotion-type", "53:type:2"),
    MutationCase("matrix_product_outer", "right-temp-type", "54:type:4"),
    MutationCase("matrix_product_outer", "right-promotion-type", "55:type:2"),
    MutationCase("matrix_product_outer", "product-operator", "56:immediate:43"),
    MutationCase("matrix_product_outer", "convert-argument-type", "57:type:2"),
    MutationCase("matrix_product_outer", "convert-call-identity", "58:identity:120"),
    MutationCase("matrix_product_outer", "convert-temp-width", "59:memory_size:1"),
    MutationCase("matrix_product_outer", "matrix-step-constant-type", "61:type:2"),
    MutationCase("matrix_product_outer", "matrix-step-type", "62:type:2"),
    MutationCase("matrix_product_outer", "matrix-store-width", "63:memory_size:1"),
    MutationCase("matrix_product_outer", "matrix-temp-width", "64:memory_size:1"),
    MutationCase("matrix_product_outer", "matrix-temp-type", "65:type:4"),
    MutationCase("matrix_product_outer", "matrix-load-type", "67:type:4"),
    MutationCase("matrix_product_outer", "matrix-promotion-type", "68:type:2"),
    MutationCase("matrix_product_outer", "converted-load-type", "69:type:4"),
    MutationCase("matrix_product_outer", "sum-type", "71:type:2"),
    MutationCase("matrix_product_outer", "clamp-argument-type", "72:type:2"),
    MutationCase("matrix_product_outer", "clamp-call-identity", "73:identity:120"),
    MutationCase("matrix_product_outer", "matrix-result-width", "74:memory_size:1"),
    MutationCase("matrix_product_outer", "inner-one-type", "77:type:2"),
    MutationCase("matrix_product_outer", "inner-increment-type", "78:type:2"),
    MutationCase("matrix_product_outer", "inner-loop-store-width", "79:memory_size:2"),
    MutationCase("matrix_product_outer", "outer-one-type", "85:type:2"),
    MutationCase("matrix_product_outer", "outer-increment-type", "86:type:2"),
    MutationCase("matrix_product_outer", "outer-loop-store-width", "87:memory_size:2"),
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=len(MUTATION_CASES))


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


def diagnostic_environment(function=None):
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
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
    )
    if function is not None:
        environment.update(
            DCC_MIR_SELECT_FUNCTION=function,
            DCC_MIR_SELECT_REPORT_FUNCTION=function,
            DCC_MIR_MACHINE_FUNCTION=function,
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


def exact_selection(function):
    return (
        f"MIR selection function={function} "
        "selector=scheduled-machine-cfg result=mir"
    )


def exact_cost(function):
    return (
        f"MIR cost-selected function={function} "
        "candidate=exact-scheduled selector=scheduled-machine-cfg"
    )


def generic_selection(function):
    return (
        f"MIR selection function={function} "
        "selector=spilled-scalar-cfg result=mir"
    )


def generic_cost(function):
    return (
        f"MIR cost-selected function={function} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )


def selected_hash(report, function):
    match = re.search(
        rf"MIR selection function={re.escape(function)} .*"
        r"selected-hash=(?P<hash>[0-9a-f]{8})",
        report,
    )
    return match.group("hash") if match is not None else None


def reject_reason(report, function):
    matches = list(re.finditer(
        rf"MIR machine function={re.escape(function)} "
        rf"template={re.escape(TEMPLATE)} reject=(?P<reason>\S+)",
        report,
    ))
    return matches[-1].group("reason") if matches else None


def require_exact(report, function, context):
    if exact_selection(function) not in report or exact_cost(function) not in report:
        raise RuntimeError(
            f"{context} did not select {function} exactly\n{report}"
        )


def require_generic(report, function, context, require_reject=True):
    if exact_selection(function) in report or exact_cost(function) in report:
        raise RuntimeError(
            f"{context} unexpectedly retained {function} exactly\n{report}"
        )
    if (
        generic_selection(function) not in report
        or generic_cost(function) not in report
    ):
        raise RuntimeError(
            f"{context} did not select {FORCED_CANDIDATE} for "
            f"{function}\n{report}"
        )
    reason = reject_reason(report, function)
    if require_reject and reason is None:
        raise RuntimeError(
            f"{context} did not report a {TEMPLATE} rejection\n{report}"
        )
    return reason


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    for schedule in SCHEDULES:
        require_exact(report, schedule.function, "baseline")
        if selected_hash(report, schedule.function) != schedule.selected_hash:
            raise RuntimeError(
                f"{schedule.function} selected hash changed\n{report}"
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
    report = run(command, diagnostic_environment(), timeout=300)
    states = []
    for schedule in SCHEDULES:
        if schedule.function in control.exact_functions:
            require_exact(report, schedule.function, control.name)
            states.append(f"{schedule.function}=exact")
        else:
            reason = require_generic(
                report, schedule.function, control.name,
                require_reject=False,
            )
            states.append(
                f"{schedule.function}=generic:{reason or 'shape'}"
            )
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
        "passed",
        ";".join(states),
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
    stem = f"{case.function}-{case.name}"
    ordinary_path = work_dir / f"{stem}-ordinary.MAC"
    forced_path = work_dir / f"{stem}-forced.MAC"
    environment = diagnostic_environment(case.function)
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=case.function,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), environment
    )
    reason = require_generic(
        ordinary_report, case.function, case.name
    )

    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(
        forced_report, case.function, f"{case.name} forced"
    )
    if ordinary_path.read_bytes() != forced_path.read_bytes():
        raise RuntimeError(
            f"{case.name} forced fallback differs from ordinary fallback"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.function,
        case.name,
        case.spec,
        "rejected",
        FORCED_CANDIDATE,
        reason,
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


def write_tsv(path, header, rows):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(header)
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/matrix-product-kind-wave140-audit",
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
    write_tsv(
        output_dir / "source-controls.tsv",
        ("name", "kind", "defines", "outcome", "schedules", "mode"),
        source_rows,
    )
    write_tsv(
        output_dir / "mutation-census.tsv",
        (
            "function", "name", "spec", "outcome",
            "candidate", "reject_reason", "control",
        ),
        mutation_rows,
    )

    outcomes = Counter(row[3] for row in mutation_rows)
    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    by_function = Counter(row[0] for row in mutation_rows)
    print(
        f"matrix product kind Wave 140 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        "mutation functions="
        + ",".join(
            f"{function}:{by_function[function]}"
            for function in sorted(by_function)
        )
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
