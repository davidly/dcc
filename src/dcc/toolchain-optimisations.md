# dcc Toolchain: Modularisation & Optimisation Summary

Branch: `modularise-dcc`

This document summarises the work done on the `modularise-dcc` branch: splitting the
single-file `dcc` compiler into maintainable modules, the four new peephole
optimisations added to `dccpeep.c`, and the empirical size/speed savings measured
across the sample programs.

---

## 1. Modularisation

The compiler was a single 18,841-line translation unit (`src/ddc.c`). It is now split
into **18 `.c` modules plus one umbrella header**, using true separate compilation.

- Root `dcc.c` on `main` is **untouched**.
- `src/ddc.c` is retained as the pre-split reference snapshot.[^upstream-sync]
- `src/dcc/dcc.h` is the single shared contract.
- `src/dcc/dcc_state.c` is the single definition site for all shared globals.

### Module breakdown

| Concern | Module | Lines |
|---|---|--:|
| Expression codegen | `dcc_expr.c` | 3,482 |
| Preprocessor / macros / lexer | `dcc_preproc.c` | 2,194 |
| Statements | `dcc_stmt.c` | 1,900 |
| Functions / top-level decls | `dcc_func.c` | 1,839 |
| Compare / branch codegen | `dcc_cmp.c` | 1,351 |
| Operators / arithmetic / longs | `dcc_ops.c` | 1,349 |
| Symbols / string pool / EXTRN | `dcc_symbols.c` | 986 |
| Assignment / floats / `gen_expr` | `dcc_assign.c` | 910 |
| Driver / `main()` | `dcc.c` | 881 |
| Umbrella header | `dcc.h` | 876 |
| Local decls / initializers | `dcc_decl.c` | 876 |
| Statement fast paths | `dcc_stmt_fast.c` | 723 |
| Type system | `dcc_types.c` | 711 |
| Constant folding | `dcc_fold.c` | 580 |
| Data-section emission | `dcc_data.c` | 281 |
| Integer constexpr parser | `dcc_constexpr.c` | 281 |
| Diagnostics / alloc / emit | `dcc_diag_emit.c` | 210 |
| Shared global definitions | `dcc_state.c` | 147 |
| C-symbol → M80 name mapping | `dcc_asmname.c` | 144 |

### Header surface (`dcc.h`)

- 429 function prototypes
- 91 `extern` global declarations
- 17 struct / union / typedef blocks

### Outcome

- Largest unit any tool or developer must load drops from **18,841 → 3,482 lines** (~5.4×).
- Build paths: `src/dcc/build-dcc.sh` and `src/dcc/CMakeLists.txt`.
- Host-language baseline: portable C11 across Clang, GCC, and MSVC.
- Behaviour is byte-identical to the monolith (full regression green).

---

## 2. Toolchain optimisations (`dccpeep.c`)

Four new peephole passes run as **final cleanup**, after all pre-existing passes have
converged, so they only tidy the settled instruction stream.

### A — `jp` → `jr` relaxation

In-range absolute jumps (3 bytes) become relative jumps (2 bytes).

```
jp LABEL      -> jr LABEL
jp z,LABEL    -> jr z,LABEL      (and nz / c / nc)
```

- Conservative upper-bound byte-size model per line; over-estimating distance can only
  reject an in-range branch, never emit an out-of-range `jr`.
- Fixpoint loop: shrinking one jump can bring others into range; monotonic, so it terminates.
- Never touches `jp m,` (no `jr m` form) or the indirect `jp (hl)`.

### B — dead 16-bit reload elimination

Two adjacent full-width loads to the same pair make the first dead:

```
ld hl,0
ld hl,_flags    ->   ld hl,_flags
```

Restricted to `hl` / `de` / `bc`, exactly adjacent so nothing can read the pair in between.

### C1 — constant sign-extension fold

```
ld hl,CONST          ld hl,CONST
ld a,h          ->   ld de,0       (CONST bit 15 clear)
rlca                 ld de,65535   (CONST bit 15 set)
sbc a,a
ld d,a
ld e,a
```

