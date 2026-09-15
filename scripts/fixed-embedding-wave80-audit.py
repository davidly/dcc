#!/usr/bin/env python3
"""Audit the retained fixed-embedding exact schedule semantically."""

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
FUNCTION = "fixed_embedding_build"
SOURCE = "tests/mir-clobber/fxembd.c"
FORCED_CANDIDATE = "spilled-phi-slot"
EXACT_SELECTION = (
    "MIR selection function=fixed_embedding_build "
    "selector=scheduled-machine-cfg result=mir"
)
EXACT_COST = (
    "MIR cost-selected function=fixed_embedding_build "
    "candidate=exact-scheduled selector=scheduled-machine-cfg"
)
GENERIC_SELECTION = (
    "MIR selection function=fixed_embedding_build "
    "selector=spilled-scalar-cfg result=mir"
)
GENERIC_COST = (
    "MIR cost-selected function=fixed_embedding_build "
    "candidate=spilled-phi-slot selector=spilled-scalar-cfg"
)
SELECTED_HASH = re.compile(
    r"MIR selection function=fixed_embedding_build .*"
    r"selected-hash=(?P<hash>[0-9a-f]{8})"
)
BASELINE_SHA256 = (
    "399f0d1374c4e85d8da4744ccaf930010a55a07c7169e4675822bdb36ec7f2e4"
)
BASELINE_SELECTED_HASH = "e979e278"
EXPECTED_RUNTIME = (
    "fixed embedding failures=0 clamps=16/16 checksum=-1479652912",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=24)


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
    SourceControl("baseline", "FX75OK", expect_exact=True),
    SourceControl(
        "volatile-tokens",
        "FX75VT",
        ("FXEMBD_VOLATILE_TOKENS",),
    ),
    SourceControl(
        "variadic-clamp",
        "FX75VA",
        ("FXEMBD_VARIADIC_CLAMP",),
    ),
)
MUTATION_CASES = (
    MutationCase("destination-global", "1:identity:120"),
    MutationCase("positions-global", "4:identity:120"),
    MutationCase("destination-initializer", "3:src1:2"),
    MutationCase("outer-zero", "8:immediate:1"),
    MutationCase("outer-phi-backedge", "11:src2:50"),
    MutationCase("outer-bound", "13:immediate:7"),
    MutationCase("token-index-stride", "19:immediate:3"),
    MutationCase("token-load-width", "20:memory_size:1"),
    MutationCase("token-row-scale", "25:immediate:15"),
    MutationCase("weight-index-source", "27:src2:14"),
    MutationCase("inner-counter-identity", "37:identity:120"),
    MutationCase("destination-step", "43:immediate:4"),
    MutationCase("source-store-identity", "49:identity:120"),
    MutationCase("source-load-width", "50:memory_size:1"),
    MutationCase("source-promotion-signedness", "51:type:20"),
    MutationCase("position-load-source", "56:src1:31"),
    MutationCase("wide-add-operator", "58:immediate:45"),
    MutationCase("call-argument-source", "59:src1:43"),
    MutationCase("call-result-type", "60:type:4"),
    MutationCase("result-store-value", "61:src2:44"),
    MutationCase("inner-increment", "64:immediate:2"),
    MutationCase("inner-store-identity", "66:identity:120"),
    MutationCase("outer-increment", "72:immediate:2"),
    MutationCase("outer-store-value", "74:src1:50"),
)


def run(command, env=None, timeout=180):
    if env is None:
        env = os.environ.copy()
    else:
        env = env.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=env,
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
    env = os.environ.copy()
    for name in (
        "DCC_MIR_MACHINE_MUTATE",
        "DCC_MIR_MACHINE_MUTATE_FUNCTION",
        "DCC_MIR_SELECT_CANDIDATE",
        "DCC_MIR_SELECT_FUNCTION",
    ):
        env.pop(name, None)
    env.update(
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
        DCC_MIR_SELECT_FUNCTION=FUNCTION,
        DCC_MIR_COST_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    return env


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


def require_exact(report, context):
    if EXACT_SELECTION not in report or EXACT_COST not in report:
        raise RuntimeError(
            f"{context} did not select fixed embedding exactly\n{report}"
        )


def require_generic(report, context):
    if EXACT_SELECTION in report or EXACT_COST in report:
        raise RuntimeError(
            f"{context} unexpectedly retained fixed embedding\n{report}"
        )
    if GENERIC_SELECTION not in report or GENERIC_COST not in report:
        raise RuntimeError(
            f"{context} did not select {FORCED_CANDIDATE}\n{report}"
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
            "fixed embedding baseline selected hash changed\n" + report
        )
    digest = hashlib.sha256(baseline_path.read_bytes()).hexdigest()
    if digest != BASELINE_SHA256:
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
    build_output = run(
        command, diagnostic_environment(), timeout=300
    )
    if control.expect_exact:
        require_exact(build_output, control.name)
        outcome = "exact"
    else:
        require_generic(build_output, control.name)
        outcome = "generic"
    runtime = run(
        [
            "ntvcm", "-p", "-s:0",
            str(build_dir / f"{control.output_name}.COM"),
        ],
        timeout=30,
    )
    for text in EXPECTED_RUNTIME:
        if text not in runtime:
            raise RuntimeError(
                f"{control.name} runtime missing {text!r}\n{runtime}"
            )
    return (
        control.name,
        "runtime",
        ",".join(control.defines) or "-",
        outcome,
        "scheduled-machine-cfg"
        if control.expect_exact else "spilled-scalar-cfg",
        selected_hash(build_output) or "",
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
    env = diagnostic_environment()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    ordinary_report = run(
        compiler_command(compiler, ordinary_path), env
    )
    require_generic(ordinary_report, case.name)

    forced_env = env.copy()
    forced_env["DCC_MIR_SELECT_CANDIDATE"] = FORCED_CANDIDATE
    forced_report = run(
        compiler_command(compiler, forced_path), forced_env
    )
    require_generic(forced_report, f"{case.name} forced")
    if ordinary_path.read_bytes() != forced_path.read_bytes():
        raise RuntimeError(
            f"{case.name} forced fallback differs from ordinary fallback"
        )
    ordinary_path.unlink(missing_ok=True)
    forced_path.unlink(missing_ok=True)
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        FORCED_CANDIDATE,
        selected_hash(ordinary_report) or "",
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
                "candidate", "selected_hash", "mode",
            )
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/fixed-embedding-wave80-audit",
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
        f"fixed embedding Wave 80 mutations={len(mutation_rows)} "
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
