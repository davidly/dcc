#!/usr/bin/env python3
"""Audit the retained ctype/realloc exact schedule semantically."""

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
FUNCTION = "crw7_exact"
SOURCE = "tests/mir-clobber/ctyper7.c"
EXACT_ACCEPT = (
    "MIR machine function=crw7_exact "
    "template=ctype-realloc-schedule accept=emitted"
)
EXACT_SELECTION = (
    "MIR selection function=crw7_exact "
    "selector=scheduled-machine-cfg result=mir"
)
GENERIC_SELECTION = re.compile(
    r"MIR selection function=crw7_exact "
    r"selector=(?P<selector>homed-scalar-cfg|"
    r"hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|"
    r"spilled-scalar-cfg) result=mir"
)
REJECT_REASON = re.compile(
    r"MIR machine function=crw7_exact "
    r"template=ctype-realloc-schedule reject=(?P<reason>\S+)"
)
BASELINE_SHA256 = (
    "851880c0ac687d0491a756fd4474075835d5035ffd91214275e5ea343715c3c4"
)
BASELINE_SELECTED_HASH = "c2afdec0"
BASELINE_RUNTIME = (
    "ctype/realloc ok",
    "CRW7 oracle failures=0 ctype=14 hash=62753",
    "alloc=3/1 resize=3/1 free=2 live=0",
    "width=32773,32779 bounds=1",
)
EXPECTED_MUTATION_OUTCOMES = Counter(rejected=18)