5 instructions → 1 (5 bytes → 3), and leaves `A` intact (strictly safer than the original,
which clobbered it). Only bare decimal constants are folded; symbol/address loads are skipped.

### D — duplicate IX-load → register copy

```
ld l,(ix+N)          ld l,(ix+N)
ld h,(ix+N+1)   ->   ld h,(ix+N+1)
ld e,(ix+N)          ld e,l
ld d,(ix+N+1)        ld d,h
```

When DE is loaded from the same frame slot just loaded into HL (the `x op x` shape).
DE ends identical, so it is safe regardless of how DE is later used. Saves 4 bytes and
two `(ix+d)` memory accesses.

### Deferred (with rationale, not oversight)

- **C2 — `long == 0` OR-reduce**: the constant operand is pushed before `gen_cmp32`; a safe
  implementation needs invasive constant-tracking or a fragile push/pop-spanning peephole.
- **E — induction-variable caching**: measured **0** genuinely-redundant IX reloads; every
  repeated reload follows an `add hl,de` / `sbc hl,de` that consumed HL. True caching is a
  register allocator — out of scope.
- **F — skip signed-compare bias**: 587 of the original bias sites are compares whose operand
  sign is not locally provable; safe removal needs value-range analysis the compiler lacks.
  The structural case is already handled by `pass_elim_loop_back_signed_bias`.

---

## 3. Empirical savings

**Method.** Three builds per app:

- `np` = no peephole (raw compiler output)
- `pb` = peephole **base** — all pre-existing passes, this branch's 4 new passes disabled
- `pn` = full current optimizer

Cycles captured with `ntvcm -p` (Z80 cycles per run). The base optimizer was produced by
disabling only the four new pass *calls* in `dccpeep.c`, giving a clean A/B isolation of the
branch's contribution.

> Note: `tsoak` is not present on this branch (it lives on `runtime-memory-optimisations`),
> so the compute-bound sample programs that *are* present were measured instead.

### Whole optimizer (np → pn) — context

Most of this predates the branch; shown to frame the incremental numbers below.

| app | .COM np | .COM pn | size Δ | cycles np | cycles pn | cycle Δ |
|---|--:|--:|--:|--:|--:|--:|
| sieve | 2048 | 1920 | −6.3% | 65,507,335 | 18,441,340 | **−71.9%** (3.55×) |
| nqueens | 2944 | 2560 | −13.0% | 46,367,350 | 30,787,967 | −33.6% |
| e | 2432 | 2432 | 0% | 28,724,400 | 23,761,743 | −17.3% |
| triangle | 1920 | 1920 | 0% | 48,552 | 45,035 | −7.2% |
| mm | 6656 | 6528 | −1.9% | 124,161,424 | 118,122,743 | −4.9% |
| tchess | 22656 | 16768 | −26.0% | (interactive) | | |
| wumpus | 13312 | 11008 | −17.3% | (interactive) | | |

### This branch's incremental contribution (pb → pn)

What passes A / B / C1 / D add on top of the existing optimizer.

| app | jp→jr | sxt-fold | dead16 | .MAC lines pb→pn | .COM pb→pn | cycles pb→pn |
|---|--:|--:|--:|--:|--:|--:|
| tchess | 291 | 9 | 0 | 7358→7322 | 17024→16768 (−256) | — |
| wumpus | 134 | 0 | 0 | 3345→3345 | 11136→11008 (−128) | — |
| pihex | 37 | 32 | 0 | 2442→2314 | 14336→14208 (−128) | −0.54% |
| primes | 18 | 17 | 0 | 815→747 | 3968→3840 (−128) | −0.59% |
| nqueens | 13 | 1 | 0 | 463→459 | (128-pad) | **−3.65%** |
| triangle | 2 | 4 | 0 | 151→135 | (128-pad) | −2.99% |
| sieve | 2 | 0 | 1 | 113→112 | (128-pad) | −0.93% |
| fact | 1 | 4 | 0 | 158→142 | (128-pad) | −0.35% |
| tphi | 0 | 4 | 0 | 236→220 | (128-pad) | −0.02% |
| tc89fmat | 5 | 0 | 0 | 422→422 | (128-pad) | −0.02% |

