#!/usr/bin/env python3
"""Structural status-pack proof census and exhaustive CP/M truth-table controls.

The existing mutation hook affects only the exact candidate and then restores
MIR. A separate, copied-compiler oracle changes the conversion *before*
verification so the generic runtime really evaluates the changed semantics.
Inactive fields are retained deliberately; rejecting stale NOP payloads is not
a semantic proof. No production fingerprint is used.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import replace
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "status_pack_audit_helpers",
    ROOT / "scripts/raw-conversion-check-wave8000-audit.py",
)
helpers = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = helpers
spec.loader.exec_module(helpers)
helpers.ROOT = ROOT
helpers.FUNCTION = "pack_status"
helpers.SOURCE = "tests/mir-clobber/stpack.c"
helpers.TEMPLATE = "status-pack"
helpers.BASELINE_SHA256 = None
helpers.BASELINE_SELECTED_HASH = None
helpers.EXPECTED_RUNTIME = ("status pack checks=64 failures=0",)
helpers.SOURCE_CONTROLS = (
    helpers.SourceControl("baseline", "STPBASE", expect_exact=True),
    helpers.SourceControl(
        "renamed", "STPNAME", ("STATUS_RENAMED",),
        "renamed_pack_status", expect_exact=True,
    ),
    helpers.SourceControl(
        "unsigned-mask", "STPUNSIG", ("STATUS_UNSIGNED_MASK",),
        expect_exact=True,
    ),
    helpers.SourceControl("bool-cast", "STPBOOL", ("STATUS_BOOL_CAST",)),
    helpers.SourceControl("mask", "STPMASK", ("STATUS_MASK",)),
    helpers.SourceControl("bitfields", "STPBITS", ("STATUS_BITFIELDS",)),
    helpers.SourceControl("volatile", "STPVOL", ("STATUS_VOLATILE",)),
    helpers.SourceControl(
        "local-volatile", "STPLVOL", ("STATUS_LOCAL_VOLATILE",),
    ),
    helpers.SourceControl("fastcall", "STPFAST", ("STATUS_FASTCALL",)),
)
helpers.SOURCE_CONTROLS = tuple(
    replace(control, function="pack_status")
    if control.function == "raw_conversion_check_fixture" else control
    for control in helpers.SOURCE_CONTROLS
)
original_environment = helpers.diagnostic_environment


def diagnostic_environment(function="pack_status"):
    return original_environment(function)


helpers.diagnostic_environment = diagnostic_environment


def run(command, env=None, timeout=180):
    command = list(command)
    if command[:2] == ["cmake", "--build"]:
        command.insert(command.index("--parallel") + 1, "4")
    return original_run(command, env, timeout)


original_run = helpers.run
helpers.run = run
original_prepare = helpers.prepare_mutation_compiler


def prepare_compiler(output_dir, parent_ref=None):
    # Reuse the comprehensive diagnostic-field hook without changing production.
    compiler = original_prepare(output_dir)
    source = output_dir / "mutation-compiler-src"
    if parent_ref:
        original = run([
            "git", "show",
            f"{parent_ref}:src/dcc/dcc_mir_machine_float_recursion.c",
        ], timeout=20)
        (source / "dcc_mir_machine_float_recursion.c").write_text(original)
    path = source / "dcc_mir_select.c"
    text = path.read_text()
    needle = "    verified = mir_verify_and_dump();"
    if text.count(needle) != 1:
        raise RuntimeError("oracle insertion point changed")
    path.write_text(text.replace(needle, """\
    if (getenv("STATUS_PACK_SEMANTIC_ORACLE") != NULL &&
        !strcmp(mir.name, "pack_status") && mir.count == 79)
        mir.insns[10].type = TYPE_BOOL;
""" + needle))
    path = source / "dcc_mir_machine_float_recursion.c"
    text = path.read_text()
    needle = "static int mir_match_status_pack(struct MirStatusPack *plan)\n{"
    if text.count(needle) != 1:
        raise RuntimeError("inventory insertion point changed")
    path.write_text(text.replace(needle, needle + """
    if (getenv("STATUS_PACK_INVENTORY") != NULL &&
        !strcmp(mir.name, "pack_status") && mir.count == 79) {
        int i;
        for (i = 0; i < mir.count; ++i) {
            const struct MirInsn *p = &mir.insns[i];
            fprintf(stderr, "STATUS-INSN %d op=%d dst=%d src1=%d src2=%d "
                    "type=%d immediate=%ld size=%d flags=%d off2=%d "
                    "inline=%d object=%d succ=%d,%d,%d name=%s base=%s\\n",
                    i, p->opcode, p->dst, p->src1, p->src2, p->type,
                    p->immediate, p->memory_size, p->memory_flags,
                    p->secondary_offset, p->inline_temp_id, p->object,
                    p->successor_count, p->successors[0], p->successors[1],
                    p->name, p->base_name);
        }
    }
