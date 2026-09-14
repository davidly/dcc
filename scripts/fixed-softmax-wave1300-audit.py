#!/usr/bin/env python3
"""Audit every mutable field used by the fixed-softmax exact matcher."""

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
FUNCTION = "fixture_fixed_softmax"
RENAMED_FUNCTION = "fixture_fixed_softmax_renamed"
SOURCE = "tests/mir-clobber/fixsmx.c"
TEMPLATE = "fixed-softmax-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=fixed-softmax-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "c9c86c97091c14451080c8a64ff0a0fcec70fe664049cd7a6dc0fc058568b954"
)
BASELINE_SELECTED_HASH = "cf58a4d7"
EXPECTED_RUNTIME = "fixed softmax failures=0"


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "FS13BASE", expect_exact=True),
    SourceControl(
        "renamed", "FS13RN", ("FIXSMX_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "renamed-clamp", "FS13RC", ("FIXSMX_RENAMED_CLAMP",),
        expect_exact=True,
    ),
    SourceControl(
        "prefix-increment", "FS13PI", ("FIXSMX_PREFIX_INCREMENT",),
        expect_exact=True,
    ),
    SourceControl(
        "long-table", "FS13LT", ("FIXSMX_LONG_TABLE",),
        expect_exact=True,
    ),
    SourceControl(
        "volatile-vector", "FS13VV", ("FIXSMX_VOLATILE_VECTOR",),
        expected_reject="volatile-memory",
    ),
    SourceControl(
        "volatile-table", "FS13VT", ("FIXSMX_VOLATILE_TABLE",),
        expected_reject="volatile-memory",
    ),
    SourceControl(
        "volatile-sum", "FS13VS", ("FIXSMX_VOLATILE_SUM",),
        expected_reject="opcodes",
    ),
    SourceControl(
        "unsigned-count", "FS13UC", ("FIXSMX_UNSIGNED_COUNT",),
    ),
    SourceControl(
        "indirect-clamp", "FS13IC", ("FIXSMX_INDIRECT_CLAMP",),
    ),
    SourceControl(
        "variadic-clamp", "FS13VC", ("FIXSMX_VARIADIC_CLAMP",),
        expected_reject="normalization",
    ),
    SourceControl(
        "unsigned-weight", "FS13UW", ("FIXSMX_UNSIGNED_WEIGHT",),
    ),
    SourceControl(
        "scale-128", "FS13S8", ("FIXSMX_SCALE_128",),
        expected_reject="normalization",
    ),
    SourceControl(
        "clamp-127", "FS13C7", ("FIXSMX_CLAMP_127",),
        expected_reject="exponential-index",
    ),
    SourceControl(
        "nonvoid", "FS13NV", ("FIXSMX_NONVOID_RETURN",),
    ),
    SourceControl(
        "short-table", "FS13ST", ("FIXSMX_SHORT_TABLE",),
        expected_reject="table-and-sum",
    ),
    SourceControl(
        "subtract-sum", "FS13SS", ("FIXSMX_SUBTRACT",),
        expected_reject="table-and-sum",
    ),
    SourceControl(
        "extra-cfg", "FS13CF", ("FIXSMX_EXTRA_CFG",),
    ),
)

TYPE_INSTRUCTIONS = (
    1, 2, 3, 5, 9, 12, 14, 16, 17, 18, 20, 23, 24, 25,
    27, 30, 36, 37, 41, 45, 47, 51, 53, 54, 56, 57, 58, 60,
    61, 62, 63, 67, 68, 70, 74, 75, 76, 80, 81, 83, 87, 88,
    89, 91, 94, 95, 96, 102, 103, 105, 106, 107, 112, 114,
    120, 125, 126, 127, 129, 130, 131, 133, 134, 135, 136,
    138, 139, 140, 141, 145, 146, 148, 149, 150,
)
WIDTH_INSTRUCTIONS = (
    5, 7, 10, 23, 30, 32, 38, 43, 46, 49, 62, 65, 72, 78,
    85, 91, 92, 95, 98, 104, 108, 113, 116, 131, 132, 140,
    141, 142, 147, 151,
)
IMMEDIATE_INSTRUCTIONS = (
    1, 3, 9, 16, 17, 18, 25, 36, 37, 41, 45, 56, 57, 58,
    63, 67, 68, 70, 75, 76, 80, 81, 83, 88, 96, 102, 103,
    106, 107, 112, 125, 126, 127, 134, 135, 136, 138, 139,
    140, 141, 145, 146, 149, 150,
)
SRC1_INSTRUCTIONS = (
    5, 7, 10, 14, 17, 18, 19, 23, 25, 26, 30, 32, 37, 38,
    43, 46, 49, 53, 54, 57, 58, 59, 62, 63, 65, 68, 69, 72,
    76, 78, 81, 82, 85, 91, 92, 95, 96, 98, 103, 104, 107,
    108, 113, 116, 120, 126, 127, 128, 131, 132, 134, 136,
    138, 139, 140, 141, 142, 146, 147, 150, 151,
)
SRC2_INSTRUCTIONS = (
    14, 18, 25, 37, 53, 54, 58, 63, 68, 76, 81, 92, 96,
    103, 107, 120, 127, 136, 139, 140, 141, 142, 146, 150,
)
IDENTITY_INSTRUCTIONS = (
    1, 2, 7, 10, 12, 14, 20, 24, 27, 32, 38, 43, 46, 47,
    49, 51, 53, 54, 60, 61, 65, 72, 74, 78, 85, 87, 88, 89,
    94, 98, 104, 105, 108, 113, 114, 116, 120, 129, 130,
    132, 133, 141, 147, 148, 151,
)


