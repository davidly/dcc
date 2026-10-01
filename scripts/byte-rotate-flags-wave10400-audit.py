#!/usr/bin/env python3
"""Prove byte-rotate flags selection, fallback, and displaced-local rejection.

The diagnostic compiler is a private source copy. Its persistent overlap
witness changes both dead carry stores to the value parameter's address before
candidate selection, unlike the normal machine-only hook (which restores MIR).
--reproduce-base 1dc57abd demonstrates that the former matcher ignored those
observable writes. Inactive-field survivors are classified, not called bugs.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTION = "byte_rotate_flags_fixture"
SOURCE = "tests/mir-clobber/byterotflags.c"
TEMPLATE = "byte-rotate-flags"
COUNT = 139
BASELINE_SHA256 = (
    "c9c5a3d9a538835aba4525fa4562b73d8e676d2420945789c8fd3a1756093391"
)
BASELINE_SELECTED_HASH = "02a87b1c"
EXPECTED_OUTCOMES = Counter(rejected=1914, accepted=1443)
EXPECTED_SURVIVORS = Counter(
    {"inactive-field": 1421, "equivalent-declared-location": 22}
)


def load_helper(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


COMMON = load_helper("rotate_flags_common", "recursive-byte-minimax-wave2600-audit.py")
OLD = load_helper("rotate_flags_old", "byte-rotate-wave24-audit.py")
run = COMMON.run
CONTROLS = (
    ("baseline", (), True),
    ("renamed", ("BYTE_ROTATE_FLAGS_RENAMED",), True),
    ("byte-flags", ("BYTE_ROTATE_FLAGS_BYTE_FLAGS",), False),
    ("wide-flags", ("BYTE_ROTATE_FLAGS_WIDE_FLAGS",), False),
    ("volatile", ("BYTE_ROTATE_FLAGS_VOLATILE_STATE",), False),
    ("mask", ("BYTE_ROTATE_FLAGS_CHANGED_MASK",), False),
    ("cfg", ("BYTE_ROTATE_FLAGS_EXTRA_CFG",), False),
    ("shift", ("BYTE_ROTATE_FLAGS_CHANGED_SHIFT",), False),
    ("forced", (), False),
)

WITNESS_NEEDLE = """\
    int default_policy = 0;

    if (generated == NULL)
"""
WITNESS_REPLACEMENT = """\
    int default_policy = 0;

    if (getenv("DCC_ROTATE_OVERLAP_WITNESS") != NULL &&
        !strcmp(mir.name, "byte_rotate_flags_fixture")) {
        int type, storage, value_offset, local_offset;
        if (mir.count != 139 ||
            !mir_declared_location(mir.insns[2].name,
                &type, &storage, &value_offset) ||
            !mir_declared_location(mir.insns[42].name,
                &type, &storage, &local_offset))
            fatal("rotate overlap witness shape changed");
        mir.insns[42].immediate = value_offset - local_offset;
        mir.insns[95].immediate = value_offset - local_offset;
        fprintf(stderr,
                "rotate overlap witness local=%d param=%d displacement=%ld\\n",
                local_offset, value_offset, mir.insns[42].immediate);
    }

    if (generated == NULL)
