# mathf — historical `<math.h>` source, no longer used to build the runtime

`mathf.c` originally generated the single-precision transcendental math
routines merged into `DCCRTL.MAC`. That block (`_expf`/`_logf`/`_powf`/
`_sinf`/… between the `BEGIN/END math functions` markers) is now
**hand-maintained directly in `DCCRTL.MAC`**, has diverged from this file
(e.g. Inf/NaN/domain-error handling added by hand), and must not be
regenerated from here - doing so would silently discard those fixes.

This file and its old build/merge procedure are kept for historical
reference only. To change one of these routines, edit `DCCRTL.MAC` directly,
the same way as any other hand-written part of the runtime.

## What it covers (for reference)

- exponential / logarithm: `expf`, `logf`, `log10f`, `powf`
- hyperbolic: `sinhf`, `coshf`, `tanhf`
- trig / inverse-trig: `sinf`, `cosf`, `tanf`, `atanf`, `atan2f`, `asinf`, `acosf`
- decomposition: `frexpf`, `ldexpf`, `modff`

## Tests

`tests/tmathf.c` verifies these routines under `ntvcm`. Accuracy is roughly
5–6 significant digits; bit-twiddling routines (`frexpf`/`ldexpf`/`modff`) are
exact. See `docs/dcc-c89-reference-guide.md` for the public API and
`docs/dccrtlstrip-inclusion-table.md` for the per-symbol code-size cost.
