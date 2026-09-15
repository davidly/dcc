#!/usr/bin/env python3
"""Exhaustively audit the retained for-initializer pointer-walk schedule."""

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
FUNCTION = "fixture_for_init_pointer_walk"
RENAMED_FUNCTION = "fixture_for_init_pointer_walk_renamed"
SOURCE = "tests/mir-clobber/forinitptr.c"
STAGED_SOURCE = "foriptr.c"
TEMPLATE = "for-init-pointer-walk-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
INSTRUCTION_COUNT = 34
BASELINE_SHA256 = (
    "7dee6b9ff0ec8842ee29f2e82955f1f45d06999445e8dffda7181a9c5f81b0a3"
)
BASELINE_SELECTED_HASH = "ad66cb79"
EXPECTED_RUNTIME = ("for init pointer walk failures=0 checksum=130",)
DOUBLE_STEP_RUNTIME = ("for init pointer walk failures=0 checksum=260",)
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=for-init-pointer-walk-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
MUTABLE_FIELDS = (
    ("opcode", 999),
    ("dst", 1960),
    ("src1", 1960),
    ("src2", 1961),
    ("type", 777),
    ("immediate", 12345),
    ("label", 1960),
    ("phi_pred1", 1960),
    ("phi_pred2", 1961),
    ("successor0", 1960),
    ("successor1", 1961),
    ("successor_count", 7),
    ("object", 1960),
    ("memory_size", 7),
    ("memory_flags", 127),
    ("pointee_volatile_mask", 127),
    ("has_pointer_qualifiers", 7),
    ("bit_width", 7),
    ("bit_shift", 7),
    ("bit_mask", 127),
    ("secondary_offset", 12345),
    ("inline_temp_id", 1960),
    ("divmod_cast_types", 127),
    ("name_identity", 1),
    ("base_identity", 1),
)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False
    expected_runtime: tuple[str, ...] = EXPECTED_RUNTIME


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "FP98BASE", expect_exact=True),
    SourceControl(
        "renamed", "FP98NAME", ("FORINITPTR_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "postfix", "FP98POST", ("FORINITPTR_POSTFIX",),
        expect_exact=True,
    ),
    SourceControl(
        "unsigned-length", "FP98ULEN", ("FORINITPTR_UNSIGNED_LENGTH",),
    ),
    SourceControl(
        "volatile-steps", "FP98VOLT", ("FORINITPTR_VOLATILE_STEPS",),
    ),
    SourceControl(
        "unsigned-steps", "FP98USTP", ("FORINITPTR_UNSIGNED_STEPS",),
    ),
    SourceControl(
        "wide-pointer", "FP98WIDE", ("FORINITPTR_WIDE_POINTER",),
    ),
    SourceControl(
        "greater-than", "FP98GT", ("FORINITPTR_GREATER_THAN",),
    ),
    SourceControl(
        "double-step", "FP98STEP", ("FORINITPTR_DOUBLE_STEP",),
        expected_runtime=DOUBLE_STEP_RUNTIME,
    ),
    SourceControl(
        "extra-cfg", "FP98CFG", ("FORINITPTR_EXTRA_CFG",),
    ),
)
MUTATION_CASES = tuple(
    MutationCase(
        f"instruction-{instruction}-{field}",
        f"{instruction}:{field}:{value}",
    )
    for instruction in range(INSTRUCTION_COUNT)
    for field, value in MUTABLE_FIELDS
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=len(MUTATION_CASES))

