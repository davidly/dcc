#!/usr/bin/env python3
"""Verify the invariant that makes IY allocatable: DCCRTL never touches it.

dcc's IY register allocation (REG_IY, dcc_regalloc.c) treats IY as CALLEE-SAVED,
which is what lets a value held there survive an arbitrary call - the property
BC can never have, and therefore the reason IY is the only register dcc can
allocate in a function that calls anything. That rests on exactly two facts:

  1. Every dcc-compiled function that claims IY saves and restores it around its
     own body. The compiler guarantees this by construction (emit_function_
     prologue/epilogue, dcc_func.c).

  2. Nothing else in a linked image ever writes IY:
       * DCCRTL.MAC contains no IY instruction anywhere. This script checks it.
       * CP/M's BDOS/BIOS cannot: CP/M 2.2 is 8080 code, and the 8080 has no
         index registers at all. dcc already depends on this for IX, which is
         its frame pointer and stays live across every call it emits - so
         relying on it for IY adds no new assumption to the toolchain.

Fact 2's first half is the only part that can drift as the runtime is edited, so
it is what this script guards. Exits non-zero if the invariant is broken.

Usage: python3 scripts/rtl-iy-safety.py [DCCRTL.MAC]
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "DCCRTL.MAC"

# A real IY register reference, not a symbol that merely contains those letters
# (DCCRTL has a "__powf_iy" scratch variable and a "MATAN2XIYF" label).
iy_re = re.compile(r"(?<![A-Za-z0-9_@?$])iy[hl]?(?![A-Za-z0-9_@?$])", re.IGNORECASE)

violations = []
for lineno, raw in enumerate(open(path, "r", errors="replace"), 1):
    code = raw.split(";", 1)[0]
    label = code.split(":", 1)
    if len(label) == 2:
        code = label[1]
    if iy_re.search(code):
        violations.append((lineno, raw.rstrip("\n")))

print("DCCRTL IY invariant check (%s)" % path)
if not violations:
    print("  OK: no IY register reference anywhere in the runtime.")
    print("  IY is therefore free for dcc to allocate as a callee-saved register.")
    sys.exit(0)

print("  BROKEN: %d IY reference(s) found. dcc's REG_IY allocation assumes the" % len(violations))
print("  runtime never writes IY; either revert these, or teach")
print("  asm_name_is_iy_safe_call (dcc_regalloc.c) to exclude the callers.")
for lineno, text in violations:
    print("    %s:%d: %s" % (path, lineno, text.strip()))
sys.exit(1)
