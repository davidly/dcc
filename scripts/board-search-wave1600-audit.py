#!/usr/bin/env python3
"""Audit the retained board-search exact schedule semantically."""

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
FUNCTION = "board_search"
RENAMED_FUNCTION = "board_search_renamed"
SOURCE = "tests/mir-clobber/bsearch.c"
TEMPLATE = "board-search-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=board-search-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "42754918a399eb67099c355ace0d6c3f12fffa609d93741ab46c61fe943a3627"
)
BASELINE_SELECTED_HASH = "93b87cc0"
EXPECTED_RUNTIME = (
    "board search failures=0 score=21 best=2,20",
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
    expected_reject: str | None = "semantic-payload"


SOURCE_CONTROLS = (
    SourceControl("baseline", "BS14BASE", expect_exact=True),
    SourceControl(
        "renamed", "BS14RN", ("BOARDSEARCH_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "volatile-counts", "BS14VC", ("BOARDSEARCH_VOLATILE_COUNTS",),
    ),
    SourceControl(
        "volatile-moves", "BS14VM", ("BOARDSEARCH_VOLATILE_MOVES",),
    ),
    SourceControl(
        "volatile-side", "BS14VS", ("BOARDSEARCH_VOLATILE_SIDE",),
    ),
    SourceControl(
        "volatile-best", "BS14VB", ("BOARDSEARCH_VOLATILE_BEST",),
    ),
    SourceControl(
        "unsigned-counts", "BS14UC", ("BOARDSEARCH_UNSIGNED_COUNTS",),
    ),
    SourceControl(
        "short-moves", "BS14SM", ("BOARDSEARCH_SHORT_MOVES",),
    ),
    SourceControl(
        "wide-move", "BS14WM", ("BOARDSEARCH_WIDE_MOVE",),
    ),
    SourceControl(
        "volatile-index", "BS14VI", ("BOARDSEARCH_VOLATILE_INDEX",),
    ),
    SourceControl(
        "signed-index", "BS14SI", ("BOARDSEARCH_SIGNED_INDEX",),
    ),
    SourceControl(
        "volatile-score", "BS14VQ", ("BOARDSEARCH_VOLATILE_SCORE",),
    ),
    SourceControl(
        "unsigned-check", "BS14CK", ("BOARDSEARCH_UNSIGNED_CHECK",),
    ),
    SourceControl(
        "unsigned-order", "BS14OR", ("BOARDSEARCH_UNSIGNED_ORDER",),
    ),
    SourceControl(
        "unsigned-return", "BS14UR", ("BOARDSEARCH_UNSIGNED_RETURN",),
    ),
    SourceControl(
        "extra-cfg", "BS14CF", ("BOARDSEARCH_EXTRA_CFG",),
    ),
)

TYPE_INSTRUCTIONS = (
    1, 2, 3, 4, 6, 7, 9, 14, 15, 17, 18, 19, 20, 22, 24,
    27, 29, 32, 37, 41, 44, 48, 58, 60, 61, 62, 63, 64, 65,
    67, 68, 69, 71, 73, 74, 75, 76, 78, 79, 80, 82, 83, 85,
    86, 88, 89, 92, 93, 94, 96, 98, 99, 102, 103, 104, 106,
    107, 108, 110, 112, 117, 118, 124, 125, 126, 129, 130,
    140, 141, 142, 167, 170, 171, 172, 174, 177, 178, 179,
    181, 183, 188, 189, 195, 196, 197, 208, 209, 213,
)
WIDTH_INSTRUCTIONS = (
    17, 18, 39, 43, 46, 49, 62, 63, 69, 71, 91, 94, 96, 101,
    108, 110, 114, 166, 169, 179, 181, 193, 204, 210,
)
SRC1_INSTRUCTIONS = (
    7, 8, 10, 13, 17, 18, 20, 21, 23, 25, 29, 30, 33, 39,
    43, 46, 49, 58, 62, 63, 64, 65, 66, 69, 71, 72, 76, 77,
    80, 81, 83, 84, 86, 87, 89, 91, 94, 96, 97, 101, 104,
    105, 108, 110, 111, 114, 118, 119, 126, 127, 130, 131,
    138, 139, 142, 143, 150, 151, 158, 162, 163, 166, 169,
    172, 173, 176, 179, 181, 182, 189, 190, 193, 197, 198,
    204, 209, 210, 214,
)
SRC2_INSTRUCTIONS = (
    7, 17, 20, 29, 58, 62, 65, 69, 71, 76, 80, 94, 96, 104,
    108, 110, 118, 126, 130, 138, 142, 150, 158, 162, 172,
    179, 181, 189, 197, 209,
)
IMMEDIATE_INSTRUCTIONS = (
    6, 7, 13, 17, 19, 20, 23, 27, 29, 32, 37, 41, 48, 62,
    64, 65, 69, 71, 72, 75, 76, 77, 79, 80, 81, 83, 84, 86,
    87, 89, 94, 96, 97, 99, 103, 104, 108, 110, 111, 118,
    121, 125, 126, 130, 133, 136, 142, 145, 148, 153, 156,
    171, 172, 176, 179, 181, 182, 189, 197, 208, 209,
)
IDENTITY_INSTRUCTIONS = (
    1, 2, 3, 4, 5, 9, 12, 14, 15, 16, 22, 24, 28, 39, 43,
    44, 46, 49, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    67, 68, 70, 73, 74, 78, 82, 85, 88, 91, 92, 93, 95, 98,
    101, 102, 106, 107, 109, 112, 114, 116, 117, 124, 128,
    129, 140, 141, 164, 166, 167, 169, 170, 174, 177, 178,
    180, 183, 187, 188, 191, 193, 195, 196, 202, 204, 207,
    210, 213,
)


def changed_type(instruction):
    if instruction in (48, 58, 208, 209):
        return 2
    if instruction in (67, 69, 71, 92, 94, 96, 106, 108, 110,
                       174, 177, 179, 181):
        return 2
    return 400


def changed_width(instruction):
    return 2 if instruction in (49, 210) else 7


MUTATION_CASES = tuple(
    [
        MutationCase(
            f"type-{instruction}",
            f"{instruction}:type:{changed_type(instruction)}",
        )
        for instruction in TYPE_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"width-{instruction}",
            f"{instruction}:memory_size:{changed_width(instruction)}",
        )
        for instruction in WIDTH_INSTRUCTIONS
    ]
    + [
        MutationCase(f"src1-{instruction}", f"{instruction}:src1:5")
        for instruction in SRC1_INSTRUCTIONS
    ]
    + [
        MutationCase(f"src2-{instruction}", f"{instruction}:src2:0")
        for instruction in SRC2_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"immediate-{instruction}",
            f"{instruction}:immediate:12345",
        )
        for instruction in IMMEDIATE_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"identity-{instruction}",
            f"{instruction}:identity:120",
            None,
        )
        for instruction in IDENTITY_INSTRUCTIONS
    ]
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


def diagnostic_environment(function=FUNCTION):
    environment = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
    ):
        environment.pop(name, None)
    environment.update(
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
        DCC_MIR_SELECT_FUNCTION=function,
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_MACHINE_FUNCTION=function,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
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
    return matches[0].group("reason") if matches else None


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
        diagnostic_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    if selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "board-search selected hash changed\n" + report
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
        command, diagnostic_environment(control.function), timeout=300
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
    environment = diagnostic_environment()
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
        default="build/board-search-wave1600-audit",
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
        f"board search Wave 1600 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print("meaningful survivors=0")
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={BASELINE_SELECTED_HASH}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
