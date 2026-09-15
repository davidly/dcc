#!/usr/bin/env python3
"""Audit the retained gnarly-call-runner exact schedule semantically."""

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
SOURCE = "tests/mir-clobber/gnarly.c"
TEMPLATE = "gnarly-call-runner"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=main "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=main "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
GENERIC_SELECTION = (
    "MIR selection function=main "
    "selector=spilled-scalar-cfg result=mir"
)
GENERIC_COST = (
    "MIR cost-selected function=main "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=main "
    r"template=gnarly-call-runner reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=main .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "dc95b353f36d0e1245a68d34b22f3ed7fafdff6bdba02a991fbd1fecb22cbfbb"
)
BASELINE_SELECTED_HASH = "a855a26c"
EXPECTED_RUNTIME = (
    "hello, world!",
    "duff: 1 5",
    "Point: 15, 20",
    "sz: 2, count: 10",
    "implicit test: 12",
    "x: 5",
    "z: 50",
    "x_after_xplusplus: 21",
    "c: #",
    "x: 30",
    "my_func",
    "str: Hello World C89",
    "mm1: 0 0 1",
    "bitops: 2 7 5",
    "idx: 40 40",
    "comma idx: 40",
    "assign idx: 3 99",
    "adj: A",
    "sizeof: 2 5",
    "charconst: 65 2",
    "gnarly oracle failures=0",
    "oldsum: 15",
    "arr sizes: 10 2",
    "struct assign: 42 x",
    "cond promo: 1",
    "ptr-to-array: 50",
    "strvar: l",
    "strderef: h",
    "revstr: h",
    "chain: 5 5 5",
    "for comma: 45",
    "sizeofstr: 6",
    "nested ternary: -1",
    "hex oct: 255 127 65 65",
    "unary plus: 65",
    "bitnot: -16",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=22)


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
    SourceControl("baseline", "GN100OK", expect_exact=True),
    SourceControl(
        "volatile-count",
        "GN100VC",
        ("GNARLY_VOLATILE_COUNT",),
        expected_reject="shape",
    ),
)
MUTATION_CASES = (
    MutationCase("constant-value", "1:immediate:11", "constants"),
    MutationCase("binary-operator", "16:immediate:43", "operations"),
    MutationCase("binary-word-type", "16:type:1", "operations"),
    MutationCase("pointer-binary-type", "235:type:2", "operations"),
    MutationCase("phi-source", "13:src1:20", "control-flow"),
    MutationCase("phi-word-type", "13:type:1", "control-flow"),
    MutationCase("conversion-type", "79:type:2", "conversions"),
    MutationCase("string-type", "342:type:2", "strings"),
    MutationCase("duplicate-string", "363:immediate:7", "distinct-strings"),
    MutationCase("reused-string", "167:immediate:5", "reused-strings"),
    MutationCase("print-call-kind", "74:src1:49", "print-calls"),
    MutationCase("direct-function", "6:identity:120", "direct-functions"),
    MutationCase("direct-argument", "56:src1:37", "direct-arguments"),
    MutationCase("indirect-call-type", "173:type:2", "indirect-calls"),
    MutationCase("duff-array-type", "18:type:2", "duff-arrays"),
    MutationCase("main-array-identity", "141:identity:120", "main-array"),
    MutationCase("main-array-type", "136:type:2", "distinct-arrays"),
    MutationCase("array-index-type", "138:type:2", "array-indices"),
    MutationCase("array-index-width", "138:memory_size:1", "array-indices"),
    MutationCase("member-word-type", "332:type:17", "structure-copy"),
    MutationCase("member-byte-type", "336:type:18", "structure-copy"),
    MutationCase("object-identity", "2:identity:120", "objects-return"),
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
            f"{context} did not select the gnarly schedule\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained the gnarly schedule\n{report}"
        )
    if GENERIC_SELECTION not in report or GENERIC_COST not in report:
        raise RuntimeError(
            f"{context} did not select {FORCED_CANDIDATE}\n{report}"
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
            "gnarly baseline selected hash changed\n" + report
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
    if runtime.count("my_func") != 2:
        raise RuntimeError(
            f"{control.name} runtime did not call my_func twice\n{runtime}"
        )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        outcome,
        "scheduled-machine-cfg"
        if control.expect_exact else "spilled-scalar-cfg",
        selected_hash(build_output) or "",
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
        default="build/gnarly-runner-wave100-audit",
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
        f"gnarly runner Wave 100 mutations={len(mutation_rows)} "
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
