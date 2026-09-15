#!/usr/bin/env python3
"""Audit the retained scope-block exact schedule semantically."""

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
SOURCE = "tests/mir-clobber/scopblk.c"
TEMPLATE = "scope-block-runner"
FORCED_CANDIDATE = "spilled-phi-slot"
INSTRUCTION_COUNT = 698
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=scope-block-runner reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "47a208a38c9e04f30ca47e8b5ab9c28aabe5d3362e3524c42cdd565d69dccce9"
)
BASELINE_SELECTED_HASH = "b3ab4a13"
EXPECTED_RUNTIME = ("scope block failures=0",)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expect_exact: bool = False


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "SB14OK", expect_exact=True),
    SourceControl(
        "volatile-failures", "SB14VF",
        ("SCOPEBLOCK_VOLATILE_FAILURES",),
    ),
    SourceControl(
        "volatile-long", "SB14VW",
        ("SCOPEBLOCK_VOLATILE_LONG",),
    ),
    SourceControl(
        "unsigned-check-abi", "SB14UC",
        ("SCOPEBLOCK_UNSIGNED_CHECK",),
    ),
    SourceControl(
        "unsigned-parameter-abi", "SB14UP",
        ("SCOPEBLOCK_UNSIGNED_PARAMETER",),
    ),
    SourceControl(
        "external-helper", "SB14EH",
        ("SCOPEBLOCK_EXTERNAL_HELPER",),
    ),
    SourceControl(
        "unsigned-helper", "SB14UH",
        ("SCOPEBLOCK_UNSIGNED_HELPER",),
    ),
    SourceControl(
        "helper-alias", "SB14HA",
        ("SCOPEBLOCK_HELPER_ALIAS",),
    ),
    SourceControl(
        "check-alias", "SB14CA",
        ("SCOPEBLOCK_CHECK_ALIAS",),
    ),
    SourceControl(
        "long-local", "SB14LL",
        ("SCOPEBLOCK_LONG_LOCAL",),
    ),
    SourceControl(
        "subtraction", "SB14SU",
        ("SCOPEBLOCK_SUBTRACT",),
    ),
    SourceControl(
        "narrow-shadow", "SB14NS",
        ("SCOPEBLOCK_NARROW_SHADOW",),
    ),
    SourceControl(
        "alternate-loop-test", "SB14LT",
        ("SCOPEBLOCK_WIDER_LOOP",),
    ),
    SourceControl(
        "extra-cfg", "SB14CF", ("SCOPEBLOCK_EXTRA_CFG",),
    ),
    SourceControl(
        "duplicate-check-string", "SB14DS",
        ("SCOPEBLOCK_DUPLICATE_STRING",),
    ),
    SourceControl(
        "summary-string-alias", "SB14SA",
        ("SCOPEBLOCK_SUMMARY_ALIAS",),
    ),
)

# Every instruction field covered by the matcher's exact semantic fingerprint
# is mutated, including fields whose clean value is zero. Instruction 225
# already has src1=120, so its replacement uses a different valid value.
IDENTITY_INSTRUCTIONS = (
    2, 4, 7, 10, 11, 17, 19, 25, 29, 31, 33, 37, 40, 41, 42, 44,
    45, 49, 50, 51, 53, 54, 56, 62, 65, 67, 69, 72, 75, 78, 79, 85,
    87, 93, 95, 101, 104, 106, 109, 112, 113, 119, 121, 127, 131,
    133, 135, 139, 142, 144, 145, 146, 147, 148, 149, 150, 151,
    152, 153, 154, 155, 156, 162, 163, 164, 166, 167, 170, 173,
    176, 182, 185, 187, 189, 192, 195, 197, 198, 199, 200, 201,
    202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 218,
    219, 223, 226, 230, 233, 236, 242, 246, 253, 255, 257, 260,
    261, 267, 268, 274, 277, 283, 286, 288, 291, 292, 293, 296,
    299, 303, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314,
    315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326,
    331, 332, 333, 334, 336, 337, 338, 341, 346, 352, 353, 359,
    362, 364, 366, 369, 372, 373, 383, 384, 385, 386, 393, 394,
    397, 403, 407, 409, 411, 415, 418, 419, 420, 422, 423, 427,
    428, 429, 431, 432, 434, 440, 444, 446, 448, 452, 455, 457,
    458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469,
    470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481,
    482, 483, 484, 485, 491, 493, 494, 495, 496, 497, 498, 499,
    500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511,
    512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 525, 526,
    528, 529, 531, 534, 538, 541, 544, 550, 553, 556, 558, 561,
    565, 568, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579,
    580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591,
    592, 593, 594, 595, 596, 597, 598, 599, 603, 604, 606, 607,
    609, 612, 615, 621, 622, 628, 630, 637, 638, 645, 646, 653,
    654, 661, 662, 669, 670, 677, 678, 685, 690, 692, 694,
)


