#!/usr/bin/env python3
"""Audit the retained board-matrix-print exact schedule semantically."""

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
FUNCTION = "board_matrix_print"
RENAMED_FUNCTION = "board_matrix_print_renamed"
SOURCE = "tests/mir-clobber/boardmx.c"
TEMPLATE = "board-matrix-print-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>scheduled-machine-cfg|"
    r"hybrid-homed-scalar-cfg|homed-scalar-cfg|"
    r"regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=board-matrix-print-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "e5027db9c92b8eade0582fa0e215731461e61ccabe8881a3ba018ef07e9b9460"
)
BASELINE_SELECTED_HASH = "7d561397"
EXPECTED_RUNTIME = (
    " 1  0  1 ",
    " 0  1  0 ",
    " 1  1  0 ",
    "board matrix failures=0",
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
    SourceControl("baseline", "BM1100OK", expect_exact=True),
    SourceControl(
        "renamed", "BM1100RN", ("BOARDMX_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "wrapped-printf", "BM1100WP", ("BOARDMX_WRAPPED_PRINTF",),
        expect_exact=True,
    ),
    SourceControl(
        "volatile-board", "BM1100VB", ("BOARDMX_VOLATILE_BOARD",),
        expected_reject="board",
    ),
    SourceControl(
        "char-board", "BM1100CB", ("BOARDMX_CHAR_BOARD",),
        expected_reject="board",
    ),
    SourceControl(
        "short-row", "BM1100SR", ("BOARDMX_SHORT_ROW",),
        expected_reject="board",
    ),
    SourceControl(
        "unsigned-size", "BM1100US", ("BOARDMX_UNSIGNED_SIZE",),
        expected_reject="locations",
    ),
    SourceControl("char-size", "BM1100CS", ("BOARDMX_CHAR_SIZE",)),
    SourceControl(
        "split-print-functions", "BM1100SP", ("BOARDMX_SPLIT_PRINT",),
    ),
    SourceControl("cfg-guard", "BM1100CG", ("BOARDMX_CFG_GUARD",)),
    SourceControl("nonvoid", "BM1100NV", ("BOARDMX_NONVOID",)),
)

MUTATION_CASES = (
    MutationCase("parameter-type", "1:type:1", "locations"),
    MutationCase("parameter-identity", "1:identity:120", "locations"),
    MutationCase("row-zero-type", "7:type:1", "loops"),
    MutationCase("row-zero-value", "7:immediate:1", "loops"),
    MutationCase("row-store-type", "8:type:1", "locations"),
    MutationCase("row-store-source", "8:src1:0", "loops"),
    MutationCase("row-store-width", "8:memory_size:1", "locations"),
    MutationCase("row-store-offset", "8:immediate:1", "loops"),
    MutationCase("row-store-identity", "8:identity:120", "locations"),
    MutationCase("row-phi-type", "11:type:1", "loops"),
    MutationCase("row-phi-initial", "11:src1:32", "loops"),
    MutationCase("row-phi-backedge", "11:src2:31", "loops"),
    MutationCase("row-phi-identity", "11:identity:120", "loops"),
    MutationCase("outer-left", "15:src1:31", "loops"),
    MutationCase("outer-right", "15:src2:31", "loops"),
    MutationCase("outer-operator", "15:immediate:61", "loops"),
    MutationCase("outer-type", "15:type:1", "loops"),
    MutationCase("outer-branch-source", "16:src1:31", "loops"),
    MutationCase("column-zero-type", "18:type:1", "loops"),
    MutationCase("column-zero-value", "18:immediate:1", "loops"),
    MutationCase("column-store-type", "19:type:1", "locations"),
    MutationCase("column-store-source", "19:src1:31", "loops"),
    MutationCase("column-store-width", "19:memory_size:1", "locations"),
    MutationCase("column-store-offset", "19:immediate:1", "loops"),
    MutationCase("column-store-identity", "19:identity:120", "locations"),
    MutationCase("column-test-load-type", "24:type:1", "loops"),
    MutationCase("column-test-load-width", "24:memory_size:1", "loops"),
    MutationCase("column-test-load-offset", "24:immediate:1", "loops"),
    MutationCase(
        "column-test-load-identity", "24:identity:120", "loops",
    ),
    MutationCase("inner-left", "26:src1:31", "loops"),
    MutationCase("inner-right", "26:src2:31", "loops"),
    MutationCase("inner-operator", "26:immediate:61", "loops"),
    MutationCase("inner-type", "26:type:1", "loops"),
    MutationCase("inner-branch-source", "27:src1:31", "loops"),
    MutationCase("value-format-type", "28:type:18", "print-calls"),
    MutationCase("value-format-arg-source", "29:src1:31", "print-calls"),
    MutationCase(
        "value-format-arg-position", "29:immediate:1", "print-calls",
    ),
    MutationCase("value-format-arg-type", "29:type:1", "print-calls"),
    MutationCase("board-address-type", "30:type:2", "board"),
    MutationCase("board-address-offset", "30:immediate:1", "board"),
    MutationCase("board-address-identity", "30:identity:120", "board"),
    MutationCase("row-index-type", "32:type:2", "board"),
    MutationCase("row-index-base", "32:src1:31", "board"),
    MutationCase("row-index-value", "32:src2:31", "board"),
    MutationCase("row-index-stride", "32:immediate:7", "board"),
    MutationCase("row-index-width", "32:memory_size:7", "board"),
    MutationCase("column-value-type", "33:type:1", "board"),
    MutationCase("column-value-width", "33:memory_size:1", "board"),
    MutationCase("column-value-offset", "33:immediate:1", "board"),
    MutationCase("column-value-identity", "33:identity:120", "board"),
    MutationCase("column-index-type", "34:type:2", "board"),
    MutationCase("column-index-base", "34:src1:31", "board"),
    MutationCase("column-index-value", "34:src2:31", "board"),
    MutationCase("column-index-stride", "34:immediate:2", "board"),
    MutationCase("column-index-width", "34:memory_size:2", "board"),
    MutationCase("board-load-type", "35:type:1", "board"),
    MutationCase("board-load-source", "35:src1:31", "board"),
    MutationCase("board-load-width", "35:memory_size:2", "board"),
    MutationCase("value-arg-source", "36:src1:31", "print-calls"),
    MutationCase("value-arg-position", "36:immediate:0", "print-calls"),
    MutationCase("value-arg-type", "36:type:1", "print-calls"),
    MutationCase("value-call-type", "37:type:4", "print-calls"),
    MutationCase("value-call-identity", "37:identity:120", "print-calls"),
    MutationCase("increment-load-type", "39:type:1", "loops"),
    MutationCase("increment-load-width", "39:memory_size:1", "loops"),
    MutationCase("increment-load-offset", "39:immediate:1", "loops"),
    MutationCase("increment-load-identity", "39:identity:120", "loops"),
    MutationCase("column-one-type", "40:type:1", "loops"),
    MutationCase("column-one-value", "40:immediate:2", "loops"),
    MutationCase("column-increment-type", "41:type:1", "loops"),
    MutationCase("column-increment-left", "41:src1:31", "loops"),
    MutationCase("column-increment-right", "41:src2:31", "loops"),
    MutationCase("column-increment-operator", "41:immediate:45", "loops"),
    MutationCase("column-update-type", "42:type:1", "loops"),
    MutationCase("column-update-width", "42:memory_size:1", "loops"),
    MutationCase("column-update-offset", "42:immediate:1", "loops"),
    MutationCase("column-update-identity", "42:identity:120", "loops"),
    MutationCase("column-update-source", "42:src1:31", "loops"),
    MutationCase("newline-format-type", "45:type:18", "print-calls"),
    MutationCase("newline-format-id", "45:immediate:2", "print-calls"),
    MutationCase("newline-arg-source", "46:src1:31", "print-calls"),
    MutationCase("newline-arg-position", "46:immediate:1", "print-calls"),
    MutationCase("newline-arg-type", "46:type:1", "print-calls"),
    MutationCase("newline-call-type", "47:type:4", "print-calls"),
    MutationCase("newline-call-identity", "47:identity:120", "print-calls"),
    MutationCase("row-one-type", "51:type:1", "loops"),
    MutationCase("row-one-value", "51:immediate:2", "loops"),
    MutationCase("row-increment-type", "52:type:1", "loops"),
    MutationCase("row-increment-left", "52:src1:31", "loops"),
    MutationCase("row-increment-right", "52:src2:31", "loops"),
    MutationCase("row-increment-operator", "52:immediate:45", "loops"),
    MutationCase("row-update-type", "53:type:1", "loops"),
    MutationCase("row-update-width", "53:memory_size:1", "loops"),
    MutationCase("row-update-offset", "53:immediate:1", "loops"),
    MutationCase("row-update-identity", "53:identity:120", "loops"),
    MutationCase("row-update-source", "53:src1:31", "loops"),
    MutationCase("final-format-type", "56:type:18", "print-calls"),
    MutationCase("final-format-id", "56:immediate:2", "print-calls"),
    MutationCase("final-arg-source", "57:src1:31", "print-calls"),
    MutationCase("final-arg-position", "57:immediate:1", "print-calls"),
    MutationCase("final-arg-type", "57:type:1", "print-calls"),
    MutationCase("final-call-type", "58:type:4", "print-calls"),
    MutationCase("final-call-identity", "58:identity:120", "print-calls"),
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
            "board matrix selected hash changed\n" + report
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
        compiler_command(compiler, output_dir / "forced-spilled.MAC"),
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
        default="build/board-matrix-print-wave1100-audit",
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
        f"board matrix print Wave 1100 mutations={len(mutation_rows)} "
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
