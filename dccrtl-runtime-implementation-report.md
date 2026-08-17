# DCC runtime audit implementation report

## Summary

This branch implements all 11 repair batches derived from the deep
`DCCRTL.MAC` audit. It fixes correctness, robustness, stripping, and measured
performance issues across the CP/M 2.2 Z80 runtime, compiler runtime selection,
`dccrtlstrip`, public headers, documentation, and regression coverage.

The runtime remains demand-linked: `dccrtlstrip` retains only the public blocks
required by an application. Large features such as the complete `strftime()`
formatter, exec loader safety state, floating-point formatting, and calendar
support do not become a universal runtime cost.

## What was fixed

### 1. Core safety and stripping

- Prevented `qsort()`'s backward walk from underflowing below the array.
- Fixed wrapped top-of-heap growth in `realloc()`.
- Made `fseek()` report the actual `lseek()` result.
- Preserved scanf literal characters across file-backed input.
- Kept the `strftime()` body in its referenced stripped block.
- Updated runtime-coverage accounting for every public formatter variant.

### 2. Floating-point core

- Corrected add/subtract subnormal handling and gradual packing.
- Implemented nearest-even rounding for add/subtract, multiply, fused
  multiply-add, and integer-to-float conversion.
- Corrected `nextafterf()` infinity transitions.
- Fixed math special values, signed zero, parity, overflow, underflow, and
  `errno` behavior across `expf`, `powf`, trig, hyperbolic, `frexpf`,
  `ldexpf`, `modff`, `fmodf`, `sqrtf`, and `nextafterf`.

### 3. Formatted I/O, scanning, and directories

- Replaced unbounded file formatting with a stripped 128-byte CP/M-record sink.
- Removed `%f` staging overflows and supported the full finite float range.
- Bounded string precision, width, and precision parsing.
- Corrected `%li`, `%lf`, `%ls`, space-flag, and argument-consumption behavior.
- Unified file scanner lookahead with stream pushback.
- Validated directory handles and corrected no-match directory streams.

### 4. Stream pushback and stdio

- Implemented one shared pushback byte per stream across `fgetc`, `fgets`,
  `fread`, scanf, and console input.
- Preserved the first pending byte, cleared EOF on success, and cleared stale
  state on open, close, and seek.
- Prevented wide `FILE *` values from aliasing valid descriptors.
- Returned pushed-back Ctrl-Z as guaranteed input.
- Added strict mode parsing and retained read/write/append/binary state without
  a second mode array.
- Corrected append positioning, partial transfers, temporary-file cleanup,
  `tmpnam()` collisions, rewind/error behavior, and console EOF.
- Allowed `fread`/`fwrite` to accumulate valid 16-bit totals through
  32767-byte low-level chunks.
- Corrected ctype full-width validation and single-evaluation `isgraph`.

### 5. Low-level CP/M file handling

- Added full-width descriptor and access-mode validation.
- Corrected `O_CREAT`, `O_TRUNC`, zero-length writes, seek validation, sync
  validation, and read/write error classification.
- Rejected wildcard and cross-drive renames as documented.
- Preserved retryable close failures and temporary-file state.
- Deliberately excluded the audit's 8 MiB file case because that size is
  outside the CP/M 2.2 target's practical/legal file model.

### 6. Runtime stripping and measured optimization

- Moved tmpfile, hyperbolic, calendar, console, and multibyte state into their
  owning stripped blocks.
- Added symbolic `EQU` dependency handling to `dccrtlstrip`.
- Removed avoidable fastcall prologues only where direct and general-call
  measurements both remained sound.
- Isolated fused divmod from its cache and made `__q2u` a direct alias.
- Applied measured heap, console, string, sort, formatted-I/O, arithmetic,
  float, exec, and calendar optimizations.
- Rejected experiments that increased linked size or regressed either peep or
  nopeep, including allocator, scanner, calendar, and layout proposals.
