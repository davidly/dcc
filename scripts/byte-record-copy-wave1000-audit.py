#!/usr/bin/env python3
"""Audit the retained byte-record-copy exact schedule semantically."""

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
FUNCTION = "byte_record_copy"
RENAMED_FUNCTION = "byte_record_copy_renamed"
SOURCE = "tests/mir-clobber/brecopy.c"
TEMPLATE = "byte-record-copy-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=byte-record-copy-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "e9d0990b8b4f72d1ba4962d98b8f6cd91cf8d602eba4b60b0031b8e0cd302114"
)
BASELINE_SELECTED_HASH = "53bdf7fb"
EXPECTED_RUNTIME = (
    "byte record copy failures=0 "
    "values=254,238,222,110,190,174,158,142",
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
    expected_reject: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "BR1KOK", expect_exact=True),
    SourceControl(
        "renamed", "BR1KRN", ("BRC_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "volatile-destination", "BR1KVD",
        ("BRC_VOLATILE_DESTINATION",), expected_reject="parameters",
    ),
    SourceControl(
        "volatile-source", "BR1KVS",
        ("BRC_VOLATILE_SOURCE",), expected_reject="parameters",
    ),
    SourceControl(
        "different-record-types", "BR1KDT",
        ("BRC_DIFFERENT_TYPES",), expected_reject="parameters",
    ),
    SourceControl(
        "reversed-parameters", "BR1KRP",
        ("BRC_REVERSED_PARAMETERS",), expected_reject="fields",
    ),
    SourceControl("extra-field", "BR1KEF", ("BRC_EXTRA_FIELD",)),
    SourceControl(
        "extra-parameter", "BR1KEP", ("BRC_EXTRA_PARAMETER",),
    ),
    SourceControl("local-state", "BR1KLS", ("BRC_LOCAL_STATE",)),
    SourceControl("vla-state", "BR1KVL", ("BRC_VLA_STATE",)),
    SourceControl("nonvoid", "BR1KNV", ("BRC_NONVOID",)),
) + tuple(
    SourceControl(
        f"volatile-field-{field}", f"BR1KV{field}",
        (f"BRC_VOLATILE_FIELD_{field}",), expected_reject="fields",
    )
    for field in range(8)
) + tuple(
    SourceControl(
        f"bitfield-{field}", f"BR1KB{field}",
        (f"BRC_BITFIELD_{field}",),
    )
    for field in range(8)
)


FIELD_INSTRUCTIONS = (
    (4, 3, 6, 5, 7, 8),
    (10, 8, 12, 10, 13, 14),
    (16, 13, 18, 15, 19, 20),
    (22, 18, 24, 20, 25, 26),
    (28, 23, 30, 25, 31, 32),
    (34, 28, 36, 30, 37, 38),
    (40, 33, 42, 35, 43, 44),
    (46, 38, 48, 40, 49, 50),
)


