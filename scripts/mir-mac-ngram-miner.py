#!/usr/bin/env python3
"""Mine repeated Z80 instruction n-grams across a MIR fallback population.

Formalizes the ad hoc n-gram mining performed by hand across several batches
of the MIR migration: compile every function in a chosen census `reason`
bucket, extract its generated (legacy AST, i.e. currently-fallback) assembly
body, strip labels/comments, and report the most frequently repeated
contiguous instruction n-grams across the whole population, with example
call sites so a candidate pattern can be inspected in context.

This does not attempt to interpret the pattern semantically - it is a lead
generator for a human (or agent) to inspect concrete contexts before writing
a new selector/gate, exactly the workflow already used for text-size and
boolean-phi-cost mining in prior batches.

Usage:
    python3 scripts/mir-mac-ngram-miner.py census.tsv --reason text-size \
        --min-n 3 --max-n 6 --top 20
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path


ASM_LINE_RE = re.compile(r"^\s*([a-zA-Z][a-zA-Z0-9']*)\b")
LABEL_RE = re.compile(r"^[A-Za-z_.$][A-Za-z0-9_.$]*:\s*$")
# Matches a numeric/hex literal operand so it can be normalized to `N`,
# keeping instruction shape comparable across functions with different
# constants (e.g. `ld a,5` and `ld a,80` become the same gram element).
NUMERIC_OPERAND_RE = re.compile(
    r"(?<![A-Za-z_])(-?\d+|0[fF]?[hH]|[0-9][0-9a-fA-F]*[hH])(?![A-Za-z_])"
)
# Matches a bracketed/plain identifier operand (label, symbol) so distinct
# call targets/labels collapse to one placeholder for shape comparison.
IDENT_OPERAND_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]{2,}\b")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tsv", help="census TSV produced by mir-migration-census.py")
    parser.add_argument("--reason", required=True, help="fallback reason bucket to mine")
    parser.add_argument("--compiler", default="./dcc")
    parser.add_argument("--tests-dir", default="tests")
    parser.add_argument("--stack", type=int, default=512)
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--extra-args", default="", help="space-separated extra dcc args")
    parser.add_argument("--min-n", type=int, default=3)
    parser.add_argument("--max-n", type=int, default=6)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument(
        "--max-functions", type=int, default=0,
        help="cap the number of functions compiled (0 = no cap; use for a quick pass)",
    )
    return parser.parse_args()


def load_bucket(tsv_path: str, reason: str) -> list[tuple[str, str]]:
    pairs = []
    with open(tsv_path, newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row.get("reason") == reason and row.get("result") != "mir":
                pairs.append((row["app"], row["function"]))
    return pairs


def compile_app(compiler: str, tests_dir: str, app: str, stack: int,
                 timeout: int, extra_args: list[str], out_dir: Path) -> str | None:
    source = Path(tests_dir) / f"{app}.c"
    if not source.exists():
        return None
    output = out_dir / f"{app.upper()}.MAC"
    command = [compiler, "-stack", str(stack), "-I", tests_dir, *extra_args,
               str(source), "-o", str(output)]
    try:
        completed = subprocess.run(
            command, text=True, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired:
        return None
    if completed.returncode != 0 or not output.exists():
        return None
    return output.read_text(errors="replace")


def normalize_instruction(stripped: str) -> str:
    """Fold varying literals/identifiers so shape (not exact value) matches.

    `ld a,5` and `ld a,80` both become `ld a,N`; `call _F123` and
    `call _F456` both become `call SYM`. This lets the miner find repeated
    *structural* idioms (e.g. a load/compare/branch triplet) across
    functions that operate on different constants or call different
    helpers, which is the useful signal for spotting a missing selector
    case rather than exact byte-identical duplication.
    """
    parts = stripped.split(None, 1)
    mnemonic = parts[0].lower()
    operands = parts[1] if len(parts) > 1 else ""
    operands = NUMERIC_OPERAND_RE.sub("N", operands)
    operands = IDENT_OPERAND_RE.sub(
        lambda m: m.group(0) if m.group(0).lower() in Z80_REGISTER_NAMES else "SYM",
        operands,
    )
    text = f"{mnemonic} {operands}" if operands else mnemonic
    return re.sub(r"\s+", " ", text).strip().lower()


Z80_REGISTER_NAMES = {
    "a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "ix", "iy",
    "sp", "pc", "nz", "z", "nc", "po", "pe", "p", "m",
}


def extract_function_body(mac_text: str, function: str) -> list[str] | None:
    """Return the stripped, normalized instruction lines for one function.

    dcc names the assembler label for a C function `_ZNNNN:` and precedes it
    with a `; static function NAME` or `; function NAME` comment for
    non-static/static functions respectively; the body runs until the next
    such comment or end of file.
    """
    lines = mac_text.splitlines()
    marker_re = re.compile(
        r"^;\s*(?:static\s+)?function\s+" + re.escape(function) + r"\s*$"
    )
    start = None
    for i, line in enumerate(lines):
        if marker_re.match(line.strip()):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if re.match(r"^;\s*(?:static\s+)?function\s+\S", lines[i].strip()):
            end = i
            break
    body = []
    for line in lines[start:end]:
        stripped = line.strip()
        if not stripped or stripped.startswith(";"):
            continue
        if LABEL_RE.match(stripped):
            continue
        if not ASM_LINE_RE.match(stripped):
            continue
        # Drop raw data directives (string/byte tables) - not instructions.
        if re.match(r"^db\b", stripped, re.IGNORECASE):
            continue
        # Strip a trailing inline comment before normalizing.
        code = stripped.split(";", 1)[0].strip()
        if not code:
            continue
        body.append(normalize_instruction(code))
    return body


def collect_ngrams(mnemonics: list[str], min_n: int, max_n: int,
                    counter: Counter, examples: dict) -> None:
    for n in range(min_n, max_n + 1):
        for i in range(len(mnemonics) - n + 1):
            gram = tuple(mnemonics[i:i + n])
            counter[gram] += 1


def main() -> int:
    args = parse_args()
    extra_args = args.extra_args.split() if args.extra_args else []
    bucket = load_bucket(args.tsv, args.reason)
    if args.max_functions:
        bucket = bucket[: args.max_functions]
    if not bucket:
        print(f"No fallback candidates found for reason={args.reason!r}", file=sys.stderr)
        return 1

    by_app: dict[str, list[str]] = defaultdict(list)
    for app, function in bucket:
        by_app[app].append(function)

    counter: Counter = Counter()
    gram_examples: dict[tuple, list[str]] = defaultdict(list)
    compiled = 0
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        for app, functions in by_app.items():
            mac_text = compile_app(args.compiler, args.tests_dir, app,
                                    args.stack, args.timeout, extra_args, out_dir)
            if mac_text is None:
                continue
            for function in functions:
                body = extract_function_body(mac_text, function)
                if not body:
                    continue
                compiled += 1
                before = len(counter)
                collect_ngrams(body, args.min_n, args.max_n, counter, gram_examples)
                for n in range(args.min_n, args.max_n + 1):
                    for i in range(len(body) - n + 1):
                        gram = tuple(body[i:i + n])
                        if len(gram_examples[gram]) < 3:
                            gram_examples[gram].append(f"{app}:{function}")

    print(f"Compiled {compiled} functions from reason={args.reason!r} "
          f"({len(bucket)} candidates, {len(by_app)} apps)", file=sys.stderr)

    # Rank by (occurrence count * n-gram length) as a rough "bytes saved"
    # proxy, longest/most-frequent patterns first.
    ranked = sorted(
        counter.items(),
        key=lambda kv: (kv[1] * len(kv[0]), kv[1], len(kv[0])),
        reverse=True,
    )
    seen_prefixes: set[tuple] = set()
    printed = 0
    for gram, count in ranked:
        if count < 2:
            continue
        # Skip grams that are a strict prefix of an already-printed longer
        # gram with the same count (redundant with the longer pattern).
        if gram in seen_prefixes:
            continue
        print(f"count={count:4d} len={len(gram)}  {' '.join(gram)}")
        print(f"    examples: {', '.join(gram_examples[gram])}")
        for n in range(1, len(gram)):
            seen_prefixes.add(gram[:n])
            seen_prefixes.add(gram[-n:])
        printed += 1
        if printed >= args.top:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
