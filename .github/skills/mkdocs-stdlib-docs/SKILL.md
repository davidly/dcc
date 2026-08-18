---
name: mkdocs-stdlib-docs
description: 'Create, update, review, or debug the dcc MkDocs standard-library reference pages generated from public C headers. Use when working on docs/docs/en/* standard C library pages, docs/docs/hooks/* table-generation hooks, Doxygen-style summaries in stdio.h/stdlib.h/string.h/ctype.h/math.h and related headers, or requests to make headers the source of truth for MkDocs API/function/symbol tables. Covers conventional C reference layout, generated marker comments, strict MkDocs validation, and cross-checking docs against DCCRTL.MAC and compiler mappings.'
argument-hint: 'Describe the stdlib docs task (new generated page, update header summaries, add hook, validate docs)'
---

# MkDocs Standard-Library Docs

Use this skill when maintaining the dcc MkDocs pages for standard C library
headers and runtime-backed APIs. The goal is that each public header is the
source of truth for the generated reference tables, while each Markdown page
keeps the human-written behavior notes, dcc deviations, examples, and caveats.

## When To Use

- Updating pages under `docs/docs/en/` for standard C headers such as
  `stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `math.h`, `stdarg.h`,
  `stddef.h`, `stdbool.h`, `stdint.h`, `limits.h`, or `float.h`.
- Adding or generalizing MkDocs hooks under `docs/docs/hooks/` that generate
  function, type, macro, or stream tables from headers.
- Adding Doxygen-style summaries to public header declarations so generated docs
  do not drift from the implemented declarations.
- Checking a library docs page against `DCCRTL.MAC`, `src/dcc/dcc_asmname.c`,
  compiler options, or runtime behavior.
- Reordering a library page to follow conventional C reference structure while
  preserving dcc-specific runtime notes.

Do not use this skill for ordinary CP/M app code; use `dcc-cpm-z80`. Do not use
it for compiler/runtime implementation changes except when docs must be checked
against those implementation details; use `dcc-project` for the code change.

## Source-Of-Truth Rules

- Public declaration tables should be generated from the public header whenever
  practical. Do not hand-maintain a long Markdown API table if the header can be
  parsed deterministically.
- Put a short Doxygen-style summary immediately before each public declaration
  that should appear in generated docs:

```c
/** Parse formatted input from a stream. */
int fscanf(FILE *stream, const char *format, ...);
```

- Use Doxygen summaries for documented non-function symbols too:

```c
/** End-of-file / error return value, -1. */
#define EOF (-1)
```

- Keep generated tables short and factual. Put nuanced behavior, limitations,
  examples, option requirements, and runtime caveats in the Markdown page below
  the generated tables.
- Never add maintainer-facing implementation notes to user docs unless they help
  users write correct dcc programs. Keep hook mechanics in `docs/docs/README.md`.

## Generated Table Markers

Current stdio markers are:

```md
<!-- STDIO-SYMBOL-TABLE: all -->
<!-- STDIO-FUNCTION-TABLE: all -->
```

For new library hooks, prefer the same pattern with the uppercase header stem:

```md
<!-- STDLIB-SYMBOL-TABLE: all -->
<!-- STDLIB-FUNCTION-TABLE: all -->
<!-- STRING-FUNCTION-TABLE: all -->
<!-- CTYPE-FUNCTION-TABLE: all -->
<!-- MATH-FUNCTION-TABLE: all -->
<!-- ASSERT-SYMBOL-TABLE: all -->
<!-- ERRNO-SYMBOL-TABLE: all -->
<!-- FLOAT-SYMBOL-TABLE: all -->
<!-- LIMITS-SYMBOL-TABLE: all -->
<!-- LOCALE-SYMBOL-TABLE: all -->
<!-- SETJMP-SYMBOL-TABLE: all -->
<!-- SIGNAL-SYMBOL-TABLE: all -->
<!-- STDARG-SYMBOL-TABLE: all -->
<!-- STDBOOL-SYMBOL-TABLE: all -->
<!-- STDDEF-SYMBOL-TABLE: all -->
<!-- STDINT-SYMBOL-TABLE: all -->
<!-- TIME-SYMBOL-TABLE: all -->
```

Marker behavior should be consistent across libraries:

- `all` means every documented declaration of that kind.
- Function tables sort by function name, case-insensitively.
- Symbol tables preserve header order by default, because macro/type order often
  carries conceptual meaning. Use `all sorted` when an alphabetical symbol table
  reads better, such as `MATH-SYMBOL-TABLE` standard C aliases.
- Explicit marker lists accept comma or whitespace separated names and de-dupe
  while preserving the intended order, except function tables may sort if the
  page convention calls for sorted reference tables.
- Missing requested names, undocumented public prototypes, and malformed markers
  should raise build-time errors, not silently produce stale docs.

## Hook Design

When adding a new generated stdlib table, prefer extending `stdlib_tables.py`
rather than copying one hook per header. A good generic shape is:

- One parser for public header declarations and their nearest preceding
  Doxygen summary.
- A library configuration table mapping marker prefixes to header paths, for
  example `STDIO -> ../../stdio.h`, `STDLIB -> ../../stdlib.h`.
- Declaration kinds for functions, macros, typedefs, extern objects, and any
  special category that a page genuinely needs.
- Renderer functions that emit stable two-column Markdown tables.
- Clear error messages that include the header path and line number.

Use structured parsing where it is reasonable. For these simple public headers,
small regex-based declaration parsing is acceptable if it is deliberately scoped
and validated with strict builds. Avoid ad hoc one-off string slicing in page
Markdown.

## Conventional Page Layout

For a standard-library reference page, prefer this order unless the local page
has a strong reason to differ:

1. Title and one-sentence include/scope statement.
2. `## Types, Macros, and Streams` or the equivalent generated symbol table.
3. `## Functions` generated function table.
4. A short runtime model or dcc-specific overview, if needed.
5. Domain sections in conventional C-reference order.
6. Examples and caveats near the APIs they explain.
7. Detailed dcc-specific buffering, memory, size, or runtime notes after the
   main reference flow.

