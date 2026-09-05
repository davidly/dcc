# Appendix: runtime function sizes

!!! info "Auto-generated"
    The statistics and tables on this page are regenerated from `DCCRTL.MAC`
    every time the documentation is built, by the `docs/docs/hooks/runtime_sizes.py`
    MkDocs hook. They never go out of step with the runtime source, so prefer
    these numbers over any quoted elsewhere.

This page estimates the source volume associated with selected runtime features
and their transitive dependencies. For *how* `dccrtlstrip` decides what
to keep — and the optimisation takeaways for keeping a program small — see
[*Runtime optimization*](01-dccrtlstrip.md).

## How to read the numbers

The numbers are **relative source-line counts**, not exact bytes (blocks contain
comments and blank lines). They help locate large source/dependency groups,
but cannot establish binary-size or performance rankings. Measure the linked
`.COM` and assembler listings for those decisions.

- **self** — source lines in the function's own block.
- **marginal** — `self` plus every *additional* reachable block that is not
  already in the `start`-rooted baseline. Actual application references may
  already retain some of these blocks, reducing the incremental cost.
- **pulls in** — the extra runtime blocks added beyond the baseline.

!!! note "Symbols are internal runtime labels"
    The **Symbol** column lists the *internal* assembler label, not the C name
    you call. For example `__stchk` is the **stack-overflow guard** linked by
    `-fstack-check`, `__mlh` is the `malloc` heap helper, and `__pf_run` is the
    shared `printf` engine. Searching this page for a feature word such as
    "stack" or "printf" may not match the symbol — look for the corresponding
    `__` label instead (the *pulls in* column names related ones).

<!-- DCCRTL-SIZE-TABLES -->
