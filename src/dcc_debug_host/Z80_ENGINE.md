# Debugger Z80 Engine

`dcc-debug-host` owns the Z80 implementation in this directory:

- `x80.cxx`: Z80 instruction engine and disassembler;
- `x80.hxx`: Z80 register/flag model;
- `cpu_x80_adapter.cpp`: adapter to the internal `z80_t` hardware API; and
- `z80.h`: debugger-only register/disassembly API layered on the vendored
  hardware ABI.

The engine is derived from David Lee's
[`ntvcm`](https://github.com/davidly/ntvcm) `x80` core, distributed under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
Its source lineage includes ntvcm through commit `dce7246` (2026-08-09). The
project imported and specialized that revision to Z80 before adding the
interrupt, bus-state, and exact instruction-boundary interfaces retained here.

This copy is intentionally **Z80 only**. It contains no Intel 8080 mode flag,
8080 instruction/cycle table, 8080 dispatch branch, or 8080 template
instantiation. `DEBUGGER_X80_Z80_ONLY` and `registers::z80_only` provide
compile-time checks in the engine, adapter, and core tests.

Only `dcc-debug-host` links these files. Its 64 KB `memory[]` implementation
and hardware-facing `z80_t` ABI are vendored under `hardware/`; building the
debugger does not require another emulator source tree.

The debugger-specific core test runs the same interrupt, block-instruction,
index-register, undocumented flag, R-register, MEMPTR, and alternate-register
checks as the shared core tests. Higher-level CTest suites additionally boot
real CP/M and exercise source debugging through this engine.