@dataclass(frozen=True)
class SourceControl:
    name: str
    output_name: str
    defines: tuple[str, ...] = ()
    expected_runtime: tuple[str, ...] = BASELINE_RUNTIME
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
    SourceControl("baseline", "CRW48OK", expect_exact=True,
                  expected_selector="scheduled-machine-cfg"),
    SourceControl(
        "alloc-fixed-print",
        "CRW48AF",
        ("CRW48_FIXED_PRINT", "CRW48_SOURCE_ALLOC_FIXED_PRINT"),
        expected_reject="allocation-failure",
    ),
    SourceControl(
        "grow-fixed-print",
        "CRW48GF",
        ("CRW48_FIXED_PRINT", "CRW48_SOURCE_GROW_FIXED_PRINT"),
        expected_reject="grow-failure",
    ),
    SourceControl(
        "grow-alias-resize",
        "CRW48GR",
        ("CRW48_ALIAS_RESIZE", "CRW48_SOURCE_GROW_ALIAS_RESIZE"),
        expected_reject="shrink",
    ),
    SourceControl(
        "compare-variadic",
        "CRW48CV",
        ("CRW48_COMPARE_VARIADIC", "CRW48_SOURCE_COMPARE_VARIADIC"),
        expected_reject="preserve",
    ),
    SourceControl(
        "byte-check-alias",
        "CRW48BC",
        ("CRW48_ALIAS_CHECK", "CRW48_SOURCE_BYTE_CHECK_ALIAS"),
        expected_reject="byte-check",
    ),
    SourceControl(
        "success-fixed-print",
        "CRW48SF",
        ("CRW48_FIXED_PRINT", "CRW48_SOURCE_SUCCESS_FIXED_PRINT"),
        expected_reject="final",
    ),
)
MUTATION_CASES = (
    MutationCase(
        "pointer-store-identity",
        "133:identity:88",
        ("CRW48_ALIAS_POINTER_GLOBAL",),
        expected_reject="allocation",
    ),
    MutationCase(
        "allocation-pointer-cast-op",
        "132:immediate:1",
        expected_reject="allocation",
    ),
    MutationCase(
        "allocation-failure-arg-index",
        "138:immediate:1",
        expected_reject="allocation-failure",
    ),
    MutationCase(
        "copy-load-identity",
        "144:identity:88",
        ("CRW48_ALIAS_POINTER_GLOBAL",),
        expected_reject="copy",
    ),
    MutationCase(
        "copy-string-arg-source",
        "147:src1:94",
        expected_reject="copy",
    ),
    MutationCase(
        "grow-pointer-cast-op",
        "157:immediate:1",
        expected_reject="grow",
    ),
    MutationCase(
        "grow-failure-arg-index",
        "163:immediate:1",
        expected_reject="grow-failure",
    ),
    MutationCase(
        "grow-resize-identity",
        "155:identity:88",
        ("CRW48_ALIAS_RESIZE",),
        expected_reject="shrink",
    ),
    MutationCase(
        "preserve-compare-arg-source",
        "172:src1:109",
        expected_reject="preserve",
    ),
    MutationCase(
        "preserve-check-identity",
        "179:identity:88",
        ("CRW48_ALIAS_CHECK",),
        expected_reject="preserve",
    ),
    MutationCase(
        "byte-store-stride",
        "182:immediate:2",
        expected_reject="byte-store",
    ),
    MutationCase(
        "shrink-pointer-cast-op",
        "200:immediate:1",
        expected_reject="shrink",
    ),
    MutationCase(
        "byte-check-stride",
        "214:immediate:2",
        expected_reject="byte-check",
    ),
    MutationCase(
        "byte-check-unary-op",
        "217:immediate:1",
        expected_reject="byte-check",
    ),
    MutationCase(
        "byte-check-identity",
        "222:identity:88",
        ("CRW48_ALIAS_CHECK",),
        expected_reject="byte-check",
    ),
    MutationCase(
        "free-arg-index",
        "236:immediate:1",
        expected_reject="free",
    ),
    MutationCase(
        "final-failure-arg-index",
        "243:immediate:0",
        expected_reject="final",
    ),
    MutationCase(
        "final-success-const",
        "252:immediate:1",
        expected_reject="final",
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
        str(compiler), "-fstack-check", "-stack", "512", "-c", "-I", ".",
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
        DCC_MIR_MACHINE_TEMPLATE="ctype-realloc-schedule",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
    )
    baseline_path = output_dir / "baseline.MAC"
    report = run(compiler_command(compiler, baseline_path), env)
    if EXACT_ACCEPT not in report or EXACT_SELECTION not in report:
        raise RuntimeError(
            "baseline did not select ctype-realloc-schedule\n" + report
        )
    if f"selected-hash={BASELINE_SELECTED_HASH}" not in report:
        raise RuntimeError(
            "ctype-realloc baseline selected hash changed\n" + report
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
        DCC_MIR_MACHINE_TEMPLATE="ctype-realloc-schedule",
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
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
                f"{control.name} reject {reject_reason!r}, "
                f"expected {control.expected_reject!r}\n{build_output}"
            )
    runtime = run(
        ["ntvcm", "-p", "-s:0", str(build_dir / f"{control.output_name}.COM")],
        timeout=30,
    )
    for text in control.expected_runtime:
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
        DCC_MIR_MACHINE_TEMPLATE="ctype-realloc-schedule",
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=case.spec,
        DCC_MIR_MACHINE_REPORT="1",
        DCC_MIR_SELECT_REPORT="1",
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
            f"{case.name} reject {reject_reason!r}, "
            f"expected {case.expected_reject!r}\n{report}"
        )
    return (
        case.name,
        "mutation",
        case.spec,
        "rejected",
        selector,
        reject_reason,
        ",".join(case.defines) or "-",
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
            results = [future.result()
                       for future in concurrent.futures.as_completed(futures)]
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)
    return sorted(results)


def write_tsv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(
            ("name", "kind", "spec", "outcome", "selector",
             "reject_reason", "mode")
        )
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--output-dir",
        default="build/ctype-realloc-wave48-audit",
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
    source_rows = run_source_controls(compiler, dccmake, output_dir)
    mutation_rows = run_mutation_cases(compiler, output_dir, args.jobs)
    write_tsv(output_dir / "source-controls.tsv", source_rows)
    write_tsv(output_dir / "mutation-census.tsv", mutation_rows)

    outcomes = Counter(row[3] for row in mutation_rows)
    if outcomes != EXPECTED_MUTATION_OUTCOMES:
        raise RuntimeError(
            f"unexpected mutation outcomes: {outcomes} "
            f"!= {EXPECTED_MUTATION_OUTCOMES}"
        )
    print(
        f"ctype-realloc Wave 48 mutations={len(mutation_rows)} {outcomes}"
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
