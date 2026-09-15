#!/usr/bin/env python3
"""Exhaustively audit the retained matrix-bitops exact schedule."""

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
FUNCTION = "fixture_matrix_bitops"
RENAMED_FUNCTION = "fixture_matrix_bitops_renamed"
SOURCE = "tests/mir-clobber/matbitops.c"
TEMPLATE = "matrix-bitops-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
INSTRUCTION_COUNT = 89
BASELINE_SHA256 = (
    "ece644935f884b1ef8d9a940dab5597734ba53ff19b046aa1e7fadfc26ce1982"
)
BASELINE_SELECTED_HASH = "6eaf1744"
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=matrix-bitops-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False
    expected_checksum: int = 3037


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "MB92BASE", expect_exact=True),
    SourceControl(
        "renamed", "MB92NAME", ("MATRIX_BITOPS_RENAMED",),
        RENAMED_FUNCTION, expect_exact=True,
    ),
    SourceControl(
        "changed-multiplier", "MB92MULT",
        ("MATRIX_BITOPS_CHANGED_MULTIPLIER",),
        expected_checksum=4227,
    ),
    SourceControl(
        "volatile-matrix", "MB92VOLT", ("MATRIX_BITOPS_VOLATILE",),
    ),
    SourceControl(
        "unsigned-elements", "MB92UNSG", ("MATRIX_BITOPS_UNSIGNED",),
    ),
    SourceControl(
        "extra-cfg", "MB92XCFG", ("MATRIX_BITOPS_EXTRA_CFG",),
    ),
)