For `stdio.h`, use this specific order:

1. Types, macros, and streams.
2. Functions.
3. Runtime model.
4. File streams.
5. Formatted I/O: `printf` output, `printf` conversions, then `scanf` input.
6. Character and string I/O.
7. Block I/O.
8. Positioning and status.
9. Console output buffering.

Keep formatted input and formatted output adjacent. They are separate runtime
engines in dcc, but C readers expect to find them together.

## Runtime And Compiler Cross-Checks

Before claiming support or limitations, check the implementation:

- `DCCRTL.MAC` for runtime entry points and exact behavior.
- `src/dcc/dcc_asmname.c` for compiler C-name to runtime-symbol mappings.
- `src/dcc/dcc.c` for user-facing compiler option help.
- Header declarations for the public surface.
- Existing tests under `tests/` when behavior is subtle or recently changed.

For formatted I/O, verify both conversion support and build-option requirements.
In this repo, for example, `-fl` / `-flongio` enables long `printf`-family
formats, while `-f` / `-ffloatio` enables `%f` for `printf` itself and does not
add floating-point `scanf` input.

## Documentation Style

- Use conventional C reference names: `Functions`, `Types, Macros, and Streams`,
  `Formatted I/O`, `File streams`, and similar user-facing terms.
- Prefer `Functions` over `APIs` in page headings and marker names.
- Keep generated table summaries one sentence and implementation-neutral.
- Use notes, tips, or warnings for dcc deviations that affect program behavior,
  portability, or code size.
- Include examples only when they clarify a dcc-specific pattern or common use.
- Keep Markdown anchors stable when renaming sections; update all inbound links.

## Validation Checklist

After changes, run from the repo root or docs directory as appropriate:

```sh
cd docs/docs && /tmp/dcc-docs-mkdocs-venv/bin/mkdocs build --strict
```

If that venv does not exist, use the project docs requirements:

```sh
cd docs/docs
python3 -m venv /tmp/dcc-docs-mkdocs-venv
/tmp/dcc-docs-mkdocs-venv/bin/python -m pip install -r requirements.txt
/tmp/dcc-docs-mkdocs-venv/bin/mkdocs build --strict
```

Also check:

- No diagnostics in changed Markdown, headers, or hook files.
- No stale anchors after heading renames: search for old `#anchor` strings.
- Generated tables render in `docs/site/...` when marker behavior changed.
- User docs do not mention hook internals except where intentionally linked from
  `docs/docs/README.md`.
- If compiler help or mappings changed, rebuild host tools with `./mmacos.sh` on
  macOS or the platform-appropriate build script.

## Files To Update Together

When introducing a new generated stdlib page or table, usually update all of:

- The public header at repo root, with Doxygen summaries.
- The Markdown page under `docs/docs/en/` with marker comments and behavior
  sections.
- A hook under `docs/docs/hooks/`, preferably generic or library-configured.
- `docs/docs/mkdocs.yml` to register a new hook if needed.
- `docs/docs/README.md` with maintainer instructions for the generated tables.
- Any links from examples, limitations, or utility-header pages that point at
  renamed headings.
