#!/usr/bin/env python3
"""Audit the retained qsort-edge exact schedule semantically."""

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
FUNCTION = "fixture_qsort_edge"
RENAMED_FUNCTION = "fixture_qsort_edge_renamed"
SOURCE = "tests/mir-clobber/qsedge.c"
TEMPLATE = "qsort-edge"
FORCED_CANDIDATE = "spilled-phi-slot"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=qsort-edge reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "a55e6f4cb97df1564115bc1f3fc1b1790141469b4dbff8f706dd5127f6ef4113"
)
BASELINE_SELECTED_HASH = "7564150d"
EXPECTED_RUNTIME = ("qsort edge failures=0",)


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
    SourceControl("baseline", "QS12BASE", expect_exact=True),
    SourceControl(
        "renamed", "QS12RN", ("QS_EDGE_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "volatile-array", "QS12VA", ("QS_EDGE_VOLATILE_ARRAY",),
        expected_reject="array",
    ),
    SourceControl(
        "unsigned-array", "QS12UA", ("QS_EDGE_UNSIGNED_ARRAY",),
        expected_reject=None,
    ),
    SourceControl(
        "long-array", "QS12LA", ("QS_EDGE_LONG_ARRAY",),
        expected_reject=None,
    ),
    SourceControl(
        "array-length", "QS12AL", ("QS_EDGE_ARRAY_65",),
        expected_reject="array",
    ),
    SourceControl(
        "volatile-index", "QS12VI", ("QS_EDGE_VOLATILE_INDEX",),
        expected_reject=None,
    ),
    SourceControl(
        "unsigned-index", "QS12UI", ("QS_EDGE_UNSIGNED_INDEX",),
        expected_reject=None,
    ),
    SourceControl(
        "alternate-compare", "QS12AC", ("QS_EDGE_ALT_COMPARE",),
        expected_reject="compare-alias",
    ),
    SourceControl(
        "second-array", "QS12SA", ("QS_EDGE_SECOND_ARRAY",),
        expected_reject="array-alias",
    ),
    SourceControl(
        "alternate-failure", "QS12AF", ("QS_EDGE_ALT_FAILURE",),
        expected_reject="call-alias",
    ),
    SourceControl(
        "extra-cfg", "QS12CF", ("QS_EDGE_EXTRA_CFG",),
        expected_reject=None,
    ),
)

TYPE_INSTRUCTIONS = (
    1, 2, 3, 4, 6, 9, 11, 14, 16, 17, 18, 19, 20, 21, 22,
    24, 26, 28, 29, 30, 31, 33, 36, 38, 41, 43, 44, 45, 46,
    47, 48, 49, 51, 53, 55, 56, 57, 58, 60, 61, 62, 63, 65,
    68, 70, 73, 75, 76, 77, 78, 79, 80, 81, 84, 87, 88, 89,
    90, 91, 92, 95, 98, 100, 104, 106, 108, 110, 111, 112,
    113, 115, 116, 117, 118, 120, 123, 125, 128, 130, 131,
    132, 133, 134, 135, 136, 139, 142, 143, 144, 145, 146,
    147, 150, 153, 155, 159, 161, 163, 165, 169, 171, 172,
    174, 176, 181, 182, 186, 189, 191, 194, 196, 197, 201,
    203, 204, 206, 208, 209, 211, 213, 215, 219, 220, 224,
    228, 230, 231, 233, 235, 236, 238, 242, 243, 247, 250,
    252, 255, 257, 258, 262, 264, 265, 267, 269, 270, 272,
    274, 276, 280, 281, 285, 289, 291, 292, 294, 296, 297,
    301, 302, 306, 309, 311, 314, 316, 317, 321, 323, 324,
    326, 328, 329, 330, 331, 333, 335, 339, 340,
)
WIDTH_INSTRUCTIONS = (
    3, 5, 19, 20, 30, 32, 46, 47, 57, 59, 62, 64, 78, 79,
    89, 90, 112, 114, 117, 119, 133, 134, 144, 145, 167,
    176, 178, 183, 199, 208, 209, 221, 226, 235, 239, 244,
    260, 269, 270, 282, 287, 296, 298, 303, 319, 328, 329,
    341,
)
SRC1_INSTRUCTIONS = (
    5, 8, 10, 13, 15, 20, 22, 23, 25, 32, 35, 37, 40, 42,
    47, 49, 50, 52, 59, 64, 67, 69, 72, 74, 79, 81, 82, 90,
    92, 93, 100, 104, 105, 107, 114, 119, 122, 124, 127,
    129, 134, 136, 137, 145, 147, 148, 155, 159, 160, 162,
    167, 169, 172, 173, 178, 182, 183, 188, 190, 193, 195,
    199, 201, 204, 205, 209, 211, 212, 214, 220, 221, 226,
    228, 231, 232, 238, 239, 243, 244, 249, 251, 254, 256,
    260, 262, 265, 266, 270, 272, 273, 275, 281, 282, 287,
    289, 292, 293, 298, 302, 303, 308, 310, 313, 315, 319,
    321, 324, 325, 329, 331, 332, 334, 340, 341,
)
SRC2_INSTRUCTIONS = (
    3, 5, 19, 22, 30, 32, 46, 49, 57, 59, 62, 64, 78, 81,
    89, 92, 100, 104, 112, 114, 117, 119, 133, 136, 144,
    147, 155, 159, 169, 172, 176, 178, 182, 201, 204, 208,
    211, 220, 228, 231, 235, 238, 239, 243, 262, 265, 269,
    272, 281, 289, 292, 296, 298, 302, 321, 324, 328, 331,
    340,
)
IMMEDIATE_INSTRUCTIONS = (
    2, 3, 4, 9, 11, 18, 19, 21, 22, 29, 30, 31, 36, 38,
    45, 46, 48, 49, 56, 57, 58, 61, 62, 63, 68, 70, 77, 78,
    80, 81, 84, 88, 89, 91, 92, 95, 98, 111, 112, 113,
    116, 117, 118, 123, 125, 132, 133, 135, 136, 139, 143,
    144, 146, 147, 150, 153, 165, 171, 172, 176, 181, 182,
    189, 191, 197, 203, 204, 208, 211, 219, 220, 224, 230,
    231, 235, 236, 238, 242, 243, 250, 252, 258, 264, 265,
    269, 272, 280, 281, 285, 291, 292, 296, 297, 301, 302,
    309, 311, 317, 323, 324, 328, 330, 331, 339, 340,
)
IDENTITY_INSTRUCTIONS = (
    1, 6, 14, 16, 17, 26, 28, 33, 41, 43, 44, 53, 55, 60,
    65, 73, 75, 76, 87, 108, 110, 115, 120, 128, 130, 131,
    142, 163, 167, 174, 183, 186, 194, 196, 199, 206, 215,
    221, 226, 233, 244, 247, 255, 257, 260, 267, 276, 282,
    287, 294, 303, 306, 314, 316, 319, 326, 335, 341,
)

