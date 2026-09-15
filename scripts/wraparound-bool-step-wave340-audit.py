#!/usr/bin/env python3
"""Audit the retained wraparound-boolean-step exact schedule semantically."""

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
FUNCTION = "fixture_wrapbool"
SOURCE = "tests/mir-clobber/wrapbool.c"
TEMPLATE = "wraparound-bool-step"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=fixture_wrapbool "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=fixture_wrapbool "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
FORCED_GENERIC_COST = (
    "MIR cost-selected function=fixture_wrapbool "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
REJECT_REASON = re.compile(
    r"MIR machine function=fixture_wrapbool "
    r"template=wraparound-bool-step reject=(?P<reason>\S+)"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=fixture_wrapbool .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "6bf3bb37f7281a406177fccf9c8d67cd069a947a730a280ef0a621159cad78f1"
)
BASELINE_SELECTED_HASH = "f741fcea"
EXPECTED_RUNTIME = ("wrapbool failures=0",)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expect_exact: bool = False
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    expected_reject: str


SOURCE_CONTROLS = (
    SourceControl("baseline", "WB340OK", expect_exact=True),
    SourceControl(
        "reverse-xor", "WB340RX", ("WRAPBOOL_REVERSE_XOR",),
        expect_exact=True,
    ),
    SourceControl(
        "unsigned-count", "WB340UC", ("WRAPBOOL_UNSIGNED_COUNT",),
        expected_reject="shape",
    ),
    SourceControl(
        "int-elements", "WB340IE", ("WRAPBOOL_INT_ELEMENTS",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-current", "WB340VC",
        ("WRAPBOOL_VOLATILE_CURRENT",), expected_reject="shape",
    ),
    SourceControl(
        "volatile-next", "WB340VN", ("WRAPBOOL_VOLATILE_NEXT",),
        expected_reject="shape",
    ),
    SourceControl(
        "unsigned-return", "WB340UR", ("WRAPBOOL_UNSIGNED_RETURN",),
        expected_reject="shape",
    ),
    SourceControl(
        "int-return", "WB340IR", ("WRAPBOOL_INT_RETURN",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-live", "WB340VL", ("WRAPBOOL_VOLATILE_LIVE",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-index", "WB340VI", ("WRAPBOOL_VOLATILE_INDEX",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-left", "WB340LF", ("WRAPBOOL_VOLATILE_LEFT",),
        expected_reject="shape",
    ),
    SourceControl(
        "volatile-right", "WB340RT", ("WRAPBOOL_VOLATILE_RIGHT",),
        expected_reject="shape",
    ),
    SourceControl(
        "extra-cfg", "WB340CF", ("WRAPBOOL_EXTRA_CFG",),
        expected_reject="shape",
    ),
)
MUTATION_CASES = (
    MutationCase("count-type", "1:type:34", "parameters"),
    MutationCase("current-type", "2:type:18", "parameters"),
    MutationCase(
        "current-unsigned-bool-pointer", "2:type:54", "parameters"
    ),
    MutationCase("current-identity", "2:identity:120", "parameters"),
    MutationCase("next-type", "3:type:18", "parameters"),
    MutationCase(
        "next-unsigned-bool-pointer", "3:type:54", "parameters"
    ),
    MutationCase("next-identity", "3:identity:120", "parameters"),
    MutationCase("live-zero-value", "5:immediate:1", "initializers"),
    MutationCase("live-zero-type", "5:type:2", "initializers"),
    MutationCase("live-zero-unsigned", "5:type:36", "initializers"),
    MutationCase("live-init-width", "6:memory_size:2", "initializers"),
    MutationCase("live-init-source", "6:src1:51", "initializers"),
    MutationCase("live-init-identity", "6:identity:120", "initializers"),
    MutationCase("index-zero-value", "30:immediate:1", "initializers"),
    MutationCase("index-zero-type", "30:type:4", "initializers"),
    MutationCase("index-zero-unsigned", "30:type:34", "initializers"),
    MutationCase("index-init-width", "31:memory_size:1", "initializers"),
    MutationCase("index-init-source", "31:src1:4", "initializers"),
    MutationCase("index-init-identity", "31:identity:120", "initializers"),
    MutationCase("phi-initial", "37:src1:4", "loop"),
    MutationCase("phi-update", "37:src2:49", "loop"),
    MutationCase("phi-type", "37:type:34", "loop"),
    MutationCase("phi-pointer-type", "37:type:18", "loop"),
    MutationCase("loop-operator", "42:immediate:62", "loop"),
    MutationCase("loop-left", "42:src1:51", "loop"),
    MutationCase("loop-right", "42:src2:51", "loop"),
    MutationCase("loop-type", "42:type:34", "loop"),
    MutationCase("loop-branch", "43:src1:28", "loop"),
    MutationCase("left-one-value", "47:immediate:2", "left"),
    MutationCase("left-one-type", "47:type:34", "left"),
    MutationCase("left-subtract-operator", "48:immediate:43", "left"),
    MutationCase("left-subtract-left", "48:src1:0", "left"),
    MutationCase("left-subtract-right", "48:src2:51", "left"),
    MutationCase("left-subtract-type", "48:type:34", "left"),
    MutationCase("left-add-operator", "50:immediate:45", "left"),
    MutationCase("left-add-left", "50:src1:0", "left"),
    MutationCase("left-add-right", "50:src2:51", "left"),
    MutationCase("left-add-type", "50:type:34", "left"),
    MutationCase("left-modulo-operator", "52:immediate:47", "left"),
    MutationCase("left-modulo-left", "52:src1:0", "left"),
    MutationCase("left-modulo-right", "52:src2:51", "left"),
    MutationCase("left-modulo-type", "52:type:34", "left"),
    MutationCase("left-index-base", "53:src1:2", "left"),
    MutationCase("left-index-subscript", "53:src2:28", "left"),
    MutationCase("left-index-stride", "53:immediate:2", "left"),
    MutationCase("left-index-width", "53:memory_size:2", "left"),
    MutationCase("left-index-type", "53:type:18", "left"),
    MutationCase(
        "left-index-unsigned-pointer", "53:type:54", "left"
    ),
    MutationCase("left-load-address", "54:src1:68", "left"),
    MutationCase("left-load-width", "54:memory_size:2", "left"),
    MutationCase("left-load-type", "54:type:2", "left"),
    MutationCase("left-load-unsigned-bool", "54:type:38", "left"),
    MutationCase("left-local-source", "55:src1:69", "left"),
    MutationCase("left-local-width", "55:memory_size:2", "left"),
    MutationCase("left-local-identity", "55:identity:120", "left"),
    MutationCase("right-one-value", "59:immediate:2", "right"),
    MutationCase("right-one-type", "59:type:34", "right"),
    MutationCase("right-add-operator", "60:immediate:45", "right"),
    MutationCase("right-add-left", "60:src1:0", "right"),
    MutationCase("right-add-right", "60:src2:51", "right"),
    MutationCase("right-add-type", "60:type:34", "right"),
    MutationCase("right-modulo-operator", "62:immediate:47", "right"),
    MutationCase("right-modulo-left", "62:src1:0", "right"),
    MutationCase("right-modulo-right", "62:src2:51", "right"),
    MutationCase("right-modulo-type", "62:type:34", "right"),
    MutationCase("right-index-base", "63:src1:2", "right"),
    MutationCase("right-index-subscript", "63:src2:28", "right"),
    MutationCase("right-index-stride", "63:immediate:2", "right"),
    MutationCase("right-index-width", "63:memory_size:2", "right"),
    MutationCase("right-index-type", "63:type:18", "right"),
    MutationCase(
        "right-index-unsigned-pointer", "63:type:54", "right"
    ),
    MutationCase("right-load-address", "64:src1:60", "right"),
    MutationCase("right-load-width", "64:memory_size:2", "right"),
    MutationCase("right-load-type", "64:type:2", "right"),
    MutationCase("right-load-unsigned-bool", "64:type:38", "right"),
    MutationCase("right-local-source", "65:src1:61", "right"),
    MutationCase("right-local-width", "65:memory_size:2", "right"),
    MutationCase("right-local-identity", "65:identity:120", "right"),
    MutationCase("store-index-base", "68:src1:1", "store"),
    MutationCase("store-index-subscript", "68:src2:59", "store"),
    MutationCase("store-index-stride", "68:immediate:2", "store"),
    MutationCase("store-index-width", "68:memory_size:2", "store"),
    MutationCase("store-index-type", "68:type:18", "store"),
    MutationCase(
        "store-index-unsigned-pointer", "68:type:54", "store"
    ),
    MutationCase("xor-operator", "71:immediate:124", "store"),
    MutationCase("xor-left", "71:src1:28", "store"),
    MutationCase("xor-right", "71:src2:28", "store"),
    MutationCase("xor-type", "71:type:34", "store"),
    MutationCase("bool-operator", "72:immediate:1", "store"),
    MutationCase("bool-source", "72:src1:61", "store"),
    MutationCase("bool-type", "72:type:2", "store"),
    MutationCase("bool-unsigned-type", "72:type:38", "store"),
    MutationCase("next-store-address", "73:src1:43", "store"),
    MutationCase("next-store-value", "73:src2:39", "store"),
    MutationCase("next-store-width", "73:memory_size:2", "store"),
    MutationCase("reload-index-base", "76:src1:1", "store"),
    MutationCase("reload-index-subscript", "76:src2:59", "store"),
    MutationCase("reload-index-stride", "76:immediate:2", "store"),
    MutationCase("reload-index-width", "76:memory_size:2", "store"),
    MutationCase("reload-index-type", "76:type:18", "store"),
    MutationCase(
        "reload-index-unsigned-pointer", "76:type:54", "store"
    ),
    MutationCase("next-load-address", "77:src1:36", "store"),
    MutationCase("next-load-width", "77:memory_size:2", "store"),
    MutationCase("next-load-type", "77:type:2", "store"),
    MutationCase("next-load-unsigned-bool", "77:type:38", "store"),
    MutationCase("next-branch-source", "78:src1:40", "store"),
    MutationCase("live-load-identity", "79:identity:120", "live-load"),
    MutationCase("live-load-type", "79:type:2", "live-load"),
    MutationCase("live-load-unsigned", "79:type:36", "live-load"),
    MutationCase("live-one-value", "80:immediate:2", "live-one"),
    MutationCase("live-one-type", "80:type:2", "live-one"),
    MutationCase("live-one-unsigned", "80:type:36", "live-one"),
    MutationCase("live-add-operator", "81:immediate:45", "live-add"),
    MutationCase("live-add-left", "81:src1:46", "live-add"),
    MutationCase("live-add-right", "81:src2:45", "live-add"),
    MutationCase("live-add-type", "81:type:36", "live-add"),
    MutationCase("live-store-identity", "82:identity:120", "live-store"),
    MutationCase("live-store-width", "82:memory_size:2", "live-store"),
    MutationCase("live-store-source", "82:src1:45", "live-store"),
    MutationCase("index-one-value", "87:immediate:2", "index-update"),
    MutationCase("index-one-type", "87:type:34", "index-update"),
    MutationCase("index-add-operator", "88:immediate:45", "index-update"),
    MutationCase("index-add-left", "88:src1:51", "index-update"),
    MutationCase("index-add-right", "88:src2:51", "index-update"),
    MutationCase("index-add-type", "88:type:34", "index-update"),
    MutationCase(
        "index-store-identity", "89:identity:120", "index-update"
    ),
    MutationCase("index-store-width", "89:memory_size:4", "index-update"),
    MutationCase("index-store-source", "89:src1:28", "index-update"),
    MutationCase("result-load-identity", "92:identity:120", "result"),
    MutationCase("result-load-type", "92:type:2", "result"),
    MutationCase("result-load-unsigned", "92:type:36", "result"),
    MutationCase("return-source", "93:src1:45", "result"),
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
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
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


def selected_hash(report):
    match = SELECTED_HASH.search(report)
    return match.group("hash") if match is not None else None


def reject_reason(report):
    matches = list(REJECT_REASON.finditer(report))
    return matches[-1].group("reason") if matches else None


def require_exact(report, context):
    if EXACT_SELECTION not in report or EXACT_COST not in report:
        raise RuntimeError(
            f"{context} did not select wraparound bool step exactly\n"
            f"{report}"
        )
    if reject_reason(report) is not None:
        raise RuntimeError(
            f"{context} reported an unexpected rejection\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained wraparound bool step\n"
            f"{report}"
        )
    selection = re.search(
        r"MIR selection function=fixture_wrapbool "
        r"selector=(?P<selector>\S+) result=mir",
        report,
    )
    if selection is None or selection.group("selector") == (
        "scheduled-machine-cfg"
    ):
        raise RuntimeError(
            f"{context} did not select a generic fallback\n{report}"
        )


def baseline_compile(compiler, output_dir):
    baseline_path = output_dir / "baseline.MAC"
    report = run(
        compiler_command(compiler, baseline_path),
        diagnostic_environment(),
    )
    require_exact(report, "baseline")
    if selected_hash(report) != BASELINE_SELECTED_HASH:
        raise RuntimeError(
            "wraparound bool baseline selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if BASELINE_SHA256 and digest != BASELINE_SHA256:
        raise RuntimeError(
            f"baseline assembly changed: {digest} != {BASELINE_SHA256}"
        )
    return digest


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
    build_output = run(command, diagnostic_environment(), timeout=300)
    if control.expect_exact:
        require_exact(build_output, control.name)
        outcome = "exact"
    else:
        require_generic(build_output, control.name)
        actual_reject = reject_reason(build_output)
        if actual_reject != control.expected_reject:
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
        reject_reason(build_output) or "",
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
    require_generic(ordinary_report, case.name)
    actual_reject = reject_reason(ordinary_report)
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
    require_generic(forced_report, f"{case.name} forced")
    if FORCED_GENERIC_COST not in forced_report:
        raise RuntimeError(
            f"{case.name} did not force {FORCED_CANDIDATE}\n"
            f"{forced_report}"
        )
    if reject_reason(forced_report) != case.expected_reject:
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
        default="build/wraparound-bool-step-wave340-audit",
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
        f"wraparound bool step Wave 340 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
