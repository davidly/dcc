#!/usr/bin/env python3
"""Audit the retained indexed-word-sum exact schedule semantically."""

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
FUNCTION = "indexed_word_sum"
SOURCE = "tests/mir-clobber/idxwsum.c"
TEMPLATE = "indexed-word-sum-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=indexed_word_sum "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=indexed_word_sum "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
FORCED_GENERIC_COST = (
    "MIR cost-selected function=indexed_word_sum "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=indexed_word_sum "
    r"template=indexed-word-sum-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=indexed_word_sum .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "a68e9c6e4b124bc3e24adf826e8b8717693927d84185d82546251c3d60c623ce"
)
BASELINE_SELECTED_HASH = "e3d216d0"
EXPECTED_RUNTIME = (
    "indexed word sum failures=0 values=1145,-32764",
)


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
    SourceControl("baseline", "IW200OK", expect_exact=True),
    SourceControl(
        "volatile-values", "IW200VV", ("IDXWSUM_VOLATILE_VALUES",),
        expected_reject="result",
    ),
    SourceControl(
        "bitfield-value", "IW200BF", ("IDXWSUM_BITFIELD_VALUE",),
        expected_reject="result",
    ),
    SourceControl(
        "long-result", "IW200LR", ("IDXWSUM_LONG_RESULT",),
        expected_reject="preflight",
    ),
    SourceControl(
        "extra-parameter", "IW200EP", ("IDXWSUM_EXTRA_PARAMETER",),
        expected_reject="opcode",
    ),
    SourceControl(
        "vla-state", "IW200VL", ("IDXWSUM_VLA_STATE",),
        expected_reject="preflight",
    ),
    SourceControl(
        "local-state", "IW200LS", ("IDXWSUM_LOCAL_STATE",),
        expected_reject="preflight",
    ),
    SourceControl(
        "branch", "IW200BR", ("IDXWSUM_BRANCH",),
        expected_reject="preflight",
    ),
)
MUTATION_CASES = (
    MutationCase("parameter-pointer-kind", "1:type:18", "index-types"),
    MutationCase("parameter-nonpointer", "1:type:2", "result"),
    MutationCase("parameter-identity", "1:identity:120", "result"),
    MutationCase("left-constant-long", "3:type:4", "index-constants"),
    MutationCase("left-constant-byte", "3:type:33", "index-constants"),
    MutationCase("left-constant-negative", "3:immediate:-1", "result"),
    MutationCase("left-constant-wide", "3:immediate:32768", "result"),
    MutationCase("left-index-type", "4:type:18", "index-types"),
    MutationCase("left-index-width", "4:memory_size:2", "index-types"),
    MutationCase("left-index-stride-metadata", "4:immediate:4", "index-types"),
    MutationCase("left-index-zero-stride", "4:immediate:0", "result"),
    MutationCase("left-index-base", "4:src1:2", "result"),
    MutationCase("left-index-subscript", "4:src2:0", "result"),
    MutationCase("left-member-type", "5:type:2", "word-types"),
    MutationCase("left-member-pointer-kind", "5:type:50", "word-types"),
    MutationCase("left-member-width", "5:memory_size:1", "word-types"),
    MutationCase("left-member-base", "5:src1:2", "result"),
    MutationCase("left-load-long", "6:type:4", "word-types"),
    MutationCase("left-load-byte", "6:type:33", "word-types"),
    MutationCase("left-load-unsigned", "6:type:34", "word-types"),
    MutationCase("left-load-width", "6:memory_size:1", "result"),
    MutationCase("left-load-address", "6:src1:2", "result"),
    MutationCase("right-constant-long", "8:type:4", "index-constants"),
    MutationCase("right-constant-byte", "8:type:33", "index-constants"),
    MutationCase("right-constant-negative", "8:immediate:-1", "result"),
    MutationCase("right-constant-wide", "8:immediate:32768", "result"),
    MutationCase("right-index-type", "9:type:18", "index-types"),
    MutationCase("right-index-width", "9:memory_size:2", "index-types"),
    MutationCase("right-index-stride-metadata", "9:immediate:4", "index-types"),
    MutationCase("right-index-zero-stride", "9:immediate:0", "result"),
    MutationCase("right-index-base", "9:src1:7", "result"),
    MutationCase("right-index-subscript", "9:src2:0", "result"),
    MutationCase("right-member-type", "10:type:2", "word-types"),
    MutationCase("right-member-pointer-kind", "10:type:50", "word-types"),
    MutationCase("right-member-width", "10:memory_size:1", "word-types"),
    MutationCase("right-member-base", "10:src1:7", "result"),
    MutationCase("right-load-long", "11:type:4", "word-types"),
    MutationCase("right-load-byte", "11:type:33", "word-types"),
    MutationCase("right-load-unsigned", "11:type:34", "word-types"),
    MutationCase("right-load-width", "11:memory_size:1", "result"),
    MutationCase("right-load-address", "11:src1:7", "result"),
    MutationCase("add-long-result", "12:type:4", "result"),
    MutationCase("add-unsigned-result", "12:type:34", "result"),
    MutationCase("add-operator", "12:immediate:45", "result"),
    MutationCase("add-left-operand", "12:src1:0", "result"),
    MutationCase("add-right-operand", "12:src2:0", "result"),
    MutationCase("return-source", "13:src1:5", "result"),
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
        DCC_MIR_SELECT_REPORT_FUNCTION=FUNCTION,
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
            f"{context} did not select the indexed-word-sum schedule\n{report}"
        )
    if reject_reason(report) is not None:
        raise RuntimeError(
            f"{context} reported an unexpected rejection\n{report}"
        )


