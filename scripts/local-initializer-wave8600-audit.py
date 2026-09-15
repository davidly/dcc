#!/usr/bin/env python3
"""Exhaustively audit both retained local-initializer exact schedules."""

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
SOURCE = "tests/mir-clobber/localinit.c"
TEMPLATE = "local-initializer-schedule"
FORCED_CANDIDATE = "spilled-phi-slot"
FUNCTIONS = {
    "small": ("local_initializer_small", 54, "180d481e"),
    "large": ("local_initializer_large", 276, "f81f5fe5"),
}
RENAMED_FUNCTIONS = {
    "small": "local_initializer_small_renamed",
    "large": "local_initializer_large_renamed",
}
BASELINE_SHA256 = (
    "517061dcfb3c0f22ae5b1527b73801a42ba5dd5d7f3aabf3e2b79b15807c34ca"
)
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=local-initializer-schedule reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=(?P<function>\S+) .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
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


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expected: tuple[str, str] = ("exact", "exact")
    checksum: int = 3883092627


@dataclass(frozen=True)
class MutationCase:
    shape: str
    function: str
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "LINBASE"),
    SourceControl(
        "renamed", "LINNAME", ("LOCALINIT_RENAMED",),
    ),
    SourceControl(
        "changed-values", "LINVALUE", ("LOCALINIT_CHANGED_VALUES",),
        ("generic", "generic"), 1511668187,
    ),
    SourceControl(
        "volatile-local", "LINVOL", ("LOCALINIT_VOLATILE",),
        ("exact", "generic"),
    ),
    SourceControl(
        "extra-cfg", "LINCFG", ("LOCALINIT_EXTRA_CFG",),
        ("generic", "generic"),
    ),
)
MUTATION_CASES = tuple(
    MutationCase(
        shape,
        function,
        f"{shape}-instruction-{instruction}-{field}",
        f"{instruction}:{field}:{value}",
    )
    for shape, (function, instruction_count, _) in FUNCTIONS.items()
    for instruction in range(instruction_count)
    for field, value in MUTABLE_FIELDS
) + tuple(
    MutationCase(
        shape,
        function,
        f"{shape}-instruction-{instruction}-identity",
        f"{instruction}:identity:120",
    )
    for shape, (function, instruction_count, _) in FUNCTIONS.items()
    for instruction in range(instruction_count)
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
IDENTITY_HOOK_NEEDLE = """\
    else if (!strcmp(field, "identity") && value > 0 &&
             value <= UCHAR_MAX) {
        insn->object = -1;
        insn->name[0] = (char)value;
    }
"""
IDENTITY_HOOK_REPLACEMENT = """\
    else if (!strcmp(field, "identity") && value > 0 &&
             value <= UCHAR_MAX) {
        insn->object = 30000;
        insn->name[0] = (char)value;
    }
"""


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


def prepare_mutation_compiler(output_dir):
    source_dir = output_dir / "mutation-compiler-src"
    build_dir = output_dir / "mutation-compiler-build"
    binary_dir = output_dir / "mutation-compiler-bin"
    shutil.copytree(ROOT / "src" / "dcc", source_dir)
    emit_path = source_dir / "dcc_mir_machine_emit.c"
    emit_text = emit_path.read_text(encoding="utf-8")
    if emit_text.count(MUTATION_HOOK_NEEDLE) != 1:
        raise RuntimeError("diagnostic mutation hook shape changed")
    if emit_text.count(IDENTITY_HOOK_NEEDLE) != 1:
        raise RuntimeError("diagnostic identity hook shape changed")
    emit_path.write_text(
        emit_text.replace(
            MUTATION_HOOK_NEEDLE, MUTATION_HOOK_REPLACEMENT
        ).replace(
            IDENTITY_HOOK_NEEDLE, IDENTITY_HOOK_REPLACEMENT
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
        timeout=900,
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


def diagnostic_environment(function=None, include_cost=False):
    environment = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
        "DCC_MIR_SELECT_FUNCTION",
        "DCC_MIR_SELECT_REPORT_FUNCTION",
        "DCC_MIR_MACHINE_FUNCTION",
    ):
        environment.pop(name, None)
    environment.update(
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_MACHINE_TEMPLATE=TEMPLATE,
        DCC_MIR_MACHINE_REPORT="1",
    )
    if function is not None:
        environment.update(
            DCC_MIR_FUNCTION=function,
            DCC_MIR_SELECT_FUNCTION=function,
            DCC_MIR_SELECT_REPORT_FUNCTION=function,
            DCC_MIR_MACHINE_FUNCTION=function,
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


def require_generic(report, function, context):
    selector = selection_from(report, function)
    if selector == "scheduled-machine-cfg" or selector is None:
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
    hashes = {}
    for shape, (function, _, expected_hash) in FUNCTIONS.items():
        require_exact(report, function, f"baseline {shape}")
        hashes[shape] = selected_hash(report, function)
        if hashes[shape] != expected_hash:
            raise RuntimeError(
                f"baseline {shape} selected hash {hashes[shape]} != "
                f"{expected_hash}\n{report}"
            )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest, hashes


def forced_fallback_controls(compiler, output_dir):
    rows = []
    for shape, (function, _, _) in FUNCTIONS.items():
        environment = diagnostic_environment(function, include_cost=True)
        environment["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
        report = run(
            compiler_command(
                compiler, output_dir / f"forced-{shape}.MAC"
            ),
            environment,
        )
        selector = require_generic(
            report, function, f"forced fallback {shape}"
        )
        expected = (
            f"MIR cost-selected function={function} "
            f"candidate={FORCED_CANDIDATE} selector={selector}"
        )
        if expected not in report:
            raise RuntimeError(
                f"forced fallback {shape} selected wrong candidate\n"
                f"{report}"
            )
        rows.append((shape, function, selector))
    return rows


def runtime_function_names(control):
    if "LOCALINIT_RENAMED" in control.defines:
        return RENAMED_FUNCTIONS
    return {
        shape: details[0] for shape, details in FUNCTIONS.items()
    }


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
    staged_source = build_dir / "LOCALINI.C"
    shutil.copy2(ROOT / SOURCE, staged_source)
    command = [
        str(dccmake),
        str(staged_source),
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
        command, diagnostic_environment(include_cost=True), timeout=300
    )
    names = runtime_function_names(control)
    selectors = []
    for shape, expected in zip(("small", "large"), control.expected):
        function = names[shape]
        if expected == "exact":
            require_exact(report, function, f"{control.name} {mode} {shape}")
            selector = "scheduled-machine-cfg"
        else:
            selector = require_generic(
                report, function, f"{control.name} {mode} {shape}"
            )
        selectors.append(selector)
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=60,
    )
    expected_runtime = (
        "local initializer failures=0 checks=23 "
        f"checksum={control.checksum}"
    )
    if expected_runtime not in runtime:
        raise RuntimeError(
            f"{control.name} {mode} missing {expected_runtime!r}\n"
            f"{runtime}"
        )
    return (
        control.name,
        ",".join(control.defines) or "-",
        selectors[0],
        selectors[1],
        mode,
        expected_runtime,
    )


def run_runtime_controls(compiler, dccmake, output_dir):
    return [
        run_runtime_control(
            compiler, dccmake, output_dir,
            control, stack_check, peep,
        )
        for control in SOURCE_CONTROLS
        for stack_check in (True, False)
        for peep in (True, False)
    ]


def run_mutation(compiler, work_dir, case):
    output_path = work_dir / f"{case.name}.MAC"
    environment = diagnostic_environment(case.function)
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=case.function,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(
        compiler_command(compiler, output_path), environment
    )
    selector = selection_from(report, case.function)
    outcome = "accepted"
    if selector != "scheduled-machine-cfg":
        selector = require_generic(
            report, case.function, case.name
        )
        outcome = "rejected"
    output_path.unlink(missing_ok=True)
    return (
        case.name,
        case.shape,
        case.spec,
        outcome,
        selector or "",
        reject_reason_from(report, case.function) or "",
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
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default="build/local-initializer-wave8600-audit",
    )
    parser.add_argument("--discover", action="store_true")
    parser.add_argument("--skip-runtime", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = ROOT / output_dir
    shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True)

    compiler = prepare_mutation_compiler(output_dir)
    baseline_digest, baseline_hashes = baseline_compile(
        compiler, output_dir
    )
    forced_rows = forced_fallback_controls(compiler, output_dir)
    source_rows = []
    if not args.skip_runtime:
        source_rows = run_runtime_controls(
            compiler, dccmake_path(), output_dir
        )
    mutation_rows = run_mutation_cases(
        compiler, output_dir, args.jobs
    )
    write_tsv(
        output_dir / "source-controls.tsv",
        (
            "name", "defines", "small_selector", "large_selector",
            "mode", "runtime_oracle",
        ),
        source_rows,
    )
    write_tsv(
        output_dir / "mutation-census.tsv",
        (
            "name", "shape", "mutation", "outcome",
            "selector", "reject_reason",
        ),
        mutation_rows,
    )

    outcomes = Counter(row[3] for row in mutation_rows)
    shape_outcomes = {
        shape: Counter(
            row[3] for row in mutation_rows if row[1] == shape
        )
        for shape in FUNCTIONS
    }
    if not args.discover and outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"local initializer Wave 8600 mutations={len(MUTATION_CASES)} "
        f"{outcomes}"
    )
    for shape, (_, instruction_count, _) in FUNCTIONS.items():
        print(
            f"{shape} instructions={instruction_count} "
            f"mutations={instruction_count * (len(MUTABLE_FIELDS) + 1)} "
            f"{shape_outcomes[shape]}"
        )
    print(f"source controls={len(source_rows)}")
    print(f"meaningful survivors={outcomes.get('accepted', 0)}")
    print(
        "forced fallback="
        + ",".join(f"{shape}:{selector}" for shape, _, selector in forced_rows)
    )
    print(f"baseline-sha256={baseline_digest}")
    print(
        "baseline-selected-hashes="
        + ",".join(
            f"{shape}:{baseline_hashes[shape]}" for shape in FUNCTIONS
        )
    )
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