"""))
    run([
        "cmake", "--build", str(output_dir / "mutation-compiler-build"),
        "--parallel", "--target", "dcc",
    ], timeout=600)
    return compiler


def semantic_runtime(compiler, output_dir, oracle, parent=False):
    environment = helpers.diagnostic_environment()
    if oracle:
        environment["STATUS_PACK_SEMANTIC_ORACLE"] = "1"
        environment["DCC_MIR_SELECT_CANDIDATE"] = "spilled-phi-slot"
    else:
        environment["DCC_MIR_MACHINE_MUTATE_FUNCTION"] = "pack_status"
        environment["DCC_MIR_MACHINE_MUTATE"] = "10:type:6"
    directory = output_dir / ("semantic-oracle" if oracle else "semantic-probe")
    report = run([
        str(helpers.dccmake_path()),
        f"dcc-input={helpers.SOURCE}", "dcc-output=STPSEM",
        f"dcc-build-dir={directory}", f"dcc-tool={compiler}",
        "dcc-peep=false", "dcc-stack-check=true", "dcc-stack-bytes=512",
        "dcc-define=STATUS_EXPECT_BOOLEAN",
    ], environment, timeout=300)
    completed = subprocess.run(
        ["ntvcm", "-p", "-s:0", str(directory / "STPSEM.COM")],
        cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=30, check=False,
    )
    runtime = completed.stdout
    if completed.returncode != (0 if oracle else 1):
        raise RuntimeError("unexpected semantic runtime exit\n" + runtime)
    (directory / "selection.log").write_text(report)
    (directory / "runtime.log").write_text(runtime)
    if oracle:
        helpers.require_generic(report, "pack_status", "semantic oracle")
    else:
        if parent:
            helpers.require_exact(report, "pack_status", "parent boolean mutation")
        else:
            helpers.require_generic(report, "pack_status", "boolean mutation")
        # Candidate-scoped mutations are restored for the generic fallback.
        if "status pack checks=64 failures=32" not in runtime:
            raise RuntimeError("restored generic probe changed\n" + runtime)
        return
    if "status pack checks=64 failures=0" not in runtime:
        raise RuntimeError("semantic generic oracle failed\n" + runtime)


def expected_mutation(instruction, field):
    roots = set(range(4, 65, 12))
    members = set(range(5, 66, 12))
    indirect = set(range(6, 67, 12))
    branches = set(range(7, 68, 12))
    constants = {2} | set(range(9, 70, 12))
    sources = set(range(10, 71, 12))
    binary = set(range(11, 72, 12))
    conversions = set(range(12, 73, 12))
    stores = {3} | set(range(14, 75, 12))
    loads = {76} | set(range(20, 69, 12))
    labels = {0} | set(range(15, 76, 12))
    values = roots | members | indirect | constants | sources | binary
    values |= conversions | loads | {78}
    active = {
        "opcode": set(range(79)),
        "dst": values,
        "src1": members | indirect | branches | sources | binary
                | conversions | stores | {77},
        "src2": binary,
        "type": values | stores | {77},
        "immediate": roots | members | constants | sources | binary
                     | conversions | stores | loads | {77},
        "label": labels | branches,
        "successor0": set(range(78)),
        "successor1": branches,
        "successor_count": set(range(79)),
        "memory_size": members | indirect | stores | loads,
        "memory_flags": roots | members | indirect | stores | loads | {78},
        "pointee_volatile_mask": roots | members,
        "bit_width": members | indirect | stores | loads,
        "secondary_offset": binary | {77, 78},
        "inline_temp_id": stores | loads,
        "identity": roots | stores | loads | {78},
    }
    if field == "object":
        return (
            "equivalent-name-resolution"
            if instruction in roots | stores | loads else "inactive",
            "survived",
        )
    if field == "type" and instruction in roots:
        # This census value is the original fixture's struct-pointer type.
        return "unchanged", "survived"
    if instruction in active.get(field, set()):
        return "active", "rejected"
    return "inactive", "survived"


def run_mutation(compiler, work_dir, case):
    output = work_dir / f"{case.name}.MAC"
    environment = helpers.diagnostic_environment()
    environment.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION="pack_status",
        DCC_MIR_MACHINE_MUTATE=case.spec,
    )
    report = run(helpers.compiler_command(compiler, output), environment)
    selector = helpers.selection_from(report, "pack_status")
    outcome = "survived" if selector == "scheduled-machine-cfg" else "rejected"
    if selector is None:
        raise RuntimeError("mutation omitted selection report: " + case.spec)
    if outcome == "survived":
        actual_hash = helpers.selected_hash(report, "pack_status")
        if actual_hash != helpers.BASELINE_SELECTED_HASH:
            raise RuntimeError("accepted mutation changed exact code: " + case.spec)
    output.unlink(missing_ok=True)
    return case.name, case.spec, outcome, selector, ""


helpers.run_mutation = run_mutation


def forced_runtime(compiler, output):
    saved = helpers.diagnostic_environment

    def forced_environment(function):
        result = saved(function)
        result["DCC_MIR_SELECT_CANDIDATE"] = "spilled-phi-slot"
        return result

    helpers.diagnostic_environment = forced_environment
    try:
        control = helpers.SourceControl(
            "forced-generic", "STPGEN", function="pack_status",
        )
        return [
            helpers.run_source_control(
                compiler, helpers.dccmake_path(), output, control, stack, peep,
            )
            for stack in (True, False) for peep in (True, False)
        ]
    finally:
        helpers.diagnostic_environment = saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", default="build/status-pack-wave10100-audit")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument(
        "--reproduce-parent", metavar="REF",
        help="reproduce only the parent false acceptance and semantic oracle",
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4")
    output = (ROOT / args.output_dir).resolve()
    if output == ROOT / "build" or not output.is_relative_to(ROOT / "build"):
        parser.error("output directory must be under build/")
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    compiler = prepare_compiler(output, args.reproduce_parent)
    environment = helpers.diagnostic_environment()
    environment["STATUS_PACK_INVENTORY"] = "1"
    inventory = run(
        helpers.compiler_command(compiler, output / "inventory.MAC"),
        environment,
    )
    (output / "inventory.log").write_text(inventory)
    if args.inventory_only:
        print(inventory)
        return
    if args.reproduce_parent:
        semantic_runtime(compiler, output, True, parent=True)
        semantic_runtime(compiler, output, False, parent=True)
        saved = helpers.diagnostic_environment

        def inactive_environment(function):
            result = saved(function)
            result.update(
                DCC_MIR_MACHINE_MUTATE_FUNCTION="pack_status",
                DCC_MIR_MACHINE_MUTATE="8:type:400",
            )
            return result

        helpers.diagnostic_environment = inactive_environment
        try:
            helpers.run_source_control(
                compiler, helpers.dccmake_path(), output,
                helpers.SourceControl(
                    "inactive-payload", "STPINACT",
                    function="pack_status", expect_exact=True,
                ),
                True, False,
            )
        finally:
            helpers.diagnostic_environment = saved
        print("parent accepts active 10:type:6 mutation: 32/64 failures; "
              "pre-verification generic semantic oracle: 0/64 failures; "
              "inactive 8:type:400 NOP payload: exact, 0/64 failures")
        return
    _, helpers.BASELINE_SELECTED_HASH = helpers.baseline_compile(compiler, output)
    helpers.forced_fallback_control(compiler, output)
    semantic_runtime(compiler, output, True)
    semantic_runtime(compiler, output, False)
    source_rows = helpers.run_source_controls(
        compiler, helpers.dccmake_path(), output,
    )
    source_rows.extend(forced_runtime(compiler, output))
    helpers.write_tsv(
        output / "source-controls.tsv",
        ("name", "defines", "outcome", "selector", "reject_reason",
         "mode", "runtime"), source_rows,
    )
    cases = [
        helpers.MutationCase(f"{field}-{i}", f"{i}:{field}:{value}")
        for i in range(79)
        for field, value in helpers.MUTATED_FIELDS
    ] + [
        helpers.MutationCase(f"identity-{i}", f"{i}:identity:120")
        for i in range(79)
    ] + [helpers.MutationCase("boolean-source", "10:type:6")]
    helpers.MUTATION_CASES = cases
    rows = helpers.run_mutation_cases(compiler, output, args.jobs)
    classified = []
    for row in rows:
        index, field, _ = row[1].split(":")
        relevance, expected = expected_mutation(int(index), field)
        classified.append((*row, relevance, expected))
    helpers.write_tsv(
        output / "mutation-census.tsv",
        ("name", "mutation", "outcome", "selector", "reject_reason",
         "relevance", "expected"), classified,
    )
    unexpected = [row for row in classified if row[2] != row[-1]]
    if unexpected:
        raise RuntimeError(f"unexpected active/inactive census outcomes: {unexpected}")
    outcomes = Counter(row[2] for row in rows)
    print(f"status pack Wave 10100 instructions=79 mutations={len(rows)} "
          f"outcomes={outcomes} runtimes={len(source_rows) + 2}")


if __name__ == "__main__":
    main()
