#!/usr/bin/env python3
"""Conservatively report runtime IY references for MIR schedule review.

Generated MIR schedules treat IY as callee-saved, which lets a value held there
survive a call.

  1. Every generated function that claims IY saves and restores it.

  2. Runtime helpers either never reference IY, or save and restore it on every
     path. This script checks the one reviewed IY-using helper.
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


def reviewed_extln_region():
    """Return true only for the audited __extln save/restore control flow."""
    start = next(
        (i for i, line in enumerate(lines) if line.strip().lower() == "__extln:"),
        None,
    )
    end = next(
        (
            i
            for i, line in enumerate(lines[start + 1 :], start + 1)
            if line.strip().lower() == "__zerdm:"
        ),
        None,
    ) if start is not None else None
    if start is None or end is None:
        return False
    if any(not (start < lineno <= end) for lineno, _ in references):
        return False

    instructions = []
    labels = set()
    for raw in lines[start:end]:
        code = raw.split(";", 1)[0].strip().lower()
        if not code:
            continue
        if code.endswith(":"):
            labels.add(code[:-1])
            continue
        instructions.append(code)
    if instructions.count("push    iy") != 1 or instructions.count("pop     iy") != 2:
        return False
    push = instructions.index("push    iy")
    working_pop = instructions.index("pop     iy")
    pop = len(instructions) - 3
    if (
        push >= working_pop
        or instructions[working_pop - 1] != "push    hl"
        or instructions[pop] != "pop     iy"
        or any(code == "ret" for code in instructions[:pop])
    ):
        return False
    if instructions[pop + 1 :] != ["pop     ix", "ret"]:
        return False

    for code in instructions:
        match = re.match(r"(?:jr|jp)\s+(?:[a-z]+,)?\s*([a-z_][a-z0-9_]*)$", code)
        if match and match.group(1) not in labels:
            return False
    return True

print("DCCRTL IY invariant check (%s)" % path)
if not references:
    print("  OK: no IY register reference anywhere in the runtime.")
    print("  IY is therefore free for dcc to allocate as a callee-saved register.")
    sys.exit(0)

if reviewed_extln_region():
    print("  OK: IY references are confined to audited helper __extln.")
    print("  __extln saves IY before use and restores it on every return path.")
    sys.exit(0)

print("  REVIEW: %d IY reference(s) found. Verify each helper preserves IY" % len(references))
print("  on every path, or keep IY-owning MIR schedules from calling it.")
for lineno, text in references:
    print("    %s:%d: %s" % (path, lineno, text.strip()))
sys.exit(1)
