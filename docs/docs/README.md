# dcc MkDocs site

This folder contains the MkDocs configuration and English documentation source
for the dcc documentation site.

## Layout

- `mkdocs.yml` — MkDocs configuration.
- `requirements.txt` — Python packages needed to build the site.
- `en/` — English documentation pages.
- `hooks/copy_assets.py` — build hook that copies shared repo assets into the
  docs source tree before MkDocs validates links.
- `hooks/runtime_sizes.py` — build hook that generates runtime size tables from
  `DCCRTL.MAC`.
- `hooks/stdlib_tables.py` — build hook that generates standard-library
  function and symbol summaries from public header declarations and
  Doxygen-style summary comments.

The benchmark chart is maintained once at the repo root as `images/table.jpg`.
During a docs build, `hooks/copy_assets.py` copies it to
`en/images/table.jpg`, which is ignored by git.

## Install dependencies

From this directory:

```sh
python3 -m venv ../../.env
../../.env/bin/python -m pip install -r requirements.txt
```

## Build the site

From this directory:

```sh
../../.env/bin/mkdocs build --strict
```

The generated site is written to `../site` (`docs/site` from the repo root),
which is ignored by git.

## Serve locally

From this directory:

```sh
../../.env/bin/mkdocs serve
```

MkDocs prints the local URL, usually `http://127.0.0.1:8000/`.

## Deploy

The GitHub Actions workflow in `.github/workflows/docs.yml` builds this MkDocs
site, uploads it as a GitHub Pages artifact, and deploys it from the default
branch with `actions/deploy-pages`.

## Updating runtime size tables

The runtime size tables on the *Runtime function sizes* appendix page are
**generated at build time** by the `hooks/runtime_sizes.py` MkDocs hook, which
analyses `DCCRTL.MAC` directly. There is nothing to regenerate by hand: edit the
runtime and rebuild the docs, and the tables update themselves. To preview:

```sh
../../.env/bin/mkdocs build --strict
```

## Updating generated standard-library tables

The function tables on standard-library reference pages are generated at build
time by `hooks/stdlib_tables.py`. Edit the prototype and its Doxygen-style
summary comment in the public header (for example `../../stdio.h`,
`../../stdlib.h`, `../../string.h`, `../../ctype.h`, `../../math.h`, or
`../../stddef.h`), then run a strict docs build to preview the updated table. Use
markers such as `<!-- STDIO-FUNCTION-TABLE: all -->` or
`<!-- MATH-FUNCTION-TABLE: all -->` to include every documented prototype,
sorted by function name.

The same hook also generates types, macros, streams, and other non-function
symbol tables from documented declarations. Use markers such as
`<!-- STDIO-SYMBOL-TABLE: all -->` to include symbols in header order, or
`<!-- MATH-SYMBOL-TABLE: all sorted -->` when an alphabetical symbol table reads
better. Other standard-library pages use the same hook with prefixes such as
`STDLIB`, `STRING`, `CTYPE`, `ASSERT`, `ERRNO`, `FLOAT`, `LIMITS`, `SETJMP`,
`STDARG`, `STDBOOL`, `STDDEF`, and `STDINT`.