MUTABLE_FIELDS = (
    ("opcode", 999),
    ("dst", 30000),
    ("src1", 30000),
    ("src2", 30001),
    ("type", 777),
    ("immediate", 12345),
    ("label", 30000),
    ("phi_pred1", 30000),
    ("phi_pred2", 30001),
    ("successor0", 30000),
    ("successor1", 30001),
    ("successor_count", 7),
    ("object", 30000),
    ("memory_size", 7),
    ("memory_flags", 127),
    ("pointee_volatile_mask", 127),
    ("has_pointer_qualifiers", 7),
    ("bit_width", 7),
    ("bit_shift", 7),
    ("bit_mask", 127),
    ("secondary_offset", 12345),
    ("inline_temp_id", 30000),
    ("divmod_cast_types", 127),
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
"""


def run(command, env=None, timeout=180, check=True):
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
    if check and completed.returncode:
        raise RuntimeError(
            f"{' '.join(map(str, command))} failed\n{completed.stdout}"
        )
    return completed


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
        emit_text.replace(MUTATION_HOOK_NEEDLE, MUTATION_HOOK_REPLACEMENT),
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
    completed = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(completed.stdout, FUNCTION, "baseline")
    actual_hash = selected_hash(completed.stdout, FUNCTION)
    if actual_hash != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            f"baseline selected hash {actual_hash} != "
            f"{BASELINE_SELECTED_HASH}\n{completed.stdout}"
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if BASELINE_SHA256 and digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


def forced_fallback_control(compiler, output_dir):
    environment = diagnostic_environment()
    environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    completed = run(
        compiler_command(
            compiler, output_dir / "forced-spilled.MAC"
        ),
        environment,
    )
    expected = (
        f"MIR cost-selected function={FUNCTION} "
        f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if expected not in completed.stdout or \
            selection_from(completed.stdout, FUNCTION) != \
            "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n"
            + completed.stdout
        )


def run_runtime_control(
    compiler, dccmake, output_dir, control, stack_check, peep
):
    mode = (
        f"{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    build_dir = output_dir / "runtime" / control.name / mode
    source_dir = output_dir / "runtime-sources" / control.name / mode
    shutil.rmtree(build_dir, ignore_errors=True)
    shutil.rmtree(source_dir, ignore_errors=True)
    build_dir.mkdir(parents=True)
    source_dir.mkdir(parents=True)
    source_path = source_dir / "MATBIT.C"
    shutil.copy2(ROOT / SOURCE, source_path)
    command = [
        str(dccmake),
        f"dcc-input={source_path}",
        f"dcc-output={control.output_name}",
        f"dcc-build-dir={build_dir}",
        f"dcc-tool={compiler}",
        f"dcc-peep={str(peep).lower()}",
        f"dcc-stack-check={str(stack_check).lower()}",
        "dcc-stack-bytes=512",
    ]
    if control.defines:
        command.append(f"dcc-define={','.join(control.defines)}")
    completed = run(
        command, diagnostic_environment(control.function), timeout=300
    )
    if control.expect_exact:
        require_exact(
            completed.stdout, control.function, f"{control.name} {mode}"
        )
        outcome = "exact"
    else:
        require_generic(
            completed.stdout, control.function, f"{control.name} {mode}"
        )
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=30,
    ).stdout
    expected = (
        f"matrix bitops failures=0 checksum={control.expected_checksum}"
    )
    if expected not in runtime:
        raise RuntimeError(
            f"{control.name} {mode} runtime missing {expected!r}\n"
            f"{runtime}"
        )
    return (
        control.name,
        ",".join(control.defines) or "-",
        outcome,
        selection_from(completed.stdout, control.function) or "",
        reject_reason_from(completed.stdout, control.function) or "",
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


def run_mutation(compiler, work_dir, case, verify_fallback):
    ordinary_path = work_dir / f"{case.name}-ordinary.MAC"
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    completed = run(
        compiler_command(compiler, ordinary_path),
        environment,
        check=False,
    )
    selector = selection_from(completed.stdout, FUNCTION)
    reject = reject_reason_from(completed.stdout, FUNCTION) or ""
    if completed.returncode:
        outcome = "crashed"
    elif selector == "scheduled-machine-cfg":
        outcome = "accepted"
    elif selector is None:
        outcome = "missing-selection"
    else:
        outcome = "rejected"

    if outcome == "rejected" and verify_fallback:
        forced_path = work_dir / f"{case.name}-forced.MAC"
        forced_environment = environment.copy()
        forced_environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
        forced = run(
            compiler_command(compiler, forced_path),
            forced_environment,
        )
        require_generic(
            forced.stdout, FUNCTION, f"{case.name} forced"
        )
        forced_cost = (
            f"MIR cost-selected function={FUNCTION} "
            f"candidate={FORCED_CANDIDATE} selector=spilled-scalar-cfg"
        )
        if forced_cost not in forced.stdout:
            raise RuntimeError(
                f"{case.name} did not select the forced fallback\n"
                f"{forced.stdout}"
            )
        forced_path.unlink(missing_ok=True)
    ordinary_path.unlink(missing_ok=True)
    return (
        case.name,
        case.spec,
        outcome,
        selector or "",
        reject,
        FORCED_CANDIDATE if outcome == "rejected" else "",
    )


def run_mutation_cases(
    compiler, output_dir, jobs, verify_fallback
):
    work_dir = output_dir / "work"
    shutil.rmtree(work_dir, ignore_errors=True)
    work_dir.mkdir(parents=True)
    try:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=jobs
        ) as executor:
            futures = [
                executor.submit(
                    run_mutation, compiler, work_dir, case,
                    verify_fallback,
                )
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
        default="build/matrix-bitops-wave9900-audit",
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
    baseline_digest = baseline_compile(compiler, output_dir)
    mutation_rows = run_mutation_cases(
        compiler, output_dir, args.jobs, True,
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
    print(
        f"matrix bitops Wave 9900 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"instructions={INSTRUCTION_COUNT} "
        f"fields-per-instruction={len(MUTABLE_FIELDS)}"
    )
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={BASELINE_SELECTED_HASH}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")

    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )

    dccmake = dccmake_path()
    forced_fallback_control(compiler, output_dir)
    source_rows = run_source_controls(
        compiler, dccmake, output_dir
    )
    write_tsv(
        output_dir / "source-controls.tsv",
        (
            "name", "defines", "outcome", "selector",
            "reject_reason", "mode",
        ),
        source_rows,
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