- Preserved explicit signedness-changing long casts through div/mod metadata
  repair, including fused quotient/remainder paths.

### 7. Startup, BIOS, and integer edge behavior

- Reserved and proved a 260-byte startup scratch layout for the full 127-byte
  CP/M command tail, 64 one-character arguments, and `argv[argc] == NULL`.
- Rejected wrapped near-64K heap starts before BSS or scratch writes.
- Accepted tabs as command-tail delimiters.
- Added full-width `strerror()` and `setvbuf()` validation.
- Added backward-compatible `biosreg(fn, bc, de)` multi-register BIOS calls.
- Unified standalone and fused divide-by-zero sentinels.
- Documented DCC's two's-complement `LONG_MIN / -1` behavior.

### 8. Conversions and exec safety

- Corrected incomplete `0x` handling in `strtol()`/`strtoul()`.
- Rejected wide characters not representable in DCC's single-byte execution
  encoding.
- Made `wcstombs()` report unrepresentable input.
- Staged caller-owned exec tails before destructive low-memory or stack setup.
- Supported the complete 127-byte CP/M command-tail payload.
- Rejected lossy empty/whitespace `execv()` arguments.
- Validated optional drive and CP/M 8.3 executable paths.
- Parsed default FCBs with the same whitespace rule as startup.
- Preflighted exact executable records and required that exact number of
  successful BDOS reads before transferring control.
- Protected DMA, stack, high FCB, trampoline, and TPA ranges from overlap.

### 9. Time, calendar, and string conversion

- Corrected `mktime()` normalization, mutation, and signed `time_t` range
  behavior.
- Prevented `mktime()` from clobbering the shared `gmtime()`/`localtime()`
  result.
- Corrected `time()` at the final signed-32-bit second.
- Made `difftime()` avoid integer subtraction overflow.
- Bounded and validated `asctime()`.
- Fixed large mantissa/exponent cancellation in `strtod()`/`atof()`.
- Added `ERANGE` reporting for float conversion overflow and underflow.
- Made `atoi()`/`atol()` accept every C89 whitespace character.
- Replaced the `strftime()` stub with a bounded, fixed-C-locale implementation
  of all supported C89 conversions plus `%C`, with deterministic malformed and
  invalid-field handling.

## Deliberate limitations

- Subnormal multiply, divide, and square root remain deferred because correct
  prototypes added excessive linked code.
- Defensive NULL `%s` formatting remains deferred because it taxes every valid
  `%s` call.
- Large finite trig arguments still use the documented compact range policy
  rather than a large Payne-Hanek reducer.
- CP/M cannot provide atomic append across independently opened descriptors.
- Universal metadata flush is unavailable on all CP/M 2.2 implementations.
- Numeric `FILE *` values prevent `freopen()` from preserving standard-stream
  object identity.
- `time_t` is signed 32-bit and ends at 2038-01-19 03:14:07.
- DCC has no distinct `double`; `strtod`, `atof`, and `difftime` use the
  target's 32-bit `float`.

## Performance-baseline comparison

The comparison uses `tests/perf_baselines.csv` from this merged working tree
against `upstream/main` at `950eaba92bbe0920252fdb3af89dee1680cd6bdd`. Only the 355 applications present in
both files are ranked. The branch adds 41 new regression applications and
removes none; new applications are excluded because main has no comparable baseline.

Apps are ranked by the arithmetic mean of their peep and nopeep percentage
cycle changes. Negative values are improvements. COM deltas are exact file
bytes and therefore move in 128-byte CP/M records.

These totals summarize checked workloads, not a weighted real-world benchmark:
large benchmark programs contribute more absolute cycles than small API tests.