MUTATION_HOOK_NEEDLE = """\
    else if (!strcmp(field, "memory_size"))
        insn->memory_size = (int)value;
    else if (!strcmp(field, "src1"))
        insn->src1 = (int)value;
    else if (!strcmp(field, "src2"))
        insn->src2 = (int)value;
"""
MUTATION_HOOK_REPLACEMENT = """\
    else if (!strcmp(field, "opcode"))
        insn->opcode = (int)value;
    else if (!strcmp(field, "dst"))
        insn->dst = (int)value;
    else if (!strcmp(field, "memory_size"))
        insn->memory_size = (int)value;
    else if (!strcmp(field, "src1"))
        insn->src1 = (int)value;
    else if (!strcmp(field, "src2"))
        insn->src2 = (int)value;
    else if (!strcmp(field, "label"))
        insn->label = (int)value;
    else if (!strcmp(field, "phi_pred1"))
        insn->phi_pred1 = (int)value;
    else if (!strcmp(field, "phi_pred2"))
        insn->phi_pred2 = (int)value;
    else if (!strcmp(field, "successor0"))
        insn->successors[0] = (int)value;
    else if (!strcmp(field, "successor1"))
        insn->successors[1] = (int)value;
    else if (!strcmp(field, "successor_count"))
        insn->successor_count = (int)value;
    else if (!strcmp(field, "object"))
        insn->object = (int)value;
    else if (!strcmp(field, "memory_flags"))
        insn->memory_flags = (int)value;
    else if (!strcmp(field, "pointee_volatile_mask"))
        insn->pointee_volatile_mask = (unsigned int)value;
    else if (!strcmp(field, "has_pointer_qualifiers"))
        insn->has_pointer_qualifiers = (int)value;
    else if (!strcmp(field, "bit_width"))
        insn->bit_width = (int)value;
    else if (!strcmp(field, "bit_shift"))
        insn->bit_shift = (int)value;
    else if (!strcmp(field, "bit_mask"))
        insn->bit_mask = (unsigned int)value;
    else if (!strcmp(field, "secondary_offset"))
        insn->secondary_offset = (int)value;
    else if (!strcmp(field, "inline_temp_id"))
        insn->inline_temp_id = (int)value;
    else if (!strcmp(field, "divmod_cast_types"))
        insn->divmod_cast_types = (int)value;
    else if (!strcmp(field, "name_identity") &&
             value > 0 && value <= UCHAR_MAX)
        insn->name[0] = (char)value;
    else if (!strcmp(field, "base_identity") &&
             value > 0 && value <= UCHAR_MAX)
        insn->base_name[0] = (char)value;
"""


def run(command, env=None, timeout=180, cwd=ROOT):
    environment = os.environ.copy() if env is None else env.copy()
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    completed = subprocess.run(
        command,
        cwd=cwd,
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


def prepare_mutation_compiler(output_dir):
    source_dir = output_dir / "mutation-compiler-src"
    build_dir = output_dir / "mutation-compiler-build"
    binary_dir = output_dir / "mutation-compiler-bin"
    shutil.copytree(ROOT / "src" / "dcc", source_dir)
    emit_path = source_dir / "dcc_mir_machine_emit.c"
    emit_text = emit_path.read_text(encoding="utf-8")
    if emit_text.count(MUTATION_HOOK_NEEDLE) != 1:
        raise RuntimeError("diagnostic mutation hook shape changed")
    emit_path.write_text(
        emit_text.replace(
            MUTATION_HOOK_NEEDLE, MUTATION_HOOK_REPLACEMENT
        ),
        encoding="utf-8",
    )
    run(
        [
            "cmake", "-S", str(source_dir), "-B", str(build_dir),
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DDCC_RUNTIME_OUTPUT_DIRECTORY={binary_dir}",
        ],
        timeout=300,
    )
    run(
        [
            "cmake", "--build", str(build_dir), "--parallel",
            "--target", "dcc",
        ],
        timeout=600,
    )
    compiler = binary_dir / ("dcc.exe" if os.name == "nt" else "dcc")
    if not compiler.is_file():
        raise RuntimeError(f"mutation compiler not found: {compiler}")
    return compiler


def dccmake_path():
    tool = ROOT / ("dccmake.exe" if os.name == "nt" else "dccmake")
    if not tool.is_file():
        raise RuntimeError(f"dccmake not found: {tool}")
    return tool


def diagnostic_environment(function=FUNCTION, include_cost=False):
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
        DCC_MIR_MACHINE_FUNCTION=function,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
    )
    if include_cost:
        environment["DCC_MIR_COST_REPORT"] = "1"
    else:
        environment.pop("DCC_MIR_COST_REPORT", None)
    return environment


def compiler_command(compiler, output, defines=()):
    command = [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
    ]
    for define in defines:
        command.append(f"-D{define}")
    command.extend([SOURCE, "-o", str(output)])
    return command


def selection_from(report, function=FUNCTION):
    matches = [
        match for match in SELECTION.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("selector") if matches else None


def reject_reason_from(report, function=FUNCTION):
    matches = [
        match for match in REJECT_REASON.finditer(report)
        if match.group("function") == function
    ]
    return matches[-1].group("reason") if matches else None


def selected_hash(report, function=FUNCTION):
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
            f"{context} did not cost-select the exact schedule\n{report}"
        )


def require_generic(report, function, context):
    selector = selection_from(report, function)
    if selector is None or selector == "scheduled-machine-cfg":
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )
    return selector


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(include_cost=True),
    )
    require_exact(report, FUNCTION, "baseline")
    actual_hash = selected_hash(report)
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


