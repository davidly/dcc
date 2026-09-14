#!/usr/bin/env python3
"""Audit the retained indexed-member-write exact schedule semantically."""

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
FUNCTION = "indexed_member_write"
RENAMED_FUNCTION = "indexed_member_write_renamed"
SOURCE = "tests/mir-clobber/idxmwrit.c"
TEMPLATE = "indexed-member-write"
FORCED_CANDIDATE = "spilled-store-address"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=indexed-member-write reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "32f652d93d598292d2dbef07f11cc0493d27c3f8be9d68f61fe73c2708a4a9d1"
)
BASELINE_SELECTED_HASH = "c37f081a"
EXPECTED_RUNTIME = (
    "indexed member write target=13398 guards=1002,2002 "
    "neighbors=-21,-23 state=26796 failures=0"
)
EXPECTED_CHAR_RUNTIME = (
    "indexed member write target=86 guards=1002,2002 "
    "neighbors=-21,-23 state=26796 failures=0"
)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False
    expected_reject: str | None = None
    expected_runtime: str = EXPECTED_RUNTIME


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_reject: str
    defines: tuple[str, ...] = ()


SOURCE_CONTROLS = (
    SourceControl("baseline", "IM260OK", expect_exact=True),
    SourceControl(
        "renamed", "IM260RN", ("IDXMW_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "adjusted-pointer", "IM260AD",
        ("IDXMW_ADJUSTED_POINTER",), expect_exact=True,
    ),
    SourceControl(
        "reversed-add", "IM260RA",
        ("IDXMW_REVERSED_ADD",), expect_exact=True,
    ),
    SourceControl(
        "volatile-root", "IM260VR", ("IDXMW_ROOT_VOLATILE",),
        expected_reject="load",
    ),
    SourceControl(
        "volatile-pointer-field", "IM260VP",
        ("IDXMW_POINTER_FIELD_VOLATILE",), expected_reject="member",
    ),
    SourceControl(
        "volatile-pointee", "IM260VE",
        ("IDXMW_POINTER_POINTEE_VOLATILE",), expected_reject="member",
    ),
    SourceControl(
        "volatile-index", "IM260VI", ("IDXMW_INDEX_VOLATILE",),
        expected_reject="member",
    ),
    SourceControl(
        "char-index", "IM260IC", ("IDXMW_INDEX_CHAR",),
        expected_reject="load-indirect",
    ),
    SourceControl(
        "volatile-value", "IM260VV", ("IDXMW_VALUE_VOLATILE",),
        expected_reject="member",
    ),
    SourceControl(
        "bitfield-value", "IM260VB", ("IDXMW_VALUE_BITFIELD",),
        expected_reject="member",
    ),
    SourceControl(
        "char-value", "IM260VC", ("IDXMW_VALUE_CHAR",),
        expected_reject="opcode", expected_runtime=EXPECTED_CHAR_RUNTIME,
    ),
    SourceControl(
        "long-value", "IM260VL", ("IDXMW_VALUE_LONG",),
        expected_reject="opcode",
    ),
    SourceControl(
        "char-parameter", "IM260PC", ("IDXMW_PARAMETER_CHAR",),
        expected_reject="opcode", expected_runtime=EXPECTED_CHAR_RUNTIME,
    ),
    SourceControl(
        "second-parameter", "IM260P2", ("IDXMW_SECOND_PARAMETER",),
        expected_reject="counts",
    ),
    SourceControl(
        "cfg-branch", "IM260CF", ("IDXMW_CFG_BRANCH",),
        expected_reject="preflight",
    ),
    SourceControl(
        "nonvoid", "IM260NV", ("IDXMW_NONVOID",),
        expected_reject="preflight",
    ),
)

MUTATION_CASES = (
    MutationCase("parameter-type-char", "1:type:1", "value"),
    MutationCase("parameter-type-long", "1:type:4", "value"),
    MutationCase("parameter-width", "1:memory_size:1", "parameter"),
    MutationCase("parameter-identity", "1:identity:120", "components"),
    MutationCase("first-root-type", "2:type:2", "components"),
    MutationCase("first-root-width", "2:memory_size:1", "load"),
    MutationCase("first-root-identity", "2:identity:120", "load"),
    MutationCase("pointer-member-type", "3:type:2", "pointer-field"),
    MutationCase("pointer-member-width", "3:memory_size:1", "pointer-field"),
    MutationCase("pointer-member-offset", "3:immediate:4", "pointer-field"),
    MutationCase("pointer-member-identity", "3:identity:120", "pointer-field"),
    MutationCase("pointer-member-source", "3:src1:6", "components"),
    MutationCase("pointer-load-type", "4:type:2", "pointer-field"),
    MutationCase("pointer-load-width", "4:memory_size:1", "load-indirect"),
    MutationCase("pointer-load-identity", "4:identity:120", "pointer-field"),
    MutationCase("pointer-load-source", "4:src1:5", "pointer-field"),
    MutationCase("second-root-type", "5:type:2", "components"),
    MutationCase("second-root-width", "5:memory_size:1", "load"),
    MutationCase("second-root-identity", "5:identity:120", "load"),
    MutationCase("index-member-type", "6:type:400", "index-field"),
    MutationCase("index-member-width", "6:memory_size:1", "index-field"),
    MutationCase("index-member-offset", "6:immediate:2", "index-field"),
    MutationCase("index-member-identity", "6:identity:120", "pointer-field"),
    MutationCase("index-member-source", "6:src1:3", "components"),
    MutationCase("index-load-type", "7:type:1", "index-field"),
    MutationCase("index-load-wide-type", "7:type:4", "index-field"),
    MutationCase("index-load-width", "7:memory_size:1", "load-indirect"),
    MutationCase("index-load-identity", "7:identity:120", "index-field"),
    MutationCase("index-load-source", "7:src1:2", "pointer-field"),
    MutationCase("stride-constant-type", "8:type:1", "arithmetic"),
    MutationCase("stride-mismatch", "8:immediate:5", "arithmetic"),
    MutationCase("stride-zero", "8:immediate:0", "components"),
    MutationCase("stride-overflow", "8:immediate:32768", "components"),
    MutationCase("scale-type", "9:type:400", "arithmetic"),
    MutationCase("scale-operator", "9:immediate:43", "scale"),
    MutationCase("scale-index-source", "9:src1:3", "pointer-field"),
    MutationCase("scale-constant-source", "9:src2:6", "scale"),
    MutationCase("addition-type", "10:type:2", "arithmetic"),
    MutationCase("addition-operator", "10:immediate:45", "adjustment"),
    MutationCase("addition-pointer-source", "10:src1:6", "pointer-field"),
    MutationCase("addition-scale-source", "10:src2:3", "scale"),
    MutationCase("local-store-width", "12:memory_size:1", "store"),
    MutationCase("local-store-source", "12:src1:3", "addition"),
    MutationCase("local-store-identity", "12:identity:120", "store"),
    MutationCase("local-load-type", "13:type:2", "local"),
    MutationCase("local-load-width", "13:memory_size:1", "load"),
    MutationCase("local-load-identity", "13:identity:120", "load"),
    MutationCase("element-member-type", "14:type:17", "element-field"),
    MutationCase("element-member-width", "14:memory_size:1", "element-field"),
    MutationCase("element-member-offset", "14:immediate:4", "element-field"),
    MutationCase("element-member-identity", "14:identity:120", "element-field"),
    MutationCase("element-member-source", "14:src1:9", "destination"),
    MutationCase("element-store-type", "16:type:1", "element-field"),
    MutationCase("element-store-width", "16:memory_size:1", "store-indirect"),
    MutationCase("element-store-identity", "16:identity:120", "element-field"),
    MutationCase("element-store-address", "16:src1:9", "destination"),
    MutationCase("element-store-value", "16:src2:6", "components"),
    MutationCase(
        "adjustment-unit-type", "11:type:1", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-stride-type", "12:type:1", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-stride-value", "12:immediate:5", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-product-type", "13:type:400", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-product-operator", "13:immediate:43", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-product-left", "13:src1:6", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "adjustment-product-right", "13:src2:10", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "subtraction-type", "14:type:2", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "subtraction-operator", "14:immediate:43", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "subtraction-left", "14:src1:8", "addition",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
    MutationCase(
        "subtraction-right", "14:src2:7", "adjustment",
        ("IDXMW_ADJUSTED_POINTER",),
    ),
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


def require_generic(report, function, context, expected_reject):
    selector = selection_from(report, function)
    if selector == "scheduled-machine-cfg" or selector is None:
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )
    reject = reject_reason_from(report, function)
    if reject != expected_reject:
        raise RuntimeError(
            f"{context} reject {reject!r}, "
            f"expected {expected_reject!r}\n{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        report_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    if selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "indexed member write selected hash changed\n" + report
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
    if control.expected_runtime not in runtime:
        raise RuntimeError(
            f"{control.name} runtime oracle failed\n{runtime}"
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
        compiler_command(compiler, ordinary_path, case.defines),
        environment,
    )
    require_generic(
        ordinary_report, FUNCTION, case.name, case.expected_reject
    )
    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path, case.defines),
        forced_environment,
    )
    require_generic(
        forced_report, FUNCTION, f"{case.name} forced",
        case.expected_reject,
    )
    forced_cost = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if forced_cost not in forced_report:
        raise RuntimeError(
            f"{case.name} did not select the forced fallback\n"
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
        case.expected_reject,
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
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/indexed-member-write-wave260-audit",
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
    forced_fallback_control(compiler, output_dir)
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
        f"indexed member write Wave 260 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
