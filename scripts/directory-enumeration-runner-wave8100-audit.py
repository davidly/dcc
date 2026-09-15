#!/usr/bin/env python3
"""Exhaustively audit the retained directory-enumeration runner."""

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
FUNCTION = "enumerate"
SOURCE = "tests/mir-clobber/direnum.c"
TEMPLATE = "directory-enumeration-runner"
FORCED_CANDIDATE = "spilled-rhs-forward"
INSTRUCTION_COUNT = 318
BASELINE_SHA256 = (
    "e68b136c76d6f291a647889ccc89a1becf718217db8188755dcd3e529bdcb5ea"
)
BASELINE_SELECTED_HASH = "96758d14"
EXPECTED_CHECKSUM = 4524
EXPECTED_RUNTIME = (
    "found=Q7BETA22.D",
    "entry=0 name=Q7ALPHA1.DAT size=128",
    "entry=1 name=Q7BETA22.D size=256",
    "entry=2 name=Q7C.D size=384",
    "directory enumeration checks=1 checksum=4524",
)
FIXTURES = (
    "Q7ALPHA1.DAT",
    "Q7BETA22.D",
    "Q7C.D",
    "Q7NOPE.TXT",
    "R7IGNORE.DAT",
)
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=directory-enumeration-runner "
    r"reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
MUTABLE_FIELDS = (
    ("opcode", 999),
    ("dst", 1734),
    ("src1", 1734),
    ("src2", 1735),
    ("type", 400),
    ("immediate", 12345),
    ("label", 1734),
    ("phi_pred1", 1734),
    ("phi_pred2", 1735),
    ("successor0", 1734),
    ("successor1", 1735),
    ("successor_count", 7),
    ("object", 88),
    ("memory_size", 7),
    ("memory_flags", 127),
    ("pointee_volatile_mask", 127),
    ("has_pointer_qualifiers", 7),
    ("bit_width", 7),
    ("bit_shift", 7),
    ("bit_mask", 127),
    ("secondary_offset", 12345),
    ("inline_temp_id", 120),
    ("divmod_cast_types", 127),
    ("name_identity", 1),
    ("base_identity", 1),
)


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


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


def diagnostic_environment(include_cost=False):
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
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
    )
    if include_cost:
        environment["DCC_MIR_COST_REPORT"] = "1"
    else:
        environment.pop("DCC_MIR_COST_REPORT", None)
    return environment


def compiler_command(compiler, output):
    return [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        SOURCE, "-o", str(output),
    ]


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
    return matches[0].group("reason") if matches else None


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
    if expected not in report:
        raise RuntimeError(
            f"{context} did not select the exact cost candidate\n{report}"
        )


def require_generic(report, context):
    selector = selection_from(report)
    if selector == "scheduled-machine-cfg" or selector is None:
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )


def independent_checksum():
    checksum = 17
    for value in (1, 4, 1, 3, 3, 3, 1, 1, 4, 3, 3):
        checksum = ((checksum % 800) * 33 + value) % 30000
    return checksum


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(include_cost=True),
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
    environment = diagnostic_environment(include_cost=True)
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
            selection_from(report) != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n" + report
        )


def run_runtime_control(
    compiler, dccmake, output_dir, stack_check, peep
):
    name = (
        f"{'stack' if stack_check else 'nostack'}-"
        f"{'peep' if peep else 'nopeep'}"
    )
    build_dir = output_dir / "runtime" / name
    shutil.rmtree(build_dir, ignore_errors=True)
    build_dir.mkdir(parents=True)
    report = run(
        [
            str(dccmake),
            f"dcc-input={SOURCE}",
            "dcc-output=DIRENUM",
            f"dcc-build-dir={build_dir}",
            f"dcc-tool={compiler}",
            f"dcc-peep={str(peep).lower()}",
            f"dcc-stack-check={str(stack_check).lower()}",
            "dcc-stack-bytes=512",
            "dcc-floatio=true",
            "dcc-flongio=true",
        ],
        diagnostic_environment(include_cost=True),
        timeout=300,
    )
    require_exact(report, name)
    for fixture in FIXTURES:
        shutil.copy2(
            ROOT / "tests" / "mir-clobber" / fixture,
            build_dir / fixture,
        )
    runtime = run(
        ["ntvcm", "-p", "-s:0", "DIRENUM.COM"],
        timeout=45,
        cwd=build_dir,
    )
    missing = [text for text in EXPECTED_RUNTIME if text not in runtime]
    if missing:
        raise RuntimeError(
            f"{name} runtime missing {missing!r}\n{runtime}"
        )
    return (
        name, "runtime", "-", "exact",
        selection_from(report) or "", "", "directory-oracle",
    )


def run_runtime_controls(compiler, dccmake, output_dir):
    return [
        run_runtime_control(
            compiler, dccmake, output_dir, stack_check, peep
        )
        for stack_check in (True, False)
        for peep in (True, False)
    ]


def run_mutation(compiler, work_dir, case):
    output_path = work_dir / f"{case.name}.MAC"
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(
        compiler_command(compiler, output_path), environment
    )
    selector = selection_from(report)
    reason = reject_reason_from(report)
    outcome = "accepted"
    if selector != "scheduled-machine-cfg":
        require_generic(report, case.name)
        if reason is None:
            raise RuntimeError(
                f"{case.name} fell back without a matcher rejection\n"
                f"{report}"
            )
        outcome = "rejected"
    output_path.unlink(missing_ok=True)
    return (
        case.name, "mutation", case.spec, outcome,
        selector or "", reason or "",
        "ordinary-selection",
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
                executor.submit(
                    run_mutation, compiler, work_dir, case
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
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default="build/directory-enumeration-runner-wave8100-audit",
    )
    parser.add_argument("--discover", action="store_true")
    parser.add_argument("--skip-runtime", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    expected_checksum = independent_checksum()
    if expected_checksum != EXPECTED_CHECKSUM:
        raise RuntimeError(
            f"independent checksum {expected_checksum} != "
            f"{EXPECTED_CHECKSUM}"
        )

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
    runtime_rows = []
    if not args.skip_runtime:
        runtime_rows = run_runtime_controls(
            compiler, dccmake, output_dir
        )
    mutation_rows = run_mutation_cases(
        compiler, output_dir, args.jobs
    )
    write_tsv(output_dir / "runtime-controls.tsv", runtime_rows)
    write_tsv(output_dir / "mutation-census.tsv", mutation_rows)

    outcomes = Counter(row[3] for row in mutation_rows)
    if not args.discover and outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"directory enumeration Wave 8100 "
        f"mutations={len(MUTATION_CASES)} {outcomes}"
    )
    print(
        f"instructions={INSTRUCTION_COUNT} "
        f"fields-per-instruction={len(MUTABLE_FIELDS)}"
    )
    print(f"runtime controls={len(runtime_rows)}")
    print(f"meaningful survivors={outcomes.get('accepted', 0)}")
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"independent-checksum={expected_checksum}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={baseline_hash}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'runtime-controls.tsv'}")


if __name__ == "__main__":
    main()
