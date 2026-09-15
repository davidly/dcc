#!/usr/bin/env python3
"""Audit the retained modular-product exact schedule semantically."""

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
FUNCTION = "fixture_modprod"
RENAMED_FUNCTION = "fixture_modprod_renamed"
SOURCE = "tests/mir-clobber/modprod.c"
TEMPLATE = "modular-product-schedule"
FORCED_CANDIDATE = "spilled-wide-binary-lhs"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=modular-product-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "329398a141cf6aa1b045f256eb26e2f15b4c0725794d733fc7f2f20adc524d4e"
)
BASELINE_SELECTED_HASH = "8e867beb"


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False
    expected_reject: str | None = None
    expected_runtime: str = (
        "modular product failures=0 values=1,42701,49197"
    )


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_reject: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "MP520OK", expect_exact=True),
    SourceControl(
        "renamed", "MP520RN", ("MODPROD_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "signed-input", "MP520SI", ("MODPROD_SIGNED_INPUT",),
        expected_reject="parameter-types",
        expected_runtime="modular product failures=0 values=3,89,31",
    ),
    SourceControl(
        "narrow-input", "MP520NI", ("MODPROD_NARROW_INPUT",),
        expected_reject="parameter-types",
        expected_runtime="modular product failures=0 values=1,12,89",
    ),
    SourceControl(
        "signed-return", "MP520SR", ("MODPROD_SIGNED_RETURN",),
        expected_reject="preflight",
    ),
    SourceControl(
        "signed-arithmetic", "MP520SA", ("MODPROD_SIGNED_ARITH",),
        expected_reject="preflight",
        expected_runtime="modular product failures=0 values=1,3544,31",
    ),
    SourceControl(
        "volatile-input", "MP520VI", ("MODPROD_VOLATILE_INPUT",),
        expected_reject="shape",
    ),
    SourceControl(
        "extra-cfg", "MP520CF", ("MODPROD_EXTRA_CFG",),
        expected_reject="preflight",
    ),
    SourceControl(
        "local-state", "MP520LS", ("MODPROD_LOCAL_STATE",),
        expected_reject="preflight",
    ),
)


def mutation_cases():
    cases = []
    for instruction in (1, 2, 3):
        cases.append(MutationCase(
            f"parameter-type-{instruction}",
            f"{instruction}:type:2",
            "parameter-types",
        ))
    for instruction in (5, 7, 8, 10, 11):
        cases.append(MutationCase(
            f"arithmetic-type-{instruction}",
            f"{instruction}:type:4",
            "arithmetic-types",
        ))
    for instruction in (1, 2, 3, 5, 7, 8, 10, 11):
        cases.append(MutationCase(
            f"width-{instruction}",
            f"{instruction}:memory_size:2",
            "widths",
        ))
    cases.append(MutationCase(
        "return-width", "12:memory_size:2", "flow"
    ))
    for instruction in (5, 7, 10):
        cases.append(MutationCase(
            f"unary-operator-{instruction}",
            f"{instruction}:immediate:45",
            "operators",
        ))
    cases.extend((
        MutationCase("multiply-operator", "8:immediate:43", "operators"),
        MutationCase("modulo-operator", "11:immediate:47", "operators"),
    ))
    for name, instruction, field, replacement in (
        ("left-conversion-source", 5, "src1", 1),
        ("right-conversion-source", 7, "src1", 0),
        ("multiply-left-source", 8, "src1", 0),
        ("multiply-right-source", 8, "src2", 0),
        ("modulus-conversion-source", 10, "src1", 0),
        ("modulo-left-source", 11, "src1", 0),
        ("modulo-right-source", 11, "src2", 0),
        ("return-source", 12, "src1", 0),
    ):
        cases.append(MutationCase(
            name,
            f"{instruction}:{field}:{replacement}",
            "flow",
        ))
    for instruction in (1, 2, 3):
        cases.append(MutationCase(
            f"parameter-identity-{instruction}",
            f"{instruction}:identity:120",
            "parameters",
        ))
    return tuple(cases)


MUTATION_CASES = mutation_cases()
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=33)


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
            "modular product selected hash changed\n" + report
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
    output = output_dir / "forced-spilled.MAC"
    report = run(compiler_command(compiler, output), environment)
    expected = (
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg "
        "emitted=1 selectable=1 selected=1"
    )
    if expected not in report:
        raise RuntimeError(
            "forced spilled cost candidate was not selected\n" + report
        )
    if selection_from(report, FUNCTION) != "spilled-scalar-cfg":
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
            f"{control.name} runtime missing "
            f"{control.expected_runtime!r}\n{runtime}"
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
    require_generic(
        ordinary_report, FUNCTION, case.name, case.expected_reject
    )

    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    require_generic(
        forced_report, FUNCTION, f"{case.name} forced",
        case.expected_reject,
    )
    forced_cost = (
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg "
        "emitted=1 selectable=1 selected=1"
    )
    if forced_cost not in forced_report:
        raise RuntimeError(
            f"{case.name} did not select the forced cost candidate\n"
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
        reject_reason_from(ordinary_report, FUNCTION) or "",
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
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/modular-product-wave520-audit",
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
        f"modular product Wave 520 mutations={len(mutation_rows)} "
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