def mutation_cases():
    groups = (
        ("type", TYPE_INSTRUCTIONS, 1),
        ("memory_size", WIDTH_INSTRUCTIONS, 4),
        ("immediate", IMMEDIATE_INSTRUCTIONS, 4),
        ("src1", SRC1_INSTRUCTIONS, 0),
        ("src2", SRC2_INSTRUCTIONS, 0),
        ("identity", IDENTITY_INSTRUCTIONS, 88),
    )
    return tuple(
        MutationCase(
            f"{field}-{instruction}",
            f"{instruction}:{field}:{value}",
        )
        for field, instructions, value in groups
        for instruction in instructions
    )


MUTATION_CASES = mutation_cases()
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=278)


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
    for define in defines:
        command.append(f"-D{define}")
    command.extend([SOURCE, "-o", str(output)])
    return command


def report_environment(function=FUNCTION):
    environment = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
    ):
        environment.pop(name, None)
    environment.update(
        DCC_MIR_MACHINE_FUNCTION=function,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_FUNCTION=function,
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_SELECT_REPORT_FUNCTION=function,
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
    return environment


def selection_from(report, function):
    matches = [
        match for match in SELECTION.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("selector") if matches else None


def reject_reason_from(report, function):
    matches = [
        match for match in REJECT_REASON.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("reason") if matches else None


def selected_hash(report, function):
    matches = [
        match for match in SELECTED_HASH.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("hash") if matches else None


def require_exact(report, function, context):
    if selection_from(report, function) != "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} did not retain the exact schedule\n{report}"
        )
    expected = (
        f"MIR cost-selected function={function} "
        "candidate=exact-scheduled selector=scheduled-machine-cfg"
    )
    if expected not in report:
        raise RuntimeError(
            f"{context} did not select the exact cost candidate\n{report}"
        )
    if reject_reason_from(report, function) is not None:
        raise RuntimeError(
            f"{context} reported an unexpected rejection\n{report}"
        )


def require_generic(report, function, context, expected_reject=None):
    selector = selection_from(report, function)
    if selector == "scheduled-machine-cfg" or selector is None:
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )
    reject = reject_reason_from(report, function)
    if expected_reject is not None and reject != expected_reject:
        raise RuntimeError(
            f"{context} reject {reject!r}, "
            f"expected {expected_reject!r}\n{report}"
        )
    return reject


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        report_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    if selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "fixed softmax selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def forced_fallback_control(compiler, output_dir):
    environment = report_environment()
    environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    report = run(
        compiler_command(
            compiler, output_dir / "forced-spilled.MAC"
        ),
        environment,
    )
    expected = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if expected not in report or \
            selection_from(report, FUNCTION) != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n" + report
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
    report = run(
        command, report_environment(control.function), timeout=300
    )
    if control.expect_exact:
        require_exact(report, control.function, control.name)
        outcome = "exact"
    else:
        require_generic(
            report, control.function, control.name,
            control.expected_reject,
        )
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=30,
    )
    if EXPECTED_RUNTIME not in runtime:
        raise RuntimeError(
            f"{control.name} runtime failed\n{runtime}"
        )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        outcome,
        selection_from(report, control.function) or "",
        reject_reason_from(report, control.function) or "",
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
    environment = report_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), environment
    )
    reject = require_generic(
        ordinary_report, FUNCTION, case.name
    )
    if reject is None:
        raise RuntimeError(
            f"{case.name} had no fixed-softmax rejection\n"
            f"{ordinary_report}"
        )
    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    forced_reject = require_generic(
        forced_report, FUNCTION, f"{case.name} forced"
    )
    if forced_reject != reject:
        raise RuntimeError(
            f"{case.name} rejection changed under forced fallback: "
            f"{reject!r} != {forced_reject!r}\n{forced_report}"
        )
    forced_cost = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if forced_cost not in forced_report:
        raise RuntimeError(
            f"{case.name} did not select forced generic fallback\n"
            f"{forced_report}"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        selection_from(ordinary_report, FUNCTION) or "",
        reject,
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
                "selector", "reject_reason", "mode",
            )
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default="build/fixed-softmax-wave1300-audit",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    jobs = min(args.jobs, 2)

    compiler = compiler_path()
    dccmake = dccmake_path()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True)

    baseline_digest = baseline_compile(compiler, output_dir)
    forced_fallback_control(compiler, output_dir)
    source_rows = run_source_controls(
        compiler, dccmake, output_dir
    )
    mutation_rows = run_mutation_cases(
        compiler, output_dir, jobs
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
        f"fixed softmax Wave 1300 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"meaningful survivors=0")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
