#!/usr/bin/env python3
"""Audit recursive frame-fill legality, inactive metadata, and target controls.

The signed-char/128 source was falsely selected before this proof: the exact
schedule recursed and hit the stack guard, whereas generic MIR never reached
the recursive call. That source eventually indexes outside its array, so it
is a range-proof witness, not a defined-behavior runtime oracle. Bounded forms
below supply defined-behavior runtime controls in both stack modes.

With --reference-compiler, an active MIR_STORE displacement mutation (+2 at
instruction 35) demonstrates old false acceptance independently of that
source limitation. Inactive survivors must retain byte-identical assembly.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts/direct-byte-sum-loop-wave9700-audit.py"
spec = importlib.util.spec_from_file_location("framefill_audit_helpers", HELPER)
assert spec and spec.loader
common = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = common
spec.loader.exec_module(common)
common.FUNCTION = "fill_frame"
common.diagnostic_environment.__defaults__ = (common.FUNCTION,)
common.TEMPLATE = "recursive-frame-fill"
common.SOURCE = "tests/mir-clobber/framefil.c"
common.BASELINE_SHA256 = ""
common.BASELINE_SELECTED_HASH = ""
common.REJECT_REASON = common.re.compile(
    r"MIR machine function=(?P<function>\S+) "
    r"template=recursive-frame-fill reject=(?P<reason>\S+)"
)

OPCODES = (
    "label param nop const store label nop phi nop const unary binary branch "
    "address nop index nop nop unary binary storeind label nop const binary "
    "store jump label address nop const binary index load nop store nop const "
    "binary arg call address const index load binary return"
).split()
FIELDS = common.MUTABLE_FIELDS + (("narrowed_for_counter_update", 7),)
SEMANTIC_FIELDS = {
    "label": {"label"},
    "param": {"dst", "type", "immediate", "object", "name_identity"},
    "nop": set(),
    "const": {"dst", "type", "immediate", "memory_flags"},
    "store": {
        "src1", "type", "immediate", "object", "name_identity", "memory_size",
        "memory_flags",
    },
    "phi": {
        "dst", "src1", "src2", "type", "phi_pred1", "phi_pred2", "object",
        "name_identity",
    },
    "unary": {"dst", "src1", "type", "immediate"},
    "binary": {"dst", "src1", "src2", "type", "immediate", "secondary_offset"},
    "branch": {"src1", "label", "successor1"},
    "jump": {"label"},
    "address": {"dst", "type", "immediate", "object", "name_identity"},
    "index": {"dst", "src1", "src2", "type", "immediate", "memory_size"},
    "storeind": {"src1", "src2", "type", "memory_size", "memory_flags"},
    "load": {"dst", "src1", "type", "memory_size", "memory_flags"},
    "arg": {"src1", "type", "immediate", "secondary_offset"},
    "call": {"dst", "type", "name_identity", "base_identity", "secondary_offset"},
    "return": {"src1"},
}
COMMON_ACTIVE = {"opcode", "successor0", "successor_count"}
CONTROL_DEFINES = (
    ("baseline", (), "fill_frame", True),
    ("renamed", ("FF_RENAMED",), "renamed_frame", True),
    ("old-style", ("FF_KNR",), "fill_frame", True),
    ("signed-small", ("FF_SIGNED",), "fill_frame", True),
    ("unsigned-large", ("FF_UNSIGNED", "FF_LARGE"), "fill_frame", True),
    ("signed-large", ("FF_SIGNED", "FF_LARGE"), "fill_frame", False),
    ("volatile", ("FF_VOLATILE",), "fill_frame", False),
    ("subtract", ("FF_SUBTRACT",), "fill_frame", False),
    ("return-next", ("FF_RETURN_NEXT",), "fill_frame", False),
    ("shifted-sink", ("FF_SHIFTED_SINK",), "fill_frame", False),
)


def prepare_compiler(output, sanitize):
    source = output / "compiler-src"
    build = output / "compiler-build"
    binary = output / "compiler-bin"
    shutil.copytree(ROOT / "src/dcc", source)
    hook = source / "dcc_mir_machine_emit.c"
    text = hook.read_text()
    if text.count(common.MUTATION_HOOK_NEEDLE) != 1:
        raise RuntimeError("diagnostic mutation hook changed")
    replacement = common.MUTATION_HOOK_REPLACEMENT + (
        '    else if (!strcmp(field, "narrowed_for_counter_update"))\n'
        "        insn->narrowed_for_counter_update = (int)value;\n"
    )
    hook.write_text(text.replace(common.MUTATION_HOOK_NEEDLE, replacement))
    command = [
        "cmake", "-S", str(source), "-B", str(build),
        f"-DDCC_RUNTIME_OUTPUT_DIRECTORY={binary}",
        f"-DCMAKE_BUILD_TYPE={'Debug' if sanitize else 'Release'}",
    ]
    if sanitize:
        command.extend([
            "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie",
            "-DCMAKE_EXE_LINKER_FLAGS=-no-pie",
        ])
    common.run(command, timeout=180)
    common.run(
        ["cmake", "--build", str(build), "--parallel", "4", "--target", "dcc"],
        timeout=600,
    )
    return binary / "dcc"


def source_controls(compiler, output, reference_compiler=None):
    rows = []
    for name, defines, function, exact in CONTROL_DEFINES:
        for stack in (True, False):
            command = common.compiler_command(
                compiler, output / f"{name}-{int(stack)}.MAC", defines
            )
            if not stack:
                command.remove("-fstack-check")
            report = common.run(command, common.diagnostic_environment(function))
            (output / f"{name}-{int(stack)}.log").write_text(report)
            if exact:
                common.require_exact(report, function, name)
                if reference_compiler:
                    old_output = output / f"{name}-{int(stack)}-reference.MAC"
                    old_command = common.compiler_command(
                        reference_compiler.resolve(), old_output, defines
                    )
                    if not stack:
                        old_command.remove("-fstack-check")
                    old_report = common.run(
                        old_command, common.diagnostic_environment(function)
                    )
                    common.require_exact(old_report, function, f"reference {name}")
                    if old_output.read_bytes() != \
                            (output / f"{name}-{int(stack)}.MAC").read_bytes():
                        raise RuntimeError(f"valid {name} assembly changed")
            else:
                common.require_generic(report, function, name)
            rows.append((name, stack, common.selection_from(report, function)))
    common.forced_fallback_control(compiler, output)
    return rows


def runtime_control(compiler, output, name, defines, bounded, forced=False):
    rows = []
    for stack in ((True, False) if bounded else (True,)):
        for peep in (True, False):
            directory = output / "runtime" / f"{name}-{int(stack)}-{int(peep)}"
            directory.mkdir(parents=True)
            source = directory / "FRAMEFIL.C"
            shutil.copy2(ROOT / common.SOURCE, source)
            environment = common.diagnostic_environment(
                "renamed_frame" if "FF_RENAMED" in defines else "fill_frame"
            )
            if forced:
                environment["DCC_MIR_SELECT_CANDIDATE"] = common.FORCED_CANDIDATE
            command = [
                str(common.dccmake_path()), str(source), "dcc-output=FRAMEFIL",
                f"dcc-build-dir={directory}", f"dcc-tool={compiler}",
                f"dcc-peep={str(peep).lower()}",
                f"dcc-stack-check={str(stack).lower()}", "dcc-stack-bytes=512",
            ]
            if defines:
                command.append(f"dcc-define={','.join(defines)}")
            report = common.run(command, environment, timeout=90)
            function = environment["DCC_MIR_SELECT_FUNCTION"]
            if bounded or forced:
                common.require_generic(report, function, name)
            else:
                common.require_exact(report, function, name)
            completed = subprocess.run(
                ["ntvcm", "-p", "-s:0", str(directory / "FRAMEFIL.COM")],
                cwd=ROOT, text=True, capture_output=True, timeout=15,
            )
            runtime = completed.stdout + completed.stderr
            if completed.returncode != (0 if bounded else 255):
                raise RuntimeError(f"{name} exit={completed.returncode}\n{runtime}")
            expected = "frame fill failures=0" if bounded else "?stack overflow"
            if expected not in runtime or "unexpected return" in runtime:
                raise RuntimeError(f"{name} unexpected runtime\n{runtime}")
            (directory / "build.log").write_text(report)
            (directory / "runtime.log").write_text(runtime)
            rows.append((name, stack, peep, expected))
    return rows


def mutation_census(compiler, output, jobs):
    baseline = (output / "baseline.MAC").read_bytes()

    def run_mutation(compiler, work_dir, case):
        assembly = work_dir / f"{case.name}.MAC"
        environment = common.diagnostic_environment()
        environment.update(
            DCC_MIR_MACHINE_MUTATE_FUNCTION=common.FUNCTION,
            DCC_MIR_MACHINE_MUTATE=case.spec,
        )
        environment.pop("DCC_MIR_COST_REPORT", None)
        report = common.run(common.compiler_command(compiler, assembly), environment)
        selector = common.selection_from(report, common.FUNCTION)
        if selector is None:
            raise RuntimeError(f"{case.name} missing selection\n{report}")
        outcome = "accepted" if selector == "scheduled-machine-cfg" else "rejected"
        if outcome == "accepted" and assembly.read_bytes() != baseline:
            raise RuntimeError(f"{case.name} changed the accepted machine stream")
        assembly.unlink()
        return (
            case.name, case.spec, outcome, selector,
            common.reject_reason_from(report, common.FUNCTION) or "",
        )

    common.run_mutation = run_mutation
    common.MUTATION_CASES = tuple(
        common.MutationCase(
            f"instruction-{instruction}-{field}",
            f"{instruction}:{field}:{value}",
        )
        for instruction in range(len(OPCODES))
        for field, value in FIELDS
    )
    rows = common.run_mutation_cases(compiler, output, jobs)
    classified = []
    meaningful = []
    for name, mutation, outcome, selector, reason in rows:
        instruction, field, _ = mutation.split(":")
        opcode = OPCODES[int(instruction)]
        active = field in SEMANTIC_FIELDS[opcode] | COMMON_ACTIVE
        # Return has no live successor; its unused successor0 is inert.
        if opcode == "return" and field == "successor0":
            active = False
        category = "semantic" if active else "inactive-or-proven-equivalent"
        classified.append((name, mutation, category, outcome, selector, reason))
        if active and outcome == "accepted":
            meaningful.append(mutation)
    common.write_tsv(
        output / "mutation-census.tsv",
        ("name", "mutation", "category", "outcome", "selector", "reject_reason"),
        classified,
    )
    print(f"mutations={len(rows)} outcomes={Counter(row[2] for row in rows)}")
    print(f"semantic survivors={meaningful}")
    if meaningful:
        raise RuntimeError("unproved semantic mutations accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default="build/framefill-wave10300-audit")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--reference-compiler", type=Path)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4")
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1"
    output = (ROOT / args.output_dir).resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("output must be under this worktree's build/")
    if output.exists():
        parser.error("output directory exists; preserve artifacts and use a new path")
    output.mkdir(parents=True)
    compiler = prepare_compiler(output, args.sanitize)
    digest = common.baseline_compile(compiler, output)
    if args.reference_compiler:
        reference = output / "reference.MAC"
        common.run(
            common.compiler_command(args.reference_compiler.resolve(), reference),
            common.diagnostic_environment(),
        )
        if reference.read_bytes() != (output / "baseline.MAC").read_bytes():
            raise RuntimeError("valid baseline raw assembly changed")
        for stack in (True, False):
            command = common.compiler_command(
                args.reference_compiler.resolve(),
                output / f"reference-{int(stack)}.MAC",
            )
            current_command = common.compiler_command(
                compiler, output / f"current-{int(stack)}.MAC",
            )
            if not stack:
                command.remove("-fstack-check")
                current_command.remove("-fstack-check")
            common.run(command, common.diagnostic_environment())
            common.run(current_command, common.diagnostic_environment())
            if (output / f"reference-{int(stack)}.MAC").read_bytes() != \
                    (output / f"current-{int(stack)}.MAC").read_bytes():
                raise RuntimeError("valid stack/no-stack assembly changed")
        environment = common.diagnostic_environment()
        environment.update(
            DCC_MIR_MACHINE_MUTATE_FUNCTION=common.FUNCTION,
            DCC_MIR_MACHINE_MUTATE="35:immediate:2",
        )
        before = common.run(
            common.compiler_command(
                args.reference_compiler.resolve(), output / "offset-before.MAC"
            ), environment,
        )
        after = common.run(
            common.compiler_command(compiler, output / "offset-after.MAC"),
            environment,
        )
        common.require_exact(before, common.FUNCTION, "old active offset acceptance")
        common.require_generic(after, common.FUNCTION, "active offset rejection")
        (output / "offset-before.log").write_text(before)
        (output / "offset-after.log").write_text(after)
    sources = source_controls(compiler, output, args.reference_compiler)
    runtime = []
    for name, defines, _, exact in CONTROL_DEFINES:
        if exact and name != "unsigned-large":
            runtime += runtime_control(compiler, output, name, defines, False)
        if name not in ("signed-large", "unsigned-large"):
            runtime += runtime_control(
                compiler, output, f"bounded-{name}",
                defines + ("FF_BOUNDED",), True,
            )
    runtime += runtime_control(compiler, output, "forced", (), False, True)
    common.write_tsv(output / "source-controls.tsv", ("name", "stack", "selector"), sources)
    common.write_tsv(output / "runtime-controls.tsv", ("name", "stack", "peep", "expected"), runtime)
    mutation_census(compiler, output, args.jobs)
    print(f"source controls={len(sources)} runtime controls={len(runtime)}")
    print(f"baseline-sha256={digest}; forced generic fallback verified")
    print(f"artifacts={output}")


if __name__ == "__main__":
    main()