def forced_fallback_control(compiler, output_dir):
    environment = diagnostic_environment(include_cost=True)
    environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    report = run(
        compiler_command(compiler, output_dir / "forced-spilled.MAC"),
        environment,
    )
    selector = require_generic(
        report, FUNCTION, "forced fallback control"
    )
    expected = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if expected not in report or selector != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n" + report
        )


def run_runtime_control(
    compiler, dccmake, output_dir, control, stack_check, peep
):
    mode = (
        f"{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    stage_dir = output_dir / "runtime" / control.name / mode
    shutil.rmtree(stage_dir, ignore_errors=True)
    stage_dir.mkdir(parents=True)
    shutil.copy2(ROOT / SOURCE, stage_dir / STAGED_SOURCE)
    command = [
        str(dccmake),
        f"dcc-input={STAGED_SOURCE}",
        f"dcc-output={control.output_name}",
        "dcc-build-dir=.",
        f"dcc-tool={compiler}",
        f"dcc-runtime={ROOT / 'DCCRTL.MAC'}",
        f"dcc-include-directory={ROOT}",
        f"dcc-peep={str(peep).lower()}",
        f"dcc-stack-check={str(stack_check).lower()}",
        "dcc-stack-bytes=512",
    ]
    if control.defines:
        command.append(f"dcc-define={','.join(control.defines)}")
    report = run(
        command,
        diagnostic_environment(control.function, include_cost=True),
        timeout=300,
        cwd=stage_dir,
    )
    if control.expect_exact:
        require_exact(
            report, control.function, f"{control.name} {mode}"
        )
        outcome = "exact"
    else:
        require_generic(
            report, control.function, f"{control.name} {mode}"
        )
        outcome = "generic"
    runtime = run(
        ["ntvcm", "-p", "-s:0", f"{control.output_name}.COM"],
        timeout=60,
        cwd=stage_dir,
    )
    missing = [
        expected for expected in control.expected_runtime
        if expected not in runtime
    ]
    if missing:
        raise RuntimeError(
            f"{control.name} {mode} runtime missing {missing!r}\n"
            f"{runtime}"
        )
    return (
        control.name,
        ",".join(control.defines) or "-",
        outcome,
        selection_from(report, control.function) or "",
        reject_reason_from(report, control.function) or "",
        mode,
        "passed",
    )


def run_source_controls(compiler, dccmake, output_dir):
    return [
        run_runtime_control(
            compiler, dccmake, output_dir, control, stack_check, peep
        )
        for control in SOURCE_CONTROLS
        for stack_check in (True, False)
        for peep in (True, False)
    ]


def run_mutation(compiler, work_dir, case):
    output = work_dir / f"{case.name}.MAC"
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(compiler_command(compiler, output), environment)
    selector = selection_from(report)
    outcome = (
        "survived"
        if selector == "scheduled-machine-cfg"
        else "rejected"
    )
    if selector is None:
        raise RuntimeError(
            f"{case.name} did not report a selected fallback\n{report}"
        )
    if outcome == "rejected" and reject_reason_from(report) is None:
        raise RuntimeError(
            f"{case.name} fell back without matcher rejection\n{report}"
        )
    output.unlink(missing_ok=True)
    return (
        case.name,
        case.spec,
        outcome,
        selector,
        reject_reason_from(report) or "",
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
        default="build/for-init-pointer-walk-wave9800-audit",
    )
    parser.add_argument(
        "--allow-survivors",
        action="store_true",
        help="report accepted mutations instead of failing",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True)

    compiler = prepare_mutation_compiler(output_dir)
    dccmake = dccmake_path()
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
            "reject_reason", "mode", "runtime",
        ),
        source_rows,
    )
    write_tsv(
        output_dir / "mutation-census.tsv",
        ("name", "mutation", "outcome", "selector", "reject_reason"),
        mutation_rows,
    )

    outcomes = Counter(row[2] for row in mutation_rows)
    print(
        f"for-init pointer walk Wave 9800 mutations="
        f"{len(mutation_rows)} {outcomes}"
    )
    print(
        f"instructions={INSTRUCTION_COUNT} "
        f"fields-per-instruction={len(MUTABLE_FIELDS)}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)} runtimes={len(source_rows)}"
    )
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={baseline_hash}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")
    if outcomes != EXPECTED_MUTATION_OUTCOMES and not args.allow_survivors:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(f"meaningful survivors={outcomes['survived']}")


if __name__ == "__main__":
    main()