"""


def environment(function=FUNCTION):
    env = COMMON.diagnostic_environment(function, include_mir=False)
    env.pop("DCC_ROTATE_OVERLAP_WITNESS", None)
    env.update(DCC_MIR_MACHINE_TEMPLATE=TEMPLATE)
    return env


def compile_command(compiler, output, defines=()):
    return [
        str(compiler), "-fstack-check", "-stack", "512", "-I", ".",
        *(f"-D{define}" for define in defines), SOURCE, "-o", str(output),
    ]


def prepare_compiler(output, reference=None):
    source = output / "src"
    binary = output / "bin"
    shutil.copytree(ROOT / "src/dcc", source)
    if reference:
        (source / "dcc_mir_machine_float_recursion.c").write_text(
            run(["git", "show", f"{reference}:src/dcc/dcc_mir_machine_float_recursion.c"]),
            encoding="utf-8",
        )
    for filename, needle, replacement in (
        ("dcc_mir_machine_emit.c", COMMON.MUTATION_HOOK_NEEDLE,
         COMMON.MUTATION_HOOK_REPLACEMENT),
        ("dcc_mir_select.c", WITNESS_NEEDLE, WITNESS_REPLACEMENT),
    ):
        path = source / filename
        text = path.read_text(encoding="utf-8")
        if text.count(needle) != 1:
            raise RuntimeError(f"{filename} diagnostic hook changed")
        path.write_text(text.replace(needle, replacement), encoding="utf-8")
    run([
        "cmake", "-S", str(source), "-B", str(output / "cmake"),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DDCC_RUNTIME_OUTPUT_DIRECTORY={binary}",
    ], timeout=300)
    run([
        "cmake", "--build", str(output / "cmake"), "--parallel", "4",
        "--target", "dcc",
    ], timeout=600)
    return binary / ("dcc.exe" if os.name == "nt" else "dcc")


def checksum(defines):
    flags_raw = any(name.endswith(("BYTE_FLAGS", "WIDE_FLAGS")) for name in defines)
    mask = 0xf0 if "BYTE_ROTATE_FLAGS_CHANGED_MASK" in defines else 0xe0
    shift = 2 if "BYTE_ROTATE_FLAGS_CHANGED_SHIFT" in defines else 1
    total = 0
    for operation in (0, 0x20, 0x40, 0x60, 0x10, 0x30, 0x50, 0x70, 0xff):
        if "BYTE_ROTATE_FLAGS_EXTRA_CFG" in defines and operation == 0xff:
            operation = 0
        operation &= mask
        for original in (0, 1, 2, 0x40, 0x7f, 0x80, 0x81, 0xff):
            for carry in (0, 1):
                value = original
                new_carry = (value & 0x80) if operation in (0, 0x20) else (value & 1)
                if not flags_raw:
                    new_carry = int(new_carry != 0)
                if operation in (0, 0x20):
                    value = ((value << shift) | (carry if operation == 0x20 else 0)) & 255
                else:
                    value = (value >> shift) | (0x80 if operation != 0x40 and carry else 0)
                negative = value & 0x80
                if not flags_raw:
                    negative = int(negative != 0)
                total = (total * 131 + value) & 0xffffffff
                total = (total * 5 + new_carry * 4 + negative * 2 + int(value == 0)) & 0xffffffff
    return total


def runtime(compiler, output, source, name, defines, stack, peep, forced=False,
            witness=False, expect_exact=False, allow_failure=False):
    function = FUNCTION + ("_renamed" if "BYTE_ROTATE_FLAGS_RENAMED" in defines else "")
    env = environment(function)
    if forced:
        env["DCC_MIR_SELECT_CANDIDATE"] = "spilled-phi-slot"
    if witness:
        env["DCC_ROTATE_OVERLAP_WITNESS"] = "1"
    build = output / f"{name}-{int(stack)}-{int(peep)}"
    build.mkdir(parents=True, exist_ok=True)
    report = run([
        str(COMMON.dccmake_path()), f"dcc-input={source}", "dcc-output=BRFLAGS",
        f"dcc-build-dir={build}", f"dcc-tool={compiler}",
        f"dcc-peep={str(peep).lower()}", f"dcc-stack-check={str(stack).lower()}",
        "dcc-stack-bytes=512", *( [f"dcc-define={','.join(defines)}"] if defines else [] ),
    ], env, timeout=300)
    (build / "selection.log").write_text(report, encoding="utf-8")
    if expect_exact:
        COMMON.require_exact(report, function, name)
    else:
        COMMON.require_generic(report, function, name)
    if forced and COMMON.selection_from(report, function) != "spilled-scalar-cfg":
        raise RuntimeError("forced candidate did not select spilled fallback")
    process = subprocess.run(
        ["ntvcm", "-p", "-s:0", str(build / "BRFLAGS.COM")],
        cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=30, check=False,
    )
    (build / "runtime.log").write_text(process.stdout, encoding="utf-8")
    match = re.search(r"byte rotate flags failures=(\d+) checks=(\d+) checksum=(\d+)", process.stdout)
    if not match:
        raise RuntimeError(f"{name}: missing runtime result\n{process.stdout}")
    result = tuple(map(int, match.groups()))
    if not allow_failure and (process.returncode or result != (0, 144, checksum(defines))):
        raise RuntimeError(f"{name}: wrong runtime {result}, expected checksum {checksum(defines)}")
    return (name, stack, peep, COMMON.selection_from(report, function), *result)


def mutation_cases():
    fields = tuple(
        (name, 777 if name == "type" else value)
        for name, value in COMMON.MUTABLE_FIELDS
    ) + (("identity", 120),)
    return [
        (instruction, field, value)
        for instruction in range(COUNT) for field, value in fields
    ] + [
        (instruction, "immediate", displacement)
        for instruction in (42, 95)
        for displacement in (-2147483648, -1, 1, 7, 2147483647)
    ] + [
        (instruction, "dst", 0) for instruction in (4, 16, 40, 93, 125, 137)
    ] + [
        (instruction, "memory_flags", 2) for instruction in (17, 23, 42, 95, 125)
    ]


def survivor_class(instruction, opcode, field):
    if opcode in OLD.UNUSED_FIELDS.get(field, set()):
        return "inactive-field"
    if field == "memory_size" and opcode == "unary":
        return "inactive-field"
    if field in ("phi_pred1", "phi_pred2", "inline_temp_id"):
        return "inactive-field"
    if field == "dst" and opcode in ("nop", "label", "jump", "brfalse", "return", "store", "storeind"):
        return "inactive-field"
    if field == "label" and opcode not in ("label", "jump", "brfalse"):
        return "inactive-field"
    if field == "successor0" and opcode == "return":
        return "inactive-field"
    if field == "successor1" and opcode != "brfalse":
        return "inactive-field"
    if field == "secondary_offset" and opcode != "binary":
        return "inactive-field"
    if field == "object":
        return "equivalent-declared-location" if opcode in ("param", "load", "store", "address") else "inactive-field"
    if field == "memory_flags" and opcode == "load":
        return "inactive-field"
    raise RuntimeError(f"unclassified survivor {instruction}:{opcode}:{field}")


def mutate(compiler, output, opcodes, case):
    instruction, field, value = case
    path = output / f"{instruction}-{field}-{value}.MAC"
    env = environment()
    env.update(
        DCC_MIR_MACHINE_MUTATE_FUNCTION=FUNCTION,
        DCC_MIR_MACHINE_MUTATE=f"{instruction}:{field}:{value}",
    )
    report = run(compile_command(compiler, path), env, timeout=60)
    selector = COMMON.selection_from(report, FUNCTION)
    if selector == "scheduled-machine-cfg":
        classification = survivor_class(instruction, opcodes[instruction], field)
        outcome = "accepted"
    else:
        COMMON.require_generic(report, FUNCTION, str(case))
        env["DCC_MIR_SELECT_CANDIDATE"] = "spilled-phi-slot"
        forced = run(compile_command(compiler, path), env, timeout=60)
        if COMMON.selection_from(forced, FUNCTION) != "spilled-scalar-cfg":
            raise RuntimeError(f"forced fallback failed for {case}")
        rejection = re.search(
            rf"function={FUNCTION} template={TEMPLATE} reject=(\S+)", report,
        )
        if not rejection:
            raise RuntimeError(f"missing matcher rejection for {case}")
        classification = rejection.group(1)
        if f"template={TEMPLATE} reject={classification}" not in forced:
            raise RuntimeError(f"forced rejection changed for {case}")
        outcome = "rejected"
    path.unlink()
    return (*case, opcodes[instruction], outcome, classification)


def write_tsv(path, header, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow(header)
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--output-dir", default="build/byte-rotate-flags-wave10400-audit")
    parser.add_argument("--reproduce-base", help="optional pre-fix commit for persistent witness")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be 1..4")
    output = (ROOT / args.output_dir).resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        parser.error("output directory must be a descendant of build/")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    scratch = output / "scratch"
    scratch.mkdir()
    os.environ["TMPDIR"] = str(scratch)
    source = output / "brflags.c"
    shutil.copyfile(ROOT / SOURCE, source)
    compiler = prepare_compiler(output / "current")
    env = environment()
    env.update(DCC_MIR_REPORT="1", DCC_MIR_FUNCTION=FUNCTION)
    baseline = output / "baseline.MAC"
    report = run(compile_command(compiler, baseline), env)
    (output / "baseline.log").write_text(report, encoding="utf-8")
    COMMON.require_exact(report, FUNCTION, "baseline")
    if hashlib.sha256(baseline.read_bytes()).hexdigest() != BASELINE_SHA256:
        raise RuntimeError("valid baseline assembly changed")
    if COMMON.selected_hash(report, FUNCTION) != BASELINE_SELECTED_HASH:
        raise RuntimeError("valid schedule hash changed")
    section = report.split(f"; MIR function={FUNCTION} ", 1)[1].split("; MIR summary", 1)[0]
    opcodes = dict((int(index), opcode) for index, opcode in re.findall(r";\s+(\d+)\s+(\w+)\s+", section))
    if list(opcodes) != list(range(COUNT)):
        raise RuntimeError("MIR instruction shape changed")
    rows = [
        runtime(compiler, output / "runtime", source, name, defines, stack, peep,
                forced=name == "forced", expect_exact=exact)
        for name, defines, exact in CONTROLS
        for stack in (True, False) for peep in (True, False)
    ]
    write_tsv(output / "source-controls.tsv",
              ("name", "stack", "peep", "selector", "failures", "checks", "checksum"), rows)
    witness = runtime(compiler, output / "witness", source, "fixed", (), False, False,
                      witness=True, allow_failure=True)
    forced = runtime(compiler, output / "witness", source, "forced", (), False, False,
                     forced=True, witness=True, allow_failure=True)
    if witness[4:] != forced[4:] or witness[4] == 0:
        raise RuntimeError("fixed overlap does not preserve the observable generic semantics")
    if args.reproduce_base:
        reference = prepare_compiler(output / "reference", args.reproduce_base)
        accepted = runtime(reference, output / "witness", source, "old-exact", (), False, False,
                           witness=True, expect_exact=True)
        generic = runtime(reference, output / "witness", source, "old-forced", (), False, False,
                          witness=True, forced=True, allow_failure=True)
        if generic[4:] != forced[4:] or accepted[4:] == generic[4:]:
            raise RuntimeError("pre-fix semantic false acceptance not reproduced")
        print(f"pre-fix exact={accepted[4:]} persistent generic={generic[4:]}")
    work = output / "mutations"
    work.mkdir()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        mutations = sorted(executor.map(lambda case: mutate(compiler, work, opcodes, case), mutation_cases()))
    work.rmdir()
    write_tsv(output / "mutation-census.tsv",
              ("instruction", "field", "value", "opcode", "outcome", "classification"), mutations)
    outcomes = Counter(row[4] for row in mutations)
    classes = Counter(row[5] for row in mutations if row[4] == "accepted")
    if outcomes != EXPECTED_OUTCOMES or classes != EXPECTED_SURVIVORS:
        raise RuntimeError(f"mutation census changed: {outcomes}; {classes}")
    shutil.rmtree(scratch)
    print(f"byte rotate flags Wave 10400: {len(rows)} runtime controls; "
          f"{len(mutations)} mutations {dict(outcomes)}; survivors {dict(classes)}")
    print(f"fixed persistent overlap={witness[4:]} (same as forced generic)")
    print(f"valid schedule hash={BASELINE_SELECTED_HASH} assembly={BASELINE_SHA256}")


if __name__ == "__main__":
    main()
