#!/usr/bin/env python3
"""Audit structural pointer-condition exact-schedule rejection paths."""

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
SOURCE = "tests/tptrcnd.c"
EXACT_ACCEPT = (
    "MIR machine function=main "
    "template=pointer-condition-main accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=main "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=main "
    r"selector=(?P<selector>homed-scalar-cfg|"
    r"hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=main "
    r"template=pointer-condition-main reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "8816c4cf4df3c7a7bccb69042a4a814d5c72ea979642204b95fcc23f896ffbfb"
)
BASELINE_SELECTED_HASH = "d4d8d799"
EXPECTED_RUNTIME = ("tptrcnd start", "PASS")
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=19)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expect_exact: bool = False
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str | None = None


@dataclass(frozen=True)
class MutationCase:
    name: str
    spec: str
    defines: tuple[str, ...] = ()
    expected_selector: str = "spilled-scalar-cfg"
    expected_reject: str = ""


SOURCE_CONTROLS = (
    SourceControl("baseline", "PTR63OK", expect_exact=True),
    SourceControl(
        "static-globals",
        "PTR63ST",
        ("PTRW25_STATIC_GLOBALS",),
        expect_exact=True,
    ),
    SourceControl(
        "fastcall-picker",
        "PTR63FP",
        ("PTRW25_FASTCALL_PICKW",),
        expect_exact=True,
    ),
    SourceControl(
        "cfg-shape",
        "PTR63CF",
        ("PTRW25_POINTER_TRUTHINESS",),
        expected_reject="shape",
    ),
    SourceControl(
        "unsigned-character-operations",
        "PTR63UC",
        ("PTRW25_UNSIGNED_CHAR_DATA",),
        expected_reject="operations",
    ),
    SourceControl(
        "fastcall-check",
        "PTR63FC",
        ("PTRW25_FASTCALL_CHECK",),
        expected_reject="fastcall",
    ),
    SourceControl(
        "alternate-init",
        "PTR63IN",
        ("PTRW63_ALT_INIT",),
        expected_reject="init-identity",
    ),
    SourceControl(
        "alternate-fail",
        "PTR63FA",
        ("PTRW63_ALT_FAIL",),
        expected_reject="fail-identity",
    ),
    SourceControl(
        "alternate-check",
        "PTR63CH",
        ("PTRW63_ALT_CHECK",),
        expected_reject="check-identity",
    ),
    SourceControl(
        "alternate-picker",
        "PTR63PK",
        ("PTRW63_ALT_PICKW",),
        expected_reject="picker-identity",
    ),
    SourceControl(
        "volatile-failures",
        "PTR63VF",
        ("PTRW63_VOLATILE_FAILURES",),
        expected_reject="volatile-global",
    ),
    SourceControl(
        "renamed-global",
        "PTR63RG",
        ("PTRW63_RENAMED_GLOBAL_INT",),
        expected_reject="semantic-signature",
    ),
    SourceControl(
        "loop-picker-alias-base",
        "PTR63LA",
        ("PTRW63_LOOP_FUNCTION_ALIAS",),
        expected_reject="semantic-signature",
    ),
)