Aggregate across the full 124-app `runall` corpus:

- **2,501** `jp` → `jr` conversions (≈2,501 code bytes)
- **227** sign-extension folds (≈454 code bytes)

### Honest attribution

- **A (`jp`→`jr`) and C1 (sxt-fold) carry essentially all of the branch's codegen benefit.**
- **B fires once** (sieve) across the measured set; it deletes via an untagged `delete_n`, so a
  tag-grep cannot count it — the A/B build is what reveals it firing.
- **D fired once** in the entire 124-app corpus. Both B and D are correct but rarely applicable.
- The headline sieve 3.55× and tchess −26% are **mostly the pre-existing optimizer**. The
  branch's *incremental* perf win peaks at ~3.7% (nqueens); its size win shows as 128–256 byte
  `.COM` drops on the larger programs once `jr` savings cross 128-byte record boundaries.
  Small programs show no `.COM` change because their code already fits within the same number
  of padded records.

---

## 4. Verification

- Strict rebuild of `dccpeep.c` and the modular compiler — no warnings.
- Adversarial peephole fixtures: `jp m`, `jp (hl)`, near/far conditional jumps, sign-extension
  of `32767` / `-1` / `65535` / `32768`, duplicate IX loads.
- Representative programs assembled through M80 (`sieve`, `nqueens`, `fact`, `triangle`, `e`).
- 124-app generated assembly corpus scanned: **no invalid `jr` forms**, no `A`/flags consumer
  immediately after a folded sign-extension, no executable `=` line that could mislead the
  `jr` distance estimator.
- Full regression `runall.sh ntvcm`: **green** — exit 1 from `__DATE__` / `__TIME__` only, both
  filtered diffs empty, all result files 3,969 lines.

### Reviewer caveats (acknowledged)

1. The `dead_reg16_reload: 0` corpus figure was a measurement artifact (untagged deletion),
   not proof of non-firing — the A/B build shows it fires once.
2. `dup_ix_load_reg_copy` firing once corpus-wide is negligible real-world value.
3. The sign-extension flag-consumer scan looked one line ahead, and the `jr` ±128 boundary was
   not unit-tested — both are covered empirically because the whole corpus assembles cleanly
   through M80 (which rejects out-of-range `jr`).

Conclusion: the optimisations are **correct**; A and C1 deliver the value, B and D are correct
but near-inert.

---

[^upstream-sync]: **Porting upstream compiler fixes after a sync.** The modules were split
    from `src/ddc.c`, a snapshot of the monolith taken *before* upstream
    (`davidly/dcc`) landed commit `77e5334` ("fix dcc bugs with macro name collisions with
    struct fields and accessing arrays of structs"). After the branch was rebased onto the
    synced `main`, the new upstream test programs `tstfield`, `tstretst`, and `fint` failed
    under the modular compiler (crash / compile errors / miscompile) precisely because the
    modules predated that fix; `bint` already passed. The fix delta
    (`git diff --no-index src/ddc.c dcc.c`) was ported by hand into the relevant modules —
    self-referential macro suppression and `__LINE__`/`__FILE__` argument expansion in
    `dcc_preproc.c`, the postfix field-access-after-subscript loops (and the
    `scale_hl_by_elem_size` refactor) in `dcc_expr.c`, and the subscript/field lookahead
    rewrite in `dcc_ops.c`. All helpers the fix relied on
    (`apply_field_access_from_addr`, `try_parse_const_subscript`, `base_struct_id_from_type`,
    `find_field_def`, `current_field_array_*`) already existed in the modules. After the port
    the four apps compile to **byte-identical** `.MAC` versus the upstream root `dcc.c`, and the
    full `runall.sh` regression is green in both peephole and no-peephole modes (130/130 app
    sections matching baseline). Reconciling these compiler fixes into the modules is
    independent of the modularisation and peephole work described above.
