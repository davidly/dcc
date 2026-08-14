#!/usr/bin/env python3
"""Conservatively report runtime IY references for MIR schedule review.

Generated MIR schedules treat IY as callee-saved, which lets a value held there
survive a call.

  1. Every generated function that claims IY saves and restores it.

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

print("  REVIEW: %d IY reference(s) found. Verify each helper preserves IY" % len(violations))
print("  on every path, or keep IY-owning MIR schedules from calling it.")
for lineno, text in violations:
    print("    %s:%d: %s" % (path, lineno, text.strip()))
sys.exit(1)