def mutation_cases():
    cases = [
        MutationCase("destination-type", "1:type:2", "parameters"),
        MutationCase("source-type", "2:type:2", "parameters"),
        MutationCase(
            "destination-identity", "1:identity:120", "parameters",
        ),
        MutationCase("source-identity", "2:identity:120", "parameters"),
    ]
    for field, (
        destination_member,
        destination_value,
        source_member,
        source_value,
        load,
        store,
    ) in enumerate(FIELD_INSTRUCTIONS):
        wrong_offset = (field + 1) % 8
        prefix = f"field-{field}"
        cases.extend(
            (
                MutationCase(
                    f"{prefix}-destination-member-signed-pointer",
                    f"{destination_member}:type:17", "field-types",
                ),
                MutationCase(
                    f"{prefix}-destination-member-nonpointer",
                    f"{destination_member}:type:2", "field-types",
                ),
                MutationCase(
                    f"{prefix}-destination-member-source",
                    f"{destination_member}:src1:1", "fields",
                ),
                MutationCase(
                    f"{prefix}-destination-member-offset",
                    f"{destination_member}:immediate:{wrong_offset}",
                    "fields",
                ),
                MutationCase(
                    f"{prefix}-destination-member-width",
                    f"{destination_member}:memory_size:2", "fields",
                ),
                MutationCase(
                    f"{prefix}-source-member-signed-pointer",
                    f"{source_member}:type:17", "field-types",
                ),
                MutationCase(
                    f"{prefix}-source-member-nonpointer",
                    f"{source_member}:type:2", "field-types",
                ),
                MutationCase(
                    f"{prefix}-source-member-source",
                    f"{source_member}:src1:0", "fields",
                ),
                MutationCase(
                    f"{prefix}-source-member-offset",
                    f"{source_member}:immediate:{wrong_offset}", "fields",
                ),
                MutationCase(
                    f"{prefix}-source-member-width",
                    f"{source_member}:memory_size:2", "fields",
                ),
                MutationCase(
                    f"{prefix}-load-signed-byte-type",
                    f"{load}:type:1", "field-types",
                ),
                MutationCase(
                    f"{prefix}-load-word-type",
                    f"{load}:type:2", "field-types",
                ),
                MutationCase(
                    f"{prefix}-load-source",
                    f"{load}:src1:{destination_value}", "fields",
                ),
                MutationCase(
                    f"{prefix}-load-width",
                    f"{load}:memory_size:2", "fields",
                ),
                MutationCase(
                    f"{prefix}-store-signed-byte-type",
                    f"{store}:type:1", "field-types",
                ),
                MutationCase(
                    f"{prefix}-store-word-type",
                    f"{store}:type:2", "field-types",
                ),
                MutationCase(
                    f"{prefix}-store-address",
                    f"{store}:src1:{source_value}", "fields",
                ),
                MutationCase(
                    f"{prefix}-store-value",
                    f"{store}:src2:{source_value}", "fields",
                ),
                MutationCase(
                    f"{prefix}-store-width",
                    f"{store}:memory_size:2", "fields",
                ),
            )
        )
    return tuple(cases)


MUTATION_CASES = mutation_cases()
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


def diagnostic_environment(function=FUNCTION):
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
        DCC_MIR_SELECT_FUNCTION=function,
        DCC_MIR_SELECT_REPORT_FUNCTION=function,
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_MACHINE_FUNCTION=function,
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
    if selector is None or selector == "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )
    reject = reject_reason_from(report, function)
    if expected_reject is not None and reject != expected_reject:
        raise RuntimeError(
            f"{context} rejected as {reject!r}, "
            f"expected {expected_reject!r}\n{report}"
        )
    return selector


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    if selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "byte record copy selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def forced_fallback_control(compiler, output_dir):
    environment = diagnostic_environment()
    environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    report = run(
        compiler_command(compiler, output_dir / "forced-spilled.MAC"),
        environment,
    )
    if selection_from(report, FUNCTION) != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n" + report
        )
    expected = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if expected not in report:
        raise RuntimeError(
            "forced spilled cost candidate was not selected\n" + report
        )


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
    report = run(
        command, diagnostic_environment(control.function), timeout=300
    )
    if control.expect_exact:
        require_exact(report, control.function, f"{control.name} {mode}")
        outcome = "exact"
    else:
        require_generic(
            report, control.function, f"{control.name} {mode}",
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
                f"{control.name} {mode} runtime missing {text!r}\n"
                f"{runtime}"
            )
    return (
        control.name,
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
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), environment
    )
    selector = require_generic(
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
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if forced_cost not in forced_report:
        raise RuntimeError(
            f"{case.name} did not select the forced candidate\n"
            f"{forced_report}"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        case.spec,
        "rejected",
        case.expected_reject,
        selector,
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
        default="build/byte-record-copy-wave1000-audit",
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
    write_tsv(
        output_dir / "source-controls.tsv",
        (
            "name", "defines", "outcome", "selector",
            "reject_reason", "mode",
        ),
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
        f"byte record copy Wave 1000 mutations={len(mutation_rows)} "
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
