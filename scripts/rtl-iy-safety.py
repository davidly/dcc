#!/usr/bin/env python3
"""Conservatively report runtime IY references for MIR schedule review.

Generated MIR schedules treat IY as callee-saved, which lets a value held there
survive a call.

  1. Every generated function that claims IY saves and restores it.

  2. Runtime helpers either never reference IY, save and restore it on every
     path, or participate in the reviewed setjmp/longjmp context pair. This
     script checks the three reviewed IY-using helpers.
       * CP/M's BDOS/BIOS cannot: CP/M 2.2 is 8080 code, and the 8080 has no
         index registers at all. dcc already depends on this for IX, which is
         its frame pointer and stays live across every call it emits - so
         relying on it for IY adds no new assumption to the toolchain.

Fact 2 is the only part that can drift as the runtime is edited, so it is what
this script guards. Exits non-zero if the invariant is broken.

Usage: python3 scripts/rtl-iy-safety.py [DCCRTL.MAC]
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "DCCRTL.MAC"

# A real IY register reference, not a symbol that merely contains those letters
# (DCCRTL has a "__powf_iy" scratch variable and a "MATAN2XIYF" label).
iy_re = re.compile(r"(?<![A-Za-z0-9_@?$])iy[hl]?(?![A-Za-z0-9_@?$])", re.IGNORECASE)

lines = list(open(path, "r", errors="replace"))
references = []
for lineno, raw in enumerate(lines, 1):
    code = raw.split(";", 1)[0]
    label = code.split(":", 1)
    if len(label) == 2:
        code = label[1]
    if iy_re.search(code):
        references.append((lineno, raw.rstrip("\n")))


def region_bounds(start_label, end_label):
    start = next(
        (
            i
            for i, line in enumerate(lines)
            if line.strip().lower() == start_label
        ),
        None,
    )
    end = next(
        (
            i
            for i, line in enumerate(lines[start + 1 :], start + 1)
            if line.strip().lower() == end_label
        ),
        None,
    ) if start is not None else None
    if start is None or end is None:
        return None
    return start, end


def region_instructions(bounds):
    if bounds is None:
        return [], {}
    start, end = bounds

    instructions = []
    label_positions = {}
    for raw in lines[start:end]:
        code = raw.split(";", 1)[0].strip().lower()
        if not code:
            continue
        if code.startswith("public ") or code.startswith("extrn "):
            continue
        if code.endswith(":"):
            label_positions[code[:-1]] = len(instructions)
            continue
        instructions.append(code)
    return instructions, label_positions


def sequence_index(instructions, sequence):
    width = len(sequence)
    for index in range(len(instructions) - width + 1):
        if instructions[index : index + width] == sequence:
            return index
    return -1


def references_confined_to(regions):
    return all(
        any(start < lineno <= end for start, end in regions)
        for lineno, _ in references
    )


def reviewed_extln_region():
    """Return the audited __extln save/restore region, or None."""
    bounds = region_bounds("__extln:", "__zerdm:")
    instructions, label_positions = region_instructions(bounds)
    if bounds is None:
        return None
    if instructions.count("push    iy") != 1 or instructions.count("pop     iy") != 2:
        return None
    push = instructions.index("push    iy")
    working_pop = instructions.index("pop     iy")
    pop = len(instructions) - 3
    if (
        push >= working_pop
        or instructions[working_pop - 1] != "push    hl"
        or instructions[pop] != "pop     iy"
        or any(code == "ret" for code in instructions[:pop])
    ):
        return None
    if instructions[pop + 1 :] != ["pop     ix", "ret"]:
        return None

    for code in instructions:
        match = re.match(r"(?:jr|jp)\s+(?:[a-z]+,)?\s*([a-z_][a-z0-9_]*)$", code)
        if match:
            target = match.group(1)
            if (
                target not in label_positions
                or label_positions[target] > pop
            ):
                return None
    return bounds


def reviewed_nonlocal_jump_regions():
    """Return the audited setjmp/longjmp regions, or an empty list."""
    setjmp_bounds = region_bounds("_setjmp:", "_longjmp:")
    longjmp_bounds = region_bounds("_longjmp:", "_dopn:")
    setjmp_instructions, _ = region_instructions(setjmp_bounds)
    longjmp_instructions, longjmp_labels = region_instructions(longjmp_bounds)
    if setjmp_bounds is None or longjmp_bounds is None:
        return []

    save_sequence = [
        "push    iy",
        "pop     de",
        "ld      a,e",
        "ld      (hl),a",
        "inc     hl",
        "ld      a,d",
        "ld      (hl),a",
    ]
    restore_sequence = [
        "ld      c,(hl)",
        "inc     hl",
        "ld      b,(hl)",
        "push    bc",
        "pop     iy",
    ]
    save = sequence_index(setjmp_instructions, save_sequence)
    restore = sequence_index(longjmp_instructions, restore_sequence)
    setjmp_control = [
        code
        for code in setjmp_instructions
        if re.match(r"(?:call|djnz|halt|jp|jr|reti?|retn|rst)\b", code)
    ]
    longjmp_branches = [
        (index, code)
        for index, code in enumerate(longjmp_instructions)
        if re.match(r"(?:djnz|jp|jr)\b", code)
    ]
    if (
        save < 0
        or restore < 0
        or setjmp_instructions.count("push    iy") != 1
        or any("iy" in code for code in setjmp_instructions if code != "push    iy")
        or longjmp_instructions.count("pop     iy") != 1
        or any("iy" in code for code in longjmp_instructions if code != "pop     iy")
        or setjmp_control != ["ret"]
        or setjmp_instructions[-1] != "ret"
        or save >= len(setjmp_instructions) - 1
        or len(longjmp_branches) != 1
        or any(
            re.match(r"(?:call|halt|reti|retn|rst)\b", code)
            for code in longjmp_instructions
        )
        or longjmp_instructions.count("ret") != 1
        or longjmp_instructions[-1] != "ret"
    ):
        return []
    branch_index, branch = longjmp_branches[0]
    branch_match = re.match(
        r"jr\s+([a-z]+),\s*([a-z_][a-z0-9_]*)$", branch
    )
    if branch_match is None:
        return []
    branch_target = branch_match.group(2)
    if (
        branch_target not in longjmp_labels
        or longjmp_labels[branch_target] <= branch_index
        or longjmp_labels[branch_target] > restore
    ):
        return []
    try:
        stack_switch = longjmp_instructions.index("ld      sp,hl")
    except ValueError:
        return []
    if restore >= stack_switch:
        return []
    return [setjmp_bounds, longjmp_bounds]

print("DCCRTL IY invariant check (%s)" % path)
if not references:
    print("  OK: no IY register reference anywhere in the runtime.")
    print("  IY is therefore free for dcc to allocate as a callee-saved register.")
    sys.exit(0)

extln_region = reviewed_extln_region()
nonlocal_regions = reviewed_nonlocal_jump_regions()
reviewed_regions = ([extln_region] if extln_region is not None else []) + nonlocal_regions
if (
    extln_region is not None
    and len(nonlocal_regions) == 2
    and references_confined_to(reviewed_regions)
):
    print("  OK: IY references are confined to audited runtime paths.")
    print("  __extln preserves IY; setjmp/longjmp save and restore it in jmp_buf.")
    sys.exit(0)

print("  REVIEW: %d IY reference(s) found. Verify each helper preserves IY" % len(references))
print("  on every path or participates in the reviewed non-local context pair.")
for lineno, text in references:
    print("    %s:%d: %s" % (path, lineno, text.strip()))
sys.exit(1)