def require_generic(report, context, expected_reject, forced=False):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained the exact schedule\n{report}"
        )
    selection = re.search(
        r"MIR selection function=indexed_word_sum "
        r"selector=(?P<selector>\S+) result=mir",
        report,
    )
    if selection is None or selection.group("selector") == "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} did not select a generic fallback\n{report}"
        )
    if forced and FORCED_GENERIC_COST not in report:
        raise RuntimeError(
            f"{context} did not select {FORCED_CANDIDATE}\n{report}"
        )
    reason = reject_reason(report)
    if reason != expected_reject:
        raise RuntimeError(
            f"{context} rejected as {reason!r}, expected "
            f"{expected_reject!r}\n{report}"
        )
    return selection.group("selector")


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, "baseline")
    if selected_hash(report) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            f"baseline selected hash changed\n{report}"
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
    mode = (
        f"{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    build_dir = output_dir / "runtime" / control.name / mode
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
    if control.expect_exact:
        require_exact(report, f"{control.name} {mode}")
        selection = "exact"
    else:
        selection = require_generic(
            report, f"{control.name} {mode}", control.expected_reject
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
            f"{control.name} {mode} runtime missing {missing!r}\n{runtime}"
        )
    return (
        control.name,
        ",".join(control.defines) or "-",
        "exact" if control.expect_exact else "generic",
        selection,
        f"stack={int(stack_check)} peep={int(peep)}",
        "passed",
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
    ordinary_selector = require_generic(
        ordinary_report, case.name, case.expected_reject
    )
    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(
        forced_report, f"{case.name} forced",
        case.expected_reject, forced=True,
    )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        case.spec,
        "rejected",
        case.expected_reject,
        ordinary_selector,
        FORCED_CANDIDATE,
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
        default="build/indexed-word-sum-wave220-audit",
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
        ("name", "defines", "expected", "selector", "mode", "runtime"),
        source_rows,
    )
    write_tsv(
        output_dir / "mutation-census.tsv",
        (
            "name", "mutation", "outcome", "reject",
            "ordinary-selector", "forced-candidate",
        ),
        mutation_rows,
    )

    outcomes = Counter(row[2] for row in mutation_rows)
    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"indexed word sum Wave 220 mutations={len(mutation_rows)} "
        f"{outcomes}"
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