| Measure | Result |
|---|---:|
| Common applications | 355 |
| New branch applications | 41 |
| Apps with average cycle improvement | 28 |
| Apps with average cycle regression | 231 |
| Apps with unchanged average cycles | 96 |
| Aggregate peep cycles | +0.253% |
| Aggregate nopeep cycles | +0.253% |
| Aggregate peep COM bytes | +8,832 (+0.315%) |
| Aggregate nopeep COM bytes | +7,936 (+0.277%) |
| Mean per-app peep cycle change | +0.704% |
| Mean per-app nopeep cycle change | +0.688% |
| Mean per-app average cycle change | +0.696% |
| Mean average change among improved apps | -3.247% |
| Mean average change among regressed apps | +1.463% |

### Most improved applications

Only 28 common applications have a lower average cycle baseline, so this table
contains all improved applications rather than padding it to 50.

| # | App | Avg cycle change | Peep | Nopeep | Peep cycles Δ | Nopeep cycles Δ | COM Δ P/N |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | `tpi` | -29.804% | -29.922% | -29.685% | -27,117,229 | -27,117,229 | -128/-128 |
| 2 | `tbfcnt` | -7.773% | -7.815% | -7.731% | -3,312 | -3,312 | +0/+0 |
| 3 | `tbfprot` | -7.347% | -7.382% | -7.312% | -3,756 | -3,756 | -128/-128 |
| 4 | `tbfdate` | -7.192% | -7.229% | -7.155% | -3,504 | -3,504 | -128/-128 |
| 5 | `sieve` | -7.144% | -4.770% | -9.517% | -876,898 | -1,820,888 | -128/-128 |
| 6 | `texec` | -6.538% | -6.615% | -6.462% | -10,523 | -10,285 | +384/+384 |
| 7 | `tbitfld` | -5.061% | -5.077% | -5.045% | -17,303 | -17,303 | -128/+0 |
| 8 | `tlmod` | -4.566% | -4.579% | -4.553% | -12,739 | -12,739 | -128/+0 |
| 9 | `tmuldiv` | -3.706% | -3.707% | -3.706% | -241,307 | -241,307 | -128/-128 |
| 10 | `tmatha` | -3.225% | -3.225% | -3.224% | -25,479 | -25,479 | +384/+384 |
| 11 | `tprintf` | -2.051% | -2.051% | -2.051% | -23,916 | -23,916 | +0/+0 |
| 12 | `pint` | -1.926% | -2.221% | -1.631% | -4,789,207 | -4,336,889 | +384/+384 |
| 13 | `tmirfix` | -1.633% | -1.953% | -1.314% | -2,415 | -2,172 | +0/+0 |
| 14 | `cobint` | -1.316% | -1.446% | -1.185% | -9,549,357 | -9,993,073 | +384/+256 |
| 15 | `tpfinf` | -1.246% | -1.246% | -1.246% | -918 | -918 | +0/+0 |
| 16 | `tcptrarr` | -0.065% | -0.065% | -0.065% | -42 | -42 | -128/-128 |
| 17 | `tarresc` | -0.062% | -0.062% | -0.062% | -14 | -14 | -128/-128 |
| 18 | `tatan2sp` | -0.052% | -0.052% | -0.052% | -97 | -97 | +0/+0 |
| 19 | `tidxrmw` | -0.040% | -0.041% | -0.039% | -7 | -7 | -128/+0 |
| 20 | `tbyteeq` | -0.031% | -0.032% | -0.031% | -7 | -7 | +0/+0 |
| 21 | `tstrify` | -0.027% | -0.027% | -0.027% | -56 | -56 | -128/+0 |
| 22 | `tpreproc` | -0.019% | -0.019% | -0.019% | -98 | -98 | +0/+0 |
| 23 | `tfcarg2d` | -0.019% | -0.019% | -0.019% | -7 | -7 | +0/+0 |
| 24 | `tportio` | -0.019% | -0.019% | -0.019% | -7 | -7 | +0/+0 |
| 25 | `tforhex` | -0.014% | -0.014% | -0.014% | -7 | -7 | -128/-128 |
| 26 | `tbios` | -0.013% | -0.013% | -0.013% | -14 | -14 | +0/+0 |
| 27 | `tlcont` | -0.012% | -0.012% | -0.012% | -21 | -21 | +0/+0 |
| 28 | `tbdos` | -0.009% | -0.009% | -0.009% | -14 | -14 | -128/-128 |

