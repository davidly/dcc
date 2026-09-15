#!/usr/bin/env python3
"""Audit the retained allocator-bridge exact schedule semantically."""

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
FUNCTION = "allocator_bridge_kernel"
RENAMED_FUNCTION = "allocator_bridge_renamed"
SOURCE = "tests/mir-clobber/albridge.c"
TEMPLATE = "allocator-coalesce-schedule"
FORCED_CANDIDATE = "spilled-boolean-phi-branch"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=allocator-coalesce-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "1677aef387a62b148a677d09db0f89d3934f701ea45319424f45b1f6d5cff7fb"
)
BASELINE_SELECTED_HASH = "9064348e"
EXPECTED_RUNTIME = (
    "allocator bridge size=3006 value=90 samples=90,90,90 failures=0",
)


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
    expected_reject: str | None = None


SOURCE_CONTROLS = (
    SourceControl("baseline", "AB240OK", expect_exact=True),
    SourceControl(
        "renamed",
        "AB240RN",
        ("ALLOCBRIDGE_RENAMED",),
        RENAMED_FUNCTION,
        expect_exact=True,
    ),
    SourceControl(
        "volatile-first",
        "AB240VF",
        ("ALLOCBRIDGE_VOLATILE_FIRST",),
        expected_reject="bridge-memory",
    ),
    SourceControl(
        "volatile-middle",
        "AB240VM",
        ("ALLOCBRIDGE_VOLATILE_MIDDLE",),
        expected_reject="bridge-memory",
    ),
    SourceControl(
        "volatile-last",
        "AB240VL",
        ("ALLOCBRIDGE_VOLATILE_LAST",),
        expected_reject="bridge-memory",
    ),
    SourceControl(
        "volatile-merged",
        "AB240VG",
        ("ALLOCBRIDGE_VOLATILE_MERGED",),
        expected_reject="bridge-memory",
    ),
    SourceControl(
        "extra-cfg",
        "AB240CF",
        ("ALLOCBRIDGE_EXTRA_CFG",),
    ),
    SourceControl(
        "fastcall-allocate",
        "AB240FA",
        ("ALLOCBRIDGE_FAST_ALLOCATE",),
        expected_reject="bridge-abi",
    ),
    SourceControl(
        "fastcall-free",
        "AB240FR",
        ("ALLOCBRIDGE_FAST_FREE",),
        expected_reject="bridge-abi",
    ),
    SourceControl(
        "fastcall-failure",
        "AB240FF",
        ("ALLOCBRIDGE_FAST_FAILURE",),
        expected_reject="bridge-abi",
    ),
    SourceControl(
        "fastcall-fill",
        "AB240FL",
        ("ALLOCBRIDGE_FAST_FILL",),
        expected_reject="bridge-abi",
    ),
)

