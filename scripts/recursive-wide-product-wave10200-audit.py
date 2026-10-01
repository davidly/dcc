#!/usr/bin/env python3
"""Structural wide-product proof audit; inactive MIR fields are not defects.

The diagnostic hook changes only the exact candidate and restores MIR before
generic fallback. Source near-matches therefore supply the runtime oracle.
With --before-ref, the active comparison-width mutant reproduces a target
stack-check failure where the equivalent narrow-test source returns one.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
from collections import Counter


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "wide_product_audit_helpers",
    ROOT / "scripts/recursive-byte-minimax-wave2600-audit.py",
)
helpers = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = helpers
spec.loader.exec_module(helpers)
helpers.FUNCTION = "wide_product"
helpers.SOURCE = "tests/mir-clobber/rwprod.c"
helpers.TEMPLATE = "recursive-wide-product"
helpers.REJECT_REASON = helpers.re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=recursive-wide-product reject=(?P<reason>\S+)"
)
FUNCTION = helpers.FUNCTION
FIELDS = helpers.MUTABLE_FIELDS
ACTIVE = {
    "opcode": set(range(20)),
    "dst": {1, 4, 5, 8, 14, 15, 17, 18},
    "src1": {5, 6, 9, 15, 16, 18, 19},
    "src2": {5, 15, 18},
    "type": {1, 4, 5, 8, 14, 15, 16, 17, 18},
    "immediate": {1, 4, 5, 8, 14, 15, 16, 18},
    "label": {0, 6, 10},
    "successor0": set(range(20)) - {9, 19},
    "successor1": {6},
    "successor_count": set(range(20)),
    "object": {1},
    "memory_flags": {1, 17},
    "secondary_offset": {5, 15, 16, 17, 18},
    "identity": {1, 17},
}
CONTROLS = (
    ("baseline", (), FUNCTION, True, "argument=10 result=3628800"),
    ("renamed", ("RWPROD_RENAMED",), "wide_product_renamed", True,
     "argument=10 result=3628800"),
    ("base-zero", ("RWPROD_BASE_ZERO",), FUNCTION, True,
     "argument=10 result=0"),
    ("sum", ("RWPROD_SUM",), FUNCTION, True, "argument=10 result=56"),
    ("narrow-test", ("RWPROD_NARROW_TEST",), FUNCTION, False,
     "argument=65536 result=1"),
    ("narrow-call", ("RWPROD_NARROW_CALL",), FUNCTION, False,
     "argument=10 result=4294663936"),
    ("unsigned-return", ("RWPROD_UNSIGNED_RETURN",), FUNCTION, False,
     "argument=10 result=3628800"),
    ("unsigned-parameter", ("RWPROD_UNSIGNED_PARAM",), FUNCTION, False,
     "argument=10 result=3628800"),
    ("extra-cfg", ("RWPROD_EXTRA_CFG",), FUNCTION, False,
     "argument=4294967295 result=7"),
)


def environment(function=FUNCTION):
    # The helper's default argument belongs to its original fixture.
    return helpers.diagnostic_environment(function)


def require_forced(report, function):
    expected = (
        f"MIR cost-selected function={function} "
        f"candidate={helpers.FORCED_CANDIDATE} selector=spilled-scalar-cfg"
    )
    if expected not in report:
        raise RuntimeError(f"forced candidate not selected\n{report}")


def prepare_compiler(directory, before_ref=None, sanitize=False):
    source = directory / "src"
    binary = directory / "bin"
    shutil.copytree(ROOT / "src/dcc", source)
    if before_ref:
        text = helpers.run(
            ["git", "show",
             f"{before_ref}:src/dcc/dcc_mir_machine_float_recursion.c"],
            timeout=30,
        )
        (source / "dcc_mir_machine_float_recursion.c").write_text(text)
    emit = source / "dcc_mir_machine_emit.c"
    text = emit.read_text()
    if text.count(helpers.MUTATION_HOOK_NEEDLE) != 1:
        raise RuntimeError("mutation hook changed")
    emit.write_text(text.replace(
        helpers.MUTATION_HOOK_NEEDLE, helpers.MUTATION_HOOK_REPLACEMENT
    ))
    command = [
        "cmake", "-S", str(source), "-B", str(directory / "cmake"),
        f"-DDCC_RUNTIME_OUTPUT_DIRECTORY={binary}",
        f"-DCMAKE_BUILD_TYPE={'Debug' if sanitize else 'Release'}",
    ]
    if sanitize:
        command += [
            "-DCMAKE_C_FLAGS=-fsanitize=address,undefined "
            "-fno-omit-frame-pointer -fno-pie",
            "-DCMAKE_EXE_LINKER_FLAGS=-no-pie",
        ]
    helpers.run(command, timeout=60)
    helpers.run(
        ["cmake", "--build", str(directory / "cmake"), "--parallel", "4"],
        timeout=600,
    )
    return binary / "dcc"


def compile_report(compiler, path, defines=(), mutation=None, forced=False):
    env = environment()
    if mutation:
        env.update(DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
                   DCC_MIR_MACHINE_MUTATE=mutation)
    if forced:
        env["DCC_MIR_SELECT_CANDIDATE"] = helpers.FORCED_CANDIDATE
    report = helpers.run(
        helpers.compiler_command(compiler, path, defines), env, timeout=30
    )
    return report


def build_target(compiler, directory, defines, function, stack, peep,
                 forced=False, mutation=None, expect_stack_failure=False):
    directory.mkdir(parents=True)
    env = environment(function)
    if forced:
        env["DCC_MIR_SELECT_CANDIDATE"] = helpers.FORCED_CANDIDATE
    if mutation:
        env.update(DCC_MIR_MACHINE_MUTATE_FUNCTION=function,
                   DCC_MIR_MACHINE_MUTATE=mutation)
    command = [
        str(helpers.dccmake_path()), f"dcc-input={helpers.SOURCE}",
        "dcc-output=RWPROD", f"dcc-build-dir={directory}",
        f"dcc-tool={compiler}", f"dcc-peep={str(peep).lower()}",
        f"dcc-stack-check={str(stack).lower()}", "dcc-stack-bytes=512",
    ]
    if defines:
        command += [f"dcc-define={','.join(defines)}"]
    report = helpers.run(command, env, timeout=60)
    target = subprocess.run(
        ["ntvcm", "-p", "-s:0", str(directory / "RWPROD.COM")],
        cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=30, check=False,
    )
    runtime = target.stdout
    (directory / "selection.log").write_text(report)
    (directory / "runtime.log").write_text(runtime)
    if expect_stack_failure:
        if target.returncode == 0 or "?stack overflow:" not in runtime:
            raise RuntimeError(f"expected stack failure: {runtime}")
    elif target.returncode:
        raise RuntimeError(f"target failed: {runtime}")
    return report, runtime


def runtime_controls(compiler, output):
    rows = []
    for name, defines, function, exact, expected in CONTROLS:
        for stack in (True, False):
            for peep in (True, False):
                for forced in (False, True):
                    mode = f"s{int(stack)}p{int(peep)}f{int(forced)}"
                    report, runtime = build_target(
                        compiler, output / "runtime" / name / mode,
                        defines, function, stack, peep, forced,
                    )
                    if exact and not forced:
                        helpers.require_exact(report, function, name)
                        if (f"function={function} template=recursive-wide-product "
                                "accept=emitted") not in report:
                            raise RuntimeError(f"wrong exact matcher: {name}")
                    else:
                        helpers.require_generic(report, function, name)
                    if forced:
                        require_forced(report, function)
                    if "wide product failures=0 " + expected not in runtime:
                        raise RuntimeError(f"{name}/{mode}: {runtime}")
                    rows.append((name, "runtime", ",".join(defines),
                                 "generic" if forced or not exact else "exact",
                                 helpers.selection_from(report, function),
                                 helpers.reject_reason_from(report, function),
                                 mode))
    return rows


def mutation_case(compiler, directory, baseline, instruction, field, value):
    name = f"i{instruction}-{field}"
    mutation = f"{instruction}:{field}:{value}"
    path = directory / f"{name}.MAC"
    report = compile_report(compiler, path, mutation=mutation)
    accepted = helpers.selection_from(report, FUNCTION) == "scheduled-machine-cfg"
    active = instruction in ACTIVE.get(field, set())
    if accepted and active:
        raise RuntimeError(f"active mutation survived: {mutation}\n{report}")
    if accepted and path.read_bytes() != baseline:
        raise RuntimeError(f"inactive mutation changed assembly: {mutation}")
    if not accepted:
        helpers.require_generic(report, FUNCTION, mutation)
        forced = compile_report(compiler, path, mutation=mutation, forced=True)
        helpers.require_generic(forced, FUNCTION, mutation)
        require_forced(forced, FUNCTION)
        if helpers.reject_reason_from(forced, FUNCTION) != \
                helpers.reject_reason_from(report, FUNCTION):
            raise RuntimeError(f"forced rejection changed: {mutation}")
    path.unlink(missing_ok=True)
    return (name, "mutation", mutation,
            "inactive" if accepted else "rejected",
            helpers.selection_from(report, FUNCTION),
            helpers.reject_reason_from(report, FUNCTION), f"active={int(active)}")


def reproduction(before, current, output):
    base = output / "before.MAC"
    report = compile_report(before, base)
    helpers.require_exact(report, FUNCTION, "before baseline")
    mutant = output / "before-width-mutant.MAC"
    report = compile_report(before, mutant, mutation="5:secondary_offset:2")
    helpers.require_exact(report, FUNCTION, "before active width mutant")
    (output / "before-width-mutant.log").write_text(report)
    # The candidate hook restores MIR; an accepted active width mutation emits
    # the unchanged wide test instead of the narrow comparison's semantics.
    if base.read_bytes() != mutant.read_bytes():
        raise RuntimeError("unexpected before mutant assembly change")
    after = output / "after.MAC"
    helpers.require_exact(compile_report(current, after), FUNCTION, "after")
    if base.read_bytes() != after.read_bytes():
        raise RuntimeError("valid baseline assembly regressed")
    report = compile_report(
        current, output / "after-width-mutant.MAC",
        mutation="5:secondary_offset:2",
    )
    helpers.require_generic(report, FUNCTION, "after active width mutant")
    (output / "after-width-mutant.log").write_text(report)
    report, runtime = build_target(
        before, output / "before-target-width-mutant",
        ("RWPROD_HIGH_ARGUMENT",), FUNCTION, True, False,
        mutation="5:secondary_offset:2", expect_stack_failure=True,
    )
    helpers.require_exact(report, FUNCTION, "before target width mutant")
    if "wide product failures=0 argument=65536 result=1" in runtime:
        raise RuntimeError("active width mutant unexpectedly preserved semantics")
    if "stack" not in runtime.lower():
        raise RuntimeError(f"expected bounded stack-check failure: {runtime}")
    report, runtime = build_target(
        current, output / "after-target-narrow-source",
        ("RWPROD_NARROW_TEST",), FUNCTION, True, False,
    )
    helpers.require_generic(report, FUNCTION, "source narrow oracle")
    if "wide product failures=0 argument=65536 result=1" not in runtime:
        raise RuntimeError(runtime)
    print("before: active width mutant accepted, unchanged wide-test assembly, "
          "target stack-check failure; narrow source oracle returns 1")
    print("after: active width mutant rejected; valid assembly byte-identical")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", default="build/wideproduct-wave10200")
    parser.add_argument("--before-ref")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be 1..4")
    output = (ROOT / args.output_dir).resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("--output-dir must be under this worktree's build directory")
    if output.exists():
        parser.error("use a new output directory to preserve previous evidence")
    output.mkdir(parents=True)
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1"
    compiler = prepare_compiler(output / "current", sanitize=args.sanitize)
    if args.before_ref:
        before = prepare_compiler(output / "before", args.before_ref)
        reproduction(before, compiler, output)
    rows = runtime_controls(compiler, output)
    helpers.write_tsv(output / "runtime-controls.tsv", rows)
    work = output / "mutations"
    work.mkdir()
    baseline_path = output / "mutation-baseline.MAC"
    helpers.require_exact(
        compile_report(compiler, baseline_path), FUNCTION, "mutation baseline"
    )
    baseline = baseline_path.read_bytes()
    cases = [(i, field, value) for i in range(20) for field, value in FIELDS]
    cases += [(i, "identity", 120) for i in (1, 17)]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as executor:
        mutations = list(executor.map(
            lambda case: mutation_case(compiler, work, baseline, *case), cases
        ))
    helpers.write_tsv(output / "mutation-census.tsv", mutations)
    work.rmdir()
    digest = hashlib.sha256((output / "runtime/baseline/s1p0f0/RWPROD.MAC")
                            .read_bytes()).hexdigest()
    print(f"mutations={len(cases)} outcomes={dict(Counter(r[3] for r in mutations))}")
    print(f"runtime-controls={len(rows)} active-survivors=0 "
          f"baseline-sha256={digest}")
    print(f"evidence={output}")


if __name__ == "__main__":
    main()