### Most negatively impacted applications

| # | App | Avg cycle change | Peep | Nopeep | Peep cycles Δ | Nopeep cycles Δ | COM Δ P/N |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | `tqsort` | +60.763% | +61.979% | +59.547% | +26,322,380 | +30,216,187 | +1408/+1664 |
| 2 | `tscanf` | +23.383% | +23.451% | +23.315% | +49,352 | +49,394 | +768/+768 |
| 3 | `ttmp` | +14.409% | +14.414% | +14.404% | +9,619 | +9,619 | +384/+512 |
| 4 | `tscanin` | +12.964% | +12.970% | +12.958% | +7,671 | +7,671 | +0/+0 |
| 5 | `tfreopen` | +9.627% | +9.627% | +9.627% | +8,288 | +8,288 | +384/+384 |
| 6 | `tungetc` | +8.703% | +8.728% | +8.677% | +4,682 | +4,682 | +384/+512 |
| 7 | `tabort` | +7.578% | +7.593% | +7.564% | +4,237 | +4,224 | +128/+256 |
| 8 | `tfo` | +7.354% | +7.361% | +7.348% | +6,475 | +6,475 | +384/+512 |
| 9 | `tfmedge` | +6.792% | +6.792% | +6.792% | +26,140 | +26,140 | +0/-128 |
| 10 | `texfile` | +6.430% | +6.430% | +6.429% | +13,684 | +13,684 | +384/+384 |
| 11 | `tlongidx` | +6.396% | +6.399% | +6.393% | +3,248 | +3,248 | +0/+0 |
| 12 | `tfpos` | +6.241% | +6.247% | +6.235% | +5,120 | +5,120 | +384/+384 |
| 13 | `tfaedge` | +6.105% | +6.105% | +6.105% | +44,410 | +44,410 | +0/+0 |
| 14 | `trig` | +6.062% | +6.062% | +6.062% | +758,489 | +758,489 | +0/+0 |
| 15 | `tfio` | +5.235% | +5.237% | +5.233% | +7,529 | +7,529 | +512/+512 |
| 16 | `tfloat4` | +5.222% | +5.237% | +5.207% | +46,199 | +46,199 | -128/-128 |
| 17 | `tfopenw` | +4.823% | +4.827% | +4.820% | +3,401 | +3,401 | +512/+384 |
| 18 | `tc89ffio` | +4.787% | +4.788% | +4.787% | +5,611 | +5,611 | +0/+0 |
| 19 | `fileops` | +4.755% | +4.758% | +4.752% | +145,849 | +145,849 | +512/+512 |
| 20 | `tfeof` | +4.621% | +4.644% | +4.598% | +103,630 | +103,630 | +512/+512 |
| 21 | `trenamex` | +4.421% | +4.423% | +4.419% | +5,969 | +5,969 | +512/+640 |
| 22 | `tappend` | +4.349% | +4.349% | +4.348% | +9,118 | +9,118 | +512/+512 |
| 23 | `tpfio` | +4.165% | +4.168% | +4.161% | +16,078 | +16,078 | +128/+128 |
| 24 | `tlogfsp` | +4.044% | +4.045% | +4.044% | +6,204 | +6,204 | +0/+0 |
| 25 | `mm` | +3.958% | +3.962% | +3.954% | +4,844,476 | +4,844,548 | +0/-128 |
| 26 | `terrno` | +3.592% | +3.599% | +3.586% | +22,530 | +22,530 | +640/+640 |
| 27 | `tdrive` | +3.462% | +3.462% | +3.462% | +5,988 | +5,988 | +512/+512 |
| 28 | `tlog` | +3.454% | +3.454% | +3.454% | +35,509 | +35,509 | -128/-128 |
| 29 | `tioerr` | +3.438% | +3.438% | +3.437% | +8,270 | +8,270 | +512/+384 |
| 30 | `tlongfn` | +3.375% | +3.376% | +3.375% | +7,443 | +7,443 | +384/+384 |
| 31 | `tphi` | +3.333% | +3.333% | +3.333% | +35,647 | +35,647 | -128/-128 |
| 32 | `tstar` | +3.221% | +3.221% | +3.220% | +5,055 | +5,055 | +384/+384 |
| 33 | `trenwild` | +3.209% | +3.210% | +3.208% | +7,614 | +7,614 | +384/+384 |
| 34 | `trwold` | +3.198% | +3.198% | +3.197% | +2,864,592 | +2,864,592 | +256/+256 |
| 35 | `tctrlz` | +3.175% | +3.179% | +3.172% | +25,835 | +25,835 | +512/+384 |
| 36 | `trtl2` | +3.108% | +3.116% | +3.101% | +1,880 | +1,880 | +256/+384 |
| 37 | `tctype` | +3.058% | +3.058% | +3.058% | +709 | +709 | +0/+0 |
| 38 | `ttrig` | +3.031% | +3.033% | +3.029% | +1,226,701 | +1,226,701 | -128/-128 |
| 39 | `twild` | +3.020% | +3.020% | +3.019% | +8,334 | +8,334 | +384/+384 |
| 40 | `tc89fmul` | +2.928% | +2.928% | +2.928% | +572 | +572 | +0/+0 |
| 41 | `tdirpat` | +2.794% | +2.794% | +2.794% | +3,452 | +3,452 | +384/+384 |
| 42 | `tpflio` | +2.019% | +2.020% | +2.018% | +11,020 | +11,020 | +128/+128 |
| 43 | `tfmadd` | +1.794% | +1.795% | +1.793% | +7,000 | +7,000 | +0/-128 |
| 44 | `tfmaddr` | +1.771% | +1.771% | +1.771% | +1,425 | +1,425 | -128/-128 |
| 45 | `tpihexb` | +1.769% | +1.770% | +1.769% | +2,878 | +2,878 | +0/+0 |
| 46 | `tfmaf` | +1.767% | +1.767% | +1.767% | +1,838 | +1,838 | +0/+0 |
| 47 | `tasinfsp` | +1.748% | +1.748% | +1.748% | +2,275 | +2,275 | +0/+0 |
| 48 | `tctxops` | +1.732% | +1.741% | +1.724% | +1,807 | +1,807 | +0/+0 |
| 49 | `tpowfsp` | +1.726% | +1.726% | +1.726% | +3,783 | +3,783 | +0/+0 |
| 50 | `tpfauto` | +1.639% | +1.639% | +1.639% | +5,280 | +5,280 | +0/+0 |

The largest cycle regression is `tqsort`, whose added safety checks dominate
the aggregate increase. Conversely, `tpi`, `sieve`, `pint`, and `cobint`
contain the largest measured absolute or percentage improvements.
## Validation

- Canonical all-tool build passed.
- Strict focused peep/nopeep tests passed for every added regression.
- Strict full+extended stack-check suite: 410 found, 400 passed, 10 skipped,
  zero failed, with zero checked performance regressions.
- Strict full+extended no-stack-check suite: 410 found, 400 passed, 10 skipped,
  zero failed.
- Diagnostics, 31 dccpeep fixtures, and the extended C suite passed.
- Runtime IY safety and coverage passed: 408 public labels reconciled, 173
  mapped APIs covered, and zero unexpected labels.
- Repository script tests: 44 passed.
- Strict MkDocs build passed.
- Full runtime assembly and JR-range checks passed.