EXPECTED_TYPES = {
    1: 34, 2: 34, 3: 19, 4: 49, 5: 49, 6: 49,
    7: 34, 8: 34, 9: 19, 10: 49, 11: 49, 12: 49,
    13: 34, 14: 34, 15: 19, 16: 49, 17: 49, 18: 49,
    19: 49, 20: 2, 21: 2, 24: 0, 27: 49, 28: 2,
    29: 2, 32: 0, 35: 0, 37: 0, 41: 0, 44: 0,
    47: 49, 48: 2, 49: 2, 52: 0, 55: 0, 57: 0,
    61: 0, 63: 17, 64: 17, 65: 3, 67: 49, 68: 19,
    69: 19, 70: 3, 71: 49, 72: 19, 73: 19, 74: 3,
    75: 49, 76: 19, 77: 19, 78: 3, 79: 34, 80: 34,
    81: 19, 82: 49, 83: 49, 84: 49, 85: 49, 86: 49,
    87: 2, 89: 17, 90: 17, 91: 3, 93: 49, 94: 49,
    95: 34, 96: 34, 97: 2, 98: 2, 99: 3, 100: 49,
    101: 19, 102: 19, 103: 3,
}
MEMORY_WIDTHS = {
    6: 2, 12: 2, 18: 2, 19: 0, 27: 0, 47: 0, 67: 0,
    71: 0, 75: 0, 84: 2, 85: 0, 86: 0, 93: 0, 100: 0,
}
VALUE_RELATIONS = {
    2: ((1, 0),),
    5: ((1, 1),),
    6: ((1, 3),),
    8: ((1, 4),),
    11: ((1, 5),),
    12: ((1, 7),),
    14: ((1, 8),),
    17: ((1, 9),),
    18: ((1, 11),),
    21: ((1, 12), (2, 13)),
    22: ((1, 14),),
    29: ((1, 16), (2, 17)),
    30: ((1, 18),),
    37: ((1, 19), (2, 20)),
    41: ((1, 15), (2, 21)),
    42: ((1, 22),),
    49: ((1, 24), (2, 25)),
    50: ((1, 26),),
    57: ((1, 27), (2, 28)),
    61: ((1, 23), (2, 29)),
    62: ((1, 30),),
    64: ((1, 31),),
    69: ((1, 33),),
    73: ((1, 36),),
    77: ((1, 39),),
    80: ((1, 42),),
    83: ((1, 43),),
    84: ((1, 45),),
    87: ((1, 46), (2, 47)),
    88: ((1, 48),),
    90: ((1, 49),),
    94: ((1, 51),),
    96: ((1, 52),),
    98: ((1, 53),),
    102: ((1, 55),),
}
LOCAL_IDENTITIES = (6, 12, 18, 19, 27, 47, 67, 71, 75, 84, 85, 86, 93, 100)
CALL_IDENTITIES = (3, 9, 15, 65, 70, 74, 78, 81, 91, 99, 103)
OPERATORS = {5: 0, 11: 0, 17: 0, 21: 276, 29: 276, 49: 276, 83: 0, 87: 277}
ARGUMENT_INDICES = {
    2: 0, 8: 0, 14: 0, 64: 0, 69: 0, 73: 0, 77: 0,
    80: 0, 90: 0, 94: 0, 96: 1, 98: 2, 102: 0,
}


def mutation_cases():
    cases = []
    for instruction, expected in EXPECTED_TYPES.items():
        replacement = 34 if expected == 2 else 2
        cases.append(MutationCase(
            f"type-{instruction}",
            f"{instruction}:type:{replacement}",
            "bridge-types",
        ))
    for instruction, expected in MEMORY_WIDTHS.items():
        replacement = 2 if expected == 0 else 1
        cases.append(MutationCase(
            f"width-{instruction}",
            f"{instruction}:memory_size:{replacement}",
            "bridge-memory",
        ))
    for instruction, relations in VALUE_RELATIONS.items():
        for operand, expected in relations:
            replacement = 4 if expected == 0 else 0
            cases.append(MutationCase(
                f"src{operand}-{instruction}",
                f"{instruction}:src{operand}:{replacement}",
            ))
    for instruction in LOCAL_IDENTITIES:
        cases.append(MutationCase(
            f"local-identity-{instruction}",
            f"{instruction}:identity:120",
        ))
    for instruction in CALL_IDENTITIES:
        cases.append(MutationCase(
            f"call-identity-{instruction}",
            f"{instruction}:identity:120",
        ))
    for instruction, expected in OPERATORS.items():
        replacement = 43 if expected != 43 else 45
        cases.append(MutationCase(
            f"operator-{instruction}",
            f"{instruction}:immediate:{replacement}",
        ))
    for instruction, expected in ARGUMENT_INDICES.items():
        replacement = 1 if expected == 0 else 0
        cases.append(MutationCase(
            f"argument-index-{instruction}",
            f"{instruction}:immediate:{replacement}",
        ))
    return tuple(cases)


MUTATION_CASES = mutation_cases()
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=178)


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


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        report_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    if selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "allocator bridge selected hash changed\n" + report
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
    for text in EXPECTED_RUNTIME:
        if text not in runtime:
            raise RuntimeError(
                f"{control.name} runtime missing {text!r}\n{runtime}"
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
        default="build/allocator-bridge-wave240-audit",
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
        f"allocator bridge Wave 240 mutations={len(mutation_rows)} "
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