def mutation_cases():
    cases = []
    for instruction in range(INSTRUCTION_COUNT):
        cases.extend(
            (
                MutationCase(
                    f"type-{instruction}",
                    f"{instruction}:type:400",
                ),
                MutationCase(
                    f"width-{instruction}",
                    f"{instruction}:memory_size:7",
                ),
                MutationCase(
                    f"src1-{instruction}",
                    f"{instruction}:src1:"
                    f"{121 if instruction == 225 else 120}",
                ),
                MutationCase(
                    f"src2-{instruction}",
                    f"{instruction}:src2:120",
                ),
                MutationCase(
                    f"immediate-{instruction}",
                    f"{instruction}:immediate:12345",
                ),
            )
        )
    cases.extend(
        MutationCase(
            f"identity-{instruction}",
            f"{instruction}:identity:120",
        )
        for instruction in IDENTITY_INSTRUCTIONS
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
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_COST_REPORT="1",
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


def selection_from(report):
    matches = [
        match for match in SELECTION.finditer(report)
        if match.group("function") == FUNCTION
    ]
    return matches[-1].group("selector") if matches else None


def reject_reason_from(report):
    matches = [
        match for match in REJECT_REASON.finditer(report)
        if match.group("function") == FUNCTION
    ]
    return matches[-1].group("reason") if matches else None


def selected_hash(report):
    matches = [
        match for match in SELECTED_HASH.finditer(report)
        if match.group("function") == FUNCTION
    ]
    return matches[-1].group("hash") if matches else None


def require_exact(report, context):
    if selection_from(report) != "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} did not retain the exact schedule\n{report}"
        )
    expected = (
        f"MIR cost-selected function={FUNCTION} "
        "candidate=exact-scheduled selector=scheduled-machine-cfg"
    )
    if expected not in report or reject_reason_from(report) is not None:
        raise RuntimeError(
            f"{context} did not cost-select the clean exact schedule\n"
            f"{report}"
        )


def require_generic(report, context):
    selector = selection_from(report)
    reject = reject_reason_from(report)
    if selector is None or selector == "scheduled-machine-cfg" or reject is None:
        raise RuntimeError(
            f"{context} selected {selector!r} with reject {reject!r}, "
            f"expected generic fallback\n{report}"
        )
    return selector, reject


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, "baseline")
    actual_hash = selected_hash(report)
    if actual_hash != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            f"baseline selected hash {actual_hash} != "
            f"{BASELINE_SELECTED_HASH}\n{report}"
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest, actual_hash


def forced_fallback_control(compiler, output_dir):
    environment = diagnostic_environment()
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
    if expected not in report or selection_from(report) != "spilled-scalar-cfg":
        raise RuntimeError(
            "clean forced fallback selected the wrong emitter\n" + report
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
    report = run(command, diagnostic_environment(), timeout=300)
    if control.expect_exact:
        require_exact(report, f"{control.name} {mode}")
        outcome = "exact"
        selector = "scheduled-machine-cfg"
        reject = ""
    else:
        selector, reject = require_generic(
            report, f"{control.name} {mode}"
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
            f"{control.name} {mode} runtime missing {missing!r}\n"
            f"{runtime}"
        )
    return (
        control.name,
        ",".join(control.defines) or "-",
        outcome,
        selector,
        reject,
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
    selector, reject = require_generic(ordinary_report, case.name)

    forced_environment = environment.copy()
    forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_environment
    )
    forced_selector, forced_reject = require_generic(
        forced_report, f"{case.name} forced"
    )
    forced_cost = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if (
        forced_cost not in forced_report
        or forced_selector != "spilled-scalar-cfg"
        or forced_reject != reject
    ):
        raise RuntimeError(
            f"{case.name} did not reproduce its rejection with the "
            f"forced fallback\n{forced_report}"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        case.spec,
        "rejected",
        selector,
        reject,
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
        default="build/scope-block-wave1700-audit",
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
            "name", "mutation", "outcome", "selector",
            "reject_reason", "forced_candidate",
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
        f"scope block Wave 1700 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={baseline_hash}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
