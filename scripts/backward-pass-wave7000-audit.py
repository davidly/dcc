#!/usr/bin/env python3
"""Exhaustively audit the retained attention backward-pass schedule."""

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
FUNCTION = "backward_pass"
RENAMED_FUNCTION = "backward_pass_renamed"
SOURCE = "tests/mir-clobber/backpass.c"
TEMPLATE = "backward-pass-schedule"
FORCED_CANDIDATE = "spilled-rhs-forward"
INSTRUCTION_COUNT = 730
D = 16
S = 8
V = 10
QB = 0
KB = S * D
VB = 2 * S * D
AB = 3 * S * D
BASELINE_SHA256 = (
    "c5f1c953dee335ce0bc389da9fcb803439136d19ea6e21ca6538cac2cd05586c"
)
BASELINE_SELECTED_HASH = "e16e3e51"
EXPECTED_CHECKSUM = 276938403
EXPECTED_RUNTIME = (
    "backward pass checks=1 failures=0 checksum=276938403",
)
SELECTION = re.compile(
    r"MIR selection function=(?P<function>\S+) "
    r"selector=(?P<selector>\S+) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=backward-pass-schedule "
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
)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    function: str = FUNCTION
    expect_exact: bool = False


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "BP30BASE", expect_exact=True),
    SourceControl(
        "renamed", "BP30RN", ("BACKPASS_RENAMED",),
        RENAMED_FUNCTION, True,
    ),
    SourceControl(
        "volatile-logits", "BP30VL", ("BACKPASS_VOLATILE_LOGITS",),
    ),
    SourceControl(
        "extra-cfg", "BP30CFG", ("BACKPASS_EXTRA_CFG",),
    ),
)
MUTATION_CASES = tuple(
    MutationCase(
        f"instruction-{instruction}-{field}",
        f"{instruction}:{field}:{value}",
    )
    for instruction in range(INSTRUCTION_COUNT)
    for field, value in MUTABLE_FIELDS
) + tuple(
    MutationCase(
        f"instruction-{instruction}-identity",
        f"{instruction}:identity:120",
    )
    for instruction in range(INSTRUCTION_COUNT)
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
        DCC_MIR_FUNCTION=function,
        DCC_MIR_SELECT_FUNCTION=function,
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


def require_generic(report, function, context):
    selector = selection_from(report, function)
    if selector == "scheduled-machine-cfg" or selector is None:
        raise RuntimeError(
            f"{context} selected {selector!r}, expected generic fallback\n"
            f"{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(include_cost=True),
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
            selection_from(report, FUNCTION) != "spilled-scalar-cfg":
        raise RuntimeError(
            "forced fallback selected the wrong emitter\n" + report
        )


def c_divide(numerator, denominator):
    quotient = abs(numerator) // abs(denominator)
    return -quotient if (numerator < 0) != (denominator < 0) else quotient


def clamp(value):
    return max(-32768, min(32767, value))


def q16_to_q8(value):
    if value > 32767 * 256:
        return 32767
    if value < -32768 * 256:
        return -32768
    if value < 0:
        return -((-value) >> 8)
    return value >> 8


def multiply_q8(left, right):
    return q16_to_q8(left * right)


def arithmetic_shift_right(value, bits):
    divisor = 1 << bits
    quotient = c_divide(value, divisor)
    if value < 0 and value % divisor:
        quotient -= 1
    return quotient


def fill_values(count, multiplier, bias, modulus):
    return [
        ((index * multiplier + bias) % modulus) - modulus // 2
        for index in range(count)
    ]


def add_clamped(destination, index, value):
    destination[index] = clamp(destination[index] + value)


def vector_dot(left, right):
    return q16_to_q8(sum(a * b for a, b in zip(left, right)))


def softmax(values):
    exponential_table = (
        256,248,240,233,226,219,212,206,199,193,187,182,176,171,165,160,
        155,150,146,141,137,133,129,125,121,117,114,110,107,103,100,97,
        94,91,88,86,83,81,78,76,73,71,69,67,65,63,61,59,
        57,55,54,52,50,49,47,46,44,43,42,41,39,38,37,36,
        35,34,33,32,31,30,29,28,27,26,25,25,24,23,22,22,
        21,20,20,19,19,18,17,17,16,16,15,15,14,14,14,13,
        13,12,12,12,11,11,11,10,10,10,9,9,9,8,8,8,
        8,7,7,7,7,7,6,6,6,6,6,5,5,5,5,5,
        5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,
        3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    )
    maximum = max(values)
    mapped = [
        exponential_table[min(255, max(0, maximum - value) >> 3)]
        for value in values
    ]
    total = sum(mapped)
    return [clamp(c_divide(value * 256, total)) for value in mapped]


def matrix_vector(matrix, vector, rows, columns):
    return [
        q16_to_q8(sum(
            matrix[row * columns + column] * vector[column]
            for column in range(columns)
        ))
        for row in range(rows)
    ]


def matrix_vector_add(matrix, vector, output, rows, columns):
    for row in range(rows):
        add_clamped(
            output, row,
            q16_to_q8(sum(
                matrix[row * columns + column] * vector[column]
                for column in range(columns)
            )),
        )


def transposed_matrix_vector(matrix, vector, rows, columns):
    output = [0] * columns
    for row in range(rows):
        for column in range(columns):
            add_clamped(
                output, column,
                multiply_q8(matrix[row * columns + column], vector[row]),
            )
    return output


def add_outer_product(matrix, left, right, rows, columns):
    for row in range(rows):
        for column in range(columns):
            add_clamped(
                matrix, row * columns + column,
                multiply_q8(left[row], right[column]),
            )


def independent_checksum():
    token_gradients = [0] * (V * D)
    position_gradients = [0] * (S * D)
    query_weight_gradients = [0] * (D * D)
    key_weight_gradients = [0] * (D * D)
    value_weight_gradients = [0] * (D * D)
    output_weight_gradients = [0] * (D * V)
    query_weights = fill_values(D * D, 17, 5, 31)
    key_weights = fill_values(D * D, 13, 7, 29)
    value_weights = fill_values(D * D, 11, 3, 27)
    output_weights = fill_values(D * V, 19, 9, 33)
    embeddings = fill_values(S * D, 7, 4, 25)
    attention_output = fill_values(S * D, 5, 2, 23)
    logits = fill_values(S * V, 29, 11, 97)
    workspace = fill_values(3 * S * D, 23, 6, 35)
    workspace.extend(16 + (index * 17 + 3) % 49 for index in range(S * S))
    tokens = [(index * 3 + 1) % V for index in range(S)]
    targets = [(index * 7 + 2) % V for index in range(S)]

    attention_output_gradients = [0] * (S * D)
    for row in range(S):
        gradients = softmax(logits[row * V:(row + 1) * V])
        gradients[targets[row]] -= 256
        gradients = [clamp(value * 128) for value in gradients]
        add_outer_product(
            output_weight_gradients,
            attention_output[row * D:(row + 1) * D],
            gradients, D, V,
        )
        attention_output_gradients[row * D:(row + 1) * D] = \
            matrix_vector(output_weights, gradients, D, V)

    attention_score_gradients = [0] * (S * S)
    value_state_gradients = [0] * (S * D)
    for row in range(S):
        output_row = attention_output_gradients[
            row * D:(row + 1) * D
        ]
        for column in range(S):
            attention_score_gradients[row * S + column] = vector_dot(
                workspace[
                    VB + column * D:VB + (column + 1) * D
                ],
                output_row,
            )
            scalar = workspace[AB + row * S + column]
            destination = value_state_gradients[
                column * D:(column + 1) * D
            ]
            for index, value in enumerate(output_row):
                add_clamped(
                    destination, index, multiply_q8(scalar, value)
                )
            value_state_gradients[
                column * D:(column + 1) * D
            ] = destination

    for row in range(S):
        score_row = attention_score_gradients[
            row * S:(row + 1) * S
        ]
        dad = vector_dot(workspace[AB + row * S:AB + (row + 1) * S],
                         score_row)
        for column in range(S):
            value = clamp(score_row[column] - dad)
            value = multiply_q8(
                workspace[AB + row * S + column], value
            )
            attention_score_gradients[row * S + column] = \
                arithmetic_shift_right(value, 2)

    query_state_gradients = [0] * (S * D)
    for row in range(S):
        query_state_gradients[row * D:(row + 1) * D] = \
            transposed_matrix_vector(
                workspace[KB:KB + S * D],
                attention_score_gradients[row * S:(row + 1) * S],
                S, D,
            )
    key_state_gradients = [0] * (S * D)
    for column in range(S):
        gradient_column = [
            attention_score_gradients[row * S + column]
            for row in range(S)
        ]
        key_state_gradients[column * D:(column + 1) * D] = \
            transposed_matrix_vector(
                workspace[QB:QB + S * D],
                gradient_column, S, D,
            )

    embedding_gradients = attention_output_gradients.copy()
    for row in range(S):
        offset = row * D
        embedding_row = embedding_gradients[offset:offset + D]
        query_row = query_state_gradients[offset:offset + D]
        key_row = key_state_gradients[offset:offset + D]
        value_row = value_state_gradients[offset:offset + D]
        matrix_vector_add(query_weights, query_row, embedding_row, D, D)
        add_outer_product(
            query_weight_gradients,
            embeddings[offset:offset + D], query_row, D, D,
        )
        matrix_vector_add(key_weights, key_row, embedding_row, D, D)
        add_outer_product(
            key_weight_gradients,
            embeddings[offset:offset + D], key_row, D, D,
        )
        matrix_vector_add(value_weights, value_row, embedding_row, D, D)
        add_outer_product(
            value_weight_gradients,
            embeddings[offset:offset + D], value_row, D, D,
        )
        embedding_gradients[offset:offset + D] = embedding_row

    for row in range(S):
        offset = row * D
        token = tokens[row]
        for column in range(D):
            add_clamped(
                token_gradients, token * D + column,
                embedding_gradients[offset + column],
            )
            add_clamped(
                position_gradients, offset + column,
                embedding_gradients[offset + column],
            )

    checksum = 5381
    arrays = (
        attention_output_gradients,
        attention_score_gradients,
        query_state_gradients,
        key_state_gradients,
        value_state_gradients,
        embedding_gradients,
        output_weight_gradients,
        query_weight_gradients,
        key_weight_gradients,
        value_weight_gradients,
        token_gradients,
        position_gradients,
    )
    for values in arrays:
        for value in values:
            checksum = (
                checksum * 33 + (value & 0xffff)
            ) & 0xffffffff
    return checksum


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
        command,
        diagnostic_environment(control.function, include_cost=True),
        timeout=300,
    )
    if control.expect_exact:
        require_exact(report, control.function, control.name)
        outcome = "exact"
    else:
        require_generic(report, control.function, control.name)
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=45,
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
    output_path = work_dir / f"{case.name}.MAC"
    environment = diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(
        compiler_command(compiler, output_path), environment
    )
    selector = selection_from(report, FUNCTION)
    outcome = "accepted"
    if selector != "scheduled-machine-cfg":
        require_generic(report, FUNCTION, case.name)
        outcome = "rejected"
    output_path.unlink(missing_ok=True)
    return (
        case.name,
        "mutation",
        case.spec,
        outcome,
        selector or "",
        reject_reason_from(report, FUNCTION) or "",
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
        default="build/backward-pass-wave7000-audit",
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
    source_rows = []
    if not args.skip_runtime:
        source_rows = run_source_controls(
            compiler, dccmake, output_dir
        )
    mutation_rows = run_mutation_cases(
        compiler, output_dir, args.jobs
    )
    write_tsv(output_dir / "source-controls.tsv", source_rows)
    write_tsv(output_dir / "mutation-census.tsv", mutation_rows)

    outcomes = Counter(row[3] for row in mutation_rows)
    if not args.discover and outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"backward pass Wave 7000 mutations={len(MUTATION_CASES)} "
        f"{outcomes}"
    )
    print(
        f"instructions={INSTRUCTION_COUNT} "
        f"fields-per-instruction={len(MUTABLE_FIELDS)} "
        f"identity-mutations={INSTRUCTION_COUNT}"
    )
    print(f"source controls={len(source_rows)}")
    print(f"meaningful survivors={outcomes.get('accepted', 0)}")
    print(f"forced fallback candidate={FORCED_CANDIDATE}")
    print(f"independent-checksum={expected_checksum}")
    print(f"baseline-sha256={baseline_digest}")
    print(f"baseline-selected-hash={baseline_hash}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