ARRAY_ADDRESSES = {
    1, 6, 17, 28, 33, 44, 55, 60, 65, 76, 87, 110, 115,
    120, 131, 142, 174, 186, 206, 233, 247, 267, 294, 306,
    326,
}
COMPARE_ADDRESSES = {14, 41, 73, 128, 194, 255, 314}
SORT_CALLS = {16, 43, 75, 130, 196, 257, 316}
FAILURE_CALLS = {26, 53, 108, 163, 215, 276, 335}


def identity_reject(instruction):
    if instruction == 1:
        return "array"
    if instruction in ARRAY_ADDRESSES:
        return "array-alias"
    if instruction == 14:
        return "functions"
    if instruction in COMPARE_ADDRESSES:
        return "compare-alias"
    if instruction in (16, 26):
        return "functions"
    if instruction in SORT_CALLS or instruction in FAILURE_CALLS:
        return "call-alias"
    return "semantic-payload"


MUTATION_CASES = tuple(
    [
        MutationCase(
            f"type-{instruction}", f"{instruction}:type:400",
            "semantic-payload",
        )
        for instruction in TYPE_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"width-{instruction}", f"{instruction}:memory_size:1",
            "semantic-payload",
        )
        for instruction in WIDTH_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"src1-{instruction}", f"{instruction}:src1:120",
            "semantic-payload",
        )
        for instruction in SRC1_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"src2-{instruction}", f"{instruction}:src2:120",
            "semantic-payload",
        )
        for instruction in SRC2_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"immediate-{instruction}",
            f"{instruction}:immediate:12345",
            "semantic-payload",
        )
        for instruction in IMMEDIATE_INSTRUCTIONS
    ]
    + [
        MutationCase(
            f"identity-{instruction}", f"{instruction}:identity:120",
            identity_reject(instruction),
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
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
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


def selected_hash(report, function):
    for match in SELECTED_HASH.finditer(report):
        if match.group("function") == function:
            return match.group("hash")
    return None


def reject_reason(report, function):
    matches = [
        match for match in REJECT_REASON.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("reason") if matches else None


def require_exact(report, function, context):
    match = next(
        (
            match for match in SELECTION.finditer(report)
            if match.group("function") == function
        ),
        None,
    )
    if match is None or match.group("selector") != "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} did not select qsort-edge exactly\n{report}"
        )
    cost = (
        f"MIR cost-selected function={function} "
        "candidate=exact-scheduled selector=scheduled-machine-cfg"
    )
    if cost not in report:
        raise RuntimeError(
            f"{context} did not cost-select qsort-edge exactly\n{report}"
        )


def require_generic(report, function, context):
    matches = [
        match for match in SELECTION.finditer(report)
        if match.group("function") == function
    ]
    if not matches or matches[-1].group("selector") == "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} did not select generic MIR\n{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, FUNCTION, "baseline")
    actual_hash = selected_hash(report, FUNCTION)
    if BASELINE_SELECTED_HASH and actual_hash != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            f"baseline selected hash {actual_hash} != "
            f"{BASELINE_SELECTED_HASH}\n{report}"
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if BASELINE_SHA256 and digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest, actual_hash


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
        command, diagnostic_environment(control.function), timeout=300
    )
    if control.expect_exact:
        require_exact(build_output, control.function, control.name)
        outcome = "exact"
    else:
        require_generic(build_output, control.function, control.name)
        actual_reject = reject_reason(build_output, control.function)
        if (
            control.expected_reject is not None
            and actual_reject != control.expected_reject
        ):
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
        "scheduled-machine-cfg" if control.expect_exact else "generic",
        reject_reason(build_output, control.function) or "",
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
    require_generic(ordinary_report, FUNCTION, case.name)
    actual_reject = reject_reason(ordinary_report, FUNCTION)
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
    require_generic(forced_report, FUNCTION, f"{case.name} forced")
    forced_cost = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if forced_cost not in forced_report:
        raise RuntimeError(
            f"{case.name} did not force {FORCED_CANDIDATE}\n"
            f"{forced_report}"
        )
    if reject_reason(forced_report, FUNCTION) != case.expected_reject:
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
        default="build/qsort-edge-wave1200-audit",
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

    baseline_digest, baseline_hash = baseline_compile(
        compiler, output_dir
    )
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
        f"qsort edge Wave 1200 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={baseline_hash}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
