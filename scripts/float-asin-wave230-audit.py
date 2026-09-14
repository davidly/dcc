#!/usr/bin/env python3
"""Audit the retained float-arcsine exact schedule semantically."""

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
FUNCTION = "fixture_asin"
SOURCE = "tests/mir-clobber/fasin.c"
TEMPLATE = "float-asin-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=fixture_asin "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=fixture_asin "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
GENERIC_SELECTION = (
    "MIR selection function=fixture_asin "
    "selector=spilled-scalar-cfg result=mir"
)
FORCED_GENERIC_COST = (
    "MIR cost-selected function=fixture_asin "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=fixture_asin "
    r"template=float-asin-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=fixture_asin .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "d2b59eea697c24e27009b21cebe118b508ccd97bb667d702002f8fa1640dce89"
)
BASELINE_SELECTED_HASH = "32a12ad4"
EXPECTED_RUNTIME = ("float asin failures=0",)


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
    SourceControl("baseline", "FA230OK", expect_exact=True),
    SourceControl(
        "variadic-sqrt",
        "FA230VA",
        ("FASIN_VARIADIC_SQRT",),
        expected_reject="call-flow",
    ),
    SourceControl(
        "volatile-x",
        "FA230VX",
        ("FASIN_VOLATILE_X",),
        expected_reject="opcodes",
    ),
    SourceControl(
        "volatile-sign",
        "FA230VS",
        ("FASIN_VOLATILE_SIGN",),
        expected_reject="loads",
    ),
    SourceControl(
        "different-recursion",
        "FA230DR",
        ("FASIN_DIFFERENT_RECURSE",),
        expected_reject="call-symbols",
    ),
)
MUTATION_CASES = (
    MutationCase("parameter-type", "1:type:2", "parameter"),
    MutationCase("one-constant-type", "2:type:2", "types"),
    MutationCase("sign-store-width", "3:memory_size:2", "stores"),
    MutationCase("negative-zero-type", "5:type:2", "types"),
    MutationCase("negative-comparison-type", "6:type:5", "types"),
    MutationCase("negative-left-source", "6:src1:3", "flow"),
    MutationCase("negative-branch-source", "7:src1:3", "flow"),
    MutationCase("negative-unary-source", "9:src1:3", "flow"),
    MutationCase("negative-store-source", "11:src1:3", "flow"),
    MutationCase("normalized-x-source", "13:src1:3", "flow"),
    MutationCase("x-store-width", "15:memory_size:2", "stores"),
    MutationCase("domain-load-type", "18:type:2", "loads"),
    MutationCase("domain-comparison-operator", "20:immediate:60", "constants"),
    MutationCase("domain-comparison-type", "20:type:5", "types"),
    MutationCase("domain-left-source", "20:src1:12", "flow"),
    MutationCase("domain-branch-source", "21:src1:11", "flow"),
    MutationCase("domain-return-source", "23:src1:12", "flow"),
    MutationCase("half-load-type", "38:type:2", "loads"),
    MutationCase("half-constant-type", "39:type:2", "types"),
    MutationCase("half-comparison-type", "40:type:5", "types"),
    MutationCase("half-left-source", "40:src1:25", "flow"),
    MutationCase("half-branch-source", "41:src1:24", "flow"),
    MutationCase("transform-x-source", "46:src2:35", "flow"),
    MutationCase("transform-result-type", "46:type:2", "types"),
    MutationCase("transform-product-left", "47:src1:35", "flow"),
    MutationCase("transform-product-type", "47:type:2", "types"),
    MutationCase("sqrt-argument-type", "48:type:2", "call-flow"),
    MutationCase("sqrt-argument-source", "48:src1:37", "calls"),
    MutationCase("sqrt-call-indirect", "49:src1:24", "call-flow"),
    MutationCase("sqrt-call-result-type", "49:type:2", "call-flow"),
    MutationCase("sqrt-call-identity", "49:identity:120", "call-symbols"),
    MutationCase("transform-store-source", "51:src1:38", "call-flow"),
    MutationCase("transform-store-width", "51:memory_size:2", "stores"),
    MutationCase("self-argument-source", "54:src1:38", "calls"),
    MutationCase("self-argument-type", "54:type:2", "call-flow"),
    MutationCase("self-call-indirect", "55:src1:24", "call-flow"),
    MutationCase("self-call-result-type", "55:type:2", "call-flow"),
    MutationCase("self-call-identity", "55:identity:120", "call-symbols"),
    MutationCase("sub-result-store-width", "56:memory_size:2", "stores"),
    MutationCase("sign-load-identity", "57:identity:120", "loads"),
    MutationCase("recursive-multiply-source", "61:src2:39", "recursive-result"),
    MutationCase("recursive-multiply-type", "61:type:2", "types"),
    MutationCase("recursive-subtract-source", "62:src1:29", "recursive-result"),
    MutationCase("recursive-result-source", "63:src1:28", "recursive-result"),
    MutationCase("recursive-result-type", "63:type:2", "types"),
    MutationCase("recursive-return-source", "64:src1:32", "recursive-result"),
    MutationCase("x2-right-identity", "68:identity:121", "loads"),
    MutationCase("x2-result-type", "69:type:2", "types"),
    MutationCase("x2-store-source", "70:src1:44", "polynomial"),
    MutationCase("x2-store-width", "70:memory_size:2", "stores"),
    MutationCase("product-left-source", "74:src1:43", "polynomial"),
    MutationCase("product-result-type", "74:type:2", "types"),
    MutationCase("coefficient-type", "75:type:2", "types"),
    MutationCase("horner-result-type", "82:type:2", "types"),
    MutationCase("polynomial-store-source", "90:src1:63", "polynomial"),
    MutationCase("polynomial-store-width", "90:memory_size:2", "stores"),
    MutationCase("final-sign-identity", "91:identity:120", "loads"),
    MutationCase("final-result-type", "93:type:2", "types"),
    MutationCase("final-result-source", "93:src1:64", "polynomial"),
    MutationCase("final-return-source", "94:src1:64", "polynomial"),
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
            f"{context} did not select float asin exactly\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained float asin\n{report}"
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
            "float asin baseline selected hash changed\n" + report
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
        default="build/float-asin-wave230-audit",
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
        f"float asin Wave 230 mutations={len(mutation_rows)} "
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