MUTATION_CASES = (
    MutationCase(
        "byte-promotion-source",
        "581:src1:1734",
        expected_reject="byte-promotion",
    ),
    MutationCase(
        "binary-operation",
        "99:immediate:0",
        expected_reject="operations",
    ),
    MutationCase(
        "indirect-memory-width",
        "185:memory_size:7",
        expected_reject="memory-effects",
    ),
    MutationCase(
        "index-layout-width",
        "6:memory_size:7",
        expected_reject="index-layout",
    ),
    MutationCase(
        "comparison-constant",
        "197:immediate:3144",
        expected_reject="constants",
    ),
    MutationCase(
        "print-call-identity",
        "2441:identity:88",
        expected_reject="print",
    ),
    MutationCase(
        "init-call-identity",
        "10:identity:88",
        expected_reject="init-call",
    ),
    MutationCase(
        "fail-call-identity",
        "209:identity:88",
        expected_reject="fail-call",
    ),
    MutationCase(
        "fail-string-source",
        "208:src1:1734",
        expected_reject="fail-string",
    ),
    MutationCase(
        "check-argument-index",
        "1415:immediate:0",
        expected_reject="check-call",
    ),
    MutationCase(
        "check-string-source",
        "1411:src1:1734",
        expected_reject="check-arguments",
    ),
    MutationCase(
        "picker-call-identity",
        "300:identity:88",
        expected_reject="picker-call",
    ),
    MutationCase(
        "loop-picker-identity",
        "1445:identity:88",
        expected_reject="loop-pickers",
    ),
    MutationCase(
        "global-address-identity",
        "4:identity:88",
        expected_reject="globals",
    ),
    MutationCase(
        "local-store-source",
        "159:src1:1734",
        expected_reject="local-aliases",
    ),
    MutationCase(
        "alias-initialization-source",
        "38:src1:1734",
        expected_reject="alias-initialization",
    ),
    MutationCase(
        "aggregate-stride",
        "6:immediate:341",
        expected_reject="aggregate-layout",
    ),
    MutationCase(
        "global-alias",
        "101:identity:103",
        ("PTRW63_RENAMED_GLOBAL_INT",),
        expected_reject="global-alias",
    ),
    MutationCase(
        "function-alias",
        "1445:identity:112",
        ("PTRW63_LOOP_FUNCTION_ALIAS",),
        expected_reject="function-alias",
    ),
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


def compiler_command(compiler, output, defines=()):
    command = [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
    ]
    for define in defines:
        command.append(f"-D{define}")
    command.extend([SOURCE, "-o", str(output)])
    return command


def selector_from(report):
    match = GENERIC_SELECTION.search(report)
    if match is not None:
        return match.group("selector")
    if EXACT_SELECTION in report:
        return "scheduled-machine-cfg"
    return None


def reject_reason_from(report):
    match = REJECT_REASON.search(report)
    return match.group("reason") if match is not None else None


def baseline_compile(compiler, output_dir):
    env = os.environ.copy()
    env.pop("DCC_MIR_MACHINE_MUTATE", None)
    env.pop("DCC_MIR_MACHINE_MUTATE_FUNCTION", None)
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="pointer-condition-main",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), env)
    if EXACT_ACCEPT not in report or EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select pointer-condition-main\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "pointer-condition baseline selected hash changed\n" + report
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
    env = os.environ.copy()
    env.update(
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="pointer-condition-main",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
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
    build_output = run(command, env, timeout=300)
    exact_selected = EXACT_ACCEPT in build_output
    selector = selector_from(build_output)
    reject_reason = reject_reason_from(build_output)
    if control.expect_exact:
        if not exact_selected or selector != "scheduled-machine-cfg":
            raise RuntimeError(
                f"{control.name} did not retain exact selection\n"
                f"{build_output}"
            )
    else:
        if exact_selected or selector != control.expected_selector:
            raise RuntimeError(
                f"{control.name} did not fall back to "
                f"{control.expected_selector}\n{build_output}"
            )
        if reject_reason != control.expected_reject:
            raise RuntimeError(
                f"{control.name} rejected with {reject_reason!r}, "
                f"expected {control.expected_reject!r}\n{build_output}"
            )
    runtime = run(
        ["ntvcm", "-p", "-s:0",
         str(build_dir / f"{control.output_name}.COM")],
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
        "exact" if control.expect_exact else "generic",
        selector or "",
        reject_reason or "",
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
                        control, stack_check, peep
                    )
                )
    return rows


def run_mutation(compiler, work_dir, case):
    output = work_dir / f"{case.name}.MAC"
    env = os.environ.copy()
    env.update(
        DCC_MIR_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_TEMPLATE="pointer-condition-main",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
        DCC_MIR_REQUIRE_COMPLETE="1",
        DCC_MIR_REQUIRE_EMIT="1",
    )
    report = run(
        compiler_command(compiler, output, case.defines),
        env,
    )
    selector = selector_from(report)
    reject_reason = reject_reason_from(report)
    output.unlink(missing_ok=True)
    if EXACT_ACCEPT in report:
        raise RuntimeError(
            f"{case.name} unexpectedly kept the exact schedule\n{report}"
        )
    if selector != case.expected_selector:
        raise RuntimeError(
            f"{case.name} selected {selector!r}, "
            f"expected {case.expected_selector!r}\n{report}"
        )
    if reject_reason != case.expected_reject:
        raise RuntimeError(
            f"{case.name} rejected with {reject_reason!r}, "
            f"expected {case.expected_reject!r}\n{report}"
        )
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        selector,
        reject_reason,
        ",".join(case.defines),
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
            ("name", "kind", "spec", "outcome", "selector",
             "reject_reason", "mode_or_defines")
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--output-dir",
        default="build/pointer-condition-wave63-audit",
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
        compiler, output_dir, min(args.jobs, 2)
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
        f"pointer-condition Wave 63 mutations={len(mutation_rows)} "
        f"{outcomes}"
    )
    print(
        f"source controls={len(source_rows)} "
        f"variants={len(SOURCE_CONTROLS)}"
    )
    print("meaningful survivors=0")
    print(f"baseline-sha256={baseline_digest}")
    print(f"compiler: {compiler}")
    print(f"mutation census: {output_dir / 'mutation-census.tsv'}")
    print(f"runtime controls: {output_dir / 'source-controls.tsv'}")


if __name__ == "__main__":
    main()
