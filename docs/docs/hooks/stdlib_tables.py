"""MkDocs hook: inject standard-library tables from public C headers.

Pages can contain marker comments such as

    <!-- STDIO-FUNCTION-TABLE: all -->
    <!-- MATH-SYMBOL-TABLE: all -->

The marker prefix selects the header, and the table kind selects public function
prototypes or documented non-function symbols.  Function tables are sorted by
function name; symbol tables preserve header order.  Declarations and summaries
come from the header source, where each documented declaration has a preceding
Doxygen-style summary comment.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Mapping

MARKER_RE = re.compile(r"<!--\s*(?P<prefix>[A-Z][A-Z0-9]*)-(?P<kind>FUNCTION|SYMBOL)-TABLE\s*:\s*(?P<names>.*?)\s*-->")
FUNCTION_RE = re.compile(r"\b(?P<name>[A-Za-z_]\w*)\s*\(")
DEFINE_RE = re.compile(r"^#\s*define\s+(?P<name>[A-Za-z_]\w*)\b")
TYPEDEF_FUNCTION_PTR_RE = re.compile(r"^typedef\b.*\(\s*\*\s*(?P<name>[A-Za-z_]\w*)\s*\)\s*\([^;]*\)\s*;")
TYPEDEF_RE = re.compile(r"^typedef\b.*\b(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;")
EXTERN_RE = re.compile(r"^extern\b.*\b(?P<name>[A-Za-z_]\w*)\s*;")
STRUCT_RE = re.compile(r"^struct\s+(?P<name>[A-Za-z_]\w*)\s*\{")

HEADER_BY_PREFIX: Mapping[str, str] = {
    "STDIO": "stdio.h",
    "MATH": "math.h",
    "STDLIB": "stdlib.h",
    "STRING": "string.h",
    "CTYPE": "ctype.h",
    "ASSERT": "assert.h",
    "ERRNO": "errno.h",
    "FLOAT": "float.h",
    "LIMITS": "limits.h",
    "LOCALE": "locale.h",
    "SIGNAL": "signal.h",
    "SETJMP": "setjmp.h",
    "STDARG": "stdarg.h",
    "STDBOOL": "stdbool.h",
    "STDDEF": "stddef.h",
    "STDINT": "stdint.h",
    "TIME": "time.h",
}


@dataclass(frozen=True)
class DocEntry:
    signature: str
    summary: str


def _split_names(text: str, marker_name: str) -> List[str]:
    names = [name for name in re.split(r"[\s,]+", text.strip()) if name]
    if not names:
        raise ValueError(f"{marker_name} marker must name at least one symbol")
    return names


def _resolve_function_names(text: str, entries: Dict[str, DocEntry], marker_name: str) -> List[str]:
    names = _split_names(text, marker_name)
    if names[0].lower() == "all":
        if len(names) > 2 or (len(names) == 2 and names[1].lower() not in ("sort", "sorted", "alpha", "alphabetical")):
            raise ValueError(f"{marker_name} marker has unknown option after all: {' '.join(names[1:])}")
        return sorted(entries, key=str.lower)
    return sorted(dict.fromkeys(names), key=str.lower)


def _resolve_symbol_names(text: str, entries: Dict[str, DocEntry], marker_name: str) -> List[str]:
    names = _split_names(text, marker_name)
    if names[0].lower() == "all":
        if len(names) == 1:
            return list(entries.keys())
        if len(names) == 2 and names[1].lower() in ("sort", "sorted", "alpha", "alphabetical"):
            return sorted(entries, key=str.lower)
        raise ValueError(f"{marker_name} marker has unknown option after all: {' '.join(names[1:])}")
    if names[0].lower() in ("sort", "sorted", "alpha", "alphabetical"):
        return sorted(dict.fromkeys(names[1:]), key=str.lower)
    return list(dict.fromkeys(names))


def _normalise_signature(signature: str) -> str:
    return re.sub(r"\s+", " ", signature.strip())


def _markdown_escape(text: str) -> str:
    return text.replace("|", "\\|")


def _clean_doxygen_line(line: str) -> str:
    text = line.strip()
    if text.startswith("/**"):
        text = text[3:]
    if text.endswith("*/"):
        text = text[:-2]
    text = text.strip()
    if text.startswith("*"):
        text = text[1:].strip()
    if text.startswith("@brief"):
        text = text[6:].strip()
    return text


def _extract_doxygen_summary(lines: List[str], start: int) -> tuple[str, int]:
    parts: List[str] = []
    index = start
    while index < len(lines):
        raw = lines[index]
        parts.append(_clean_doxygen_line(raw))
        if "*/" in raw:
            break
        index += 1
    summary = " ".join(part for part in parts if part)
    return _normalise_signature(summary), index + 1


def _non_function_symbol_name(declaration: str) -> str:
    for pattern in (DEFINE_RE, TYPEDEF_FUNCTION_PTR_RE, TYPEDEF_RE, EXTERN_RE, STRUCT_RE):
        match = pattern.match(declaration)
        if match:
            return match.group("name")
    return ""


def _parse_header_functions(header: Path) -> Dict[str, DocEntry]:
    entries: Dict[str, DocEntry] = {}
    pending_summary = ""
    lines = header.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line_number = index + 1
        line = lines[index]
        stripped = line.strip()
        if stripped.startswith("/**"):
            pending_summary, index = _extract_doxygen_summary(lines, index)
            continue
        if pending_summary and _non_function_symbol_name(stripped):
            pending_summary = ""
            index += 1
            continue
        if not stripped or stripped.startswith("/*") or stripped.startswith("*") or stripped.startswith("#"):
            index += 1
            continue
        if "(" not in stripped or ");" not in stripped:
            index += 1
            continue

        declaration = stripped
        if not declaration.endswith(";"):
            index += 1
            continue
        match = FUNCTION_RE.search(declaration)
        if not match:
            index += 1
            continue
        if not pending_summary:
            raise ValueError(f"{header}:{line_number}: function prototype lacks a Doxygen summary")

        name = match.group("name")
        signature = _normalise_signature(declaration[:-1])
        entries[name] = DocEntry(signature=signature, summary=pending_summary)
        pending_summary = ""
        index += 1

    return entries


def _parse_header_symbols(header: Path) -> Dict[str, DocEntry]:
    entries: Dict[str, DocEntry] = {}
    pending_summary = ""
    lines = header.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if stripped.startswith("/**"):
            pending_summary, index = _extract_doxygen_summary(lines, index)
            continue

        declaration = stripped
        if pending_summary and stripped.startswith("typedef") and ";" not in stripped:
            is_record_typedef = stripped.startswith("typedef struct") or stripped.startswith("typedef union")
            lookahead = index + 1
            while lookahead < len(lines):
                next_line = lines[lookahead].strip()
                declaration += " " + next_line
                if next_line.endswith(";") and (not is_record_typedef or "}" in next_line):
                    break
                lookahead += 1
            index = lookahead

        name = _non_function_symbol_name(declaration)
        if name:
            if pending_summary:
                signature = f"struct {name}" if STRUCT_RE.match(declaration) else name
                entries[name] = DocEntry(signature=signature, summary=pending_summary)
            pending_summary = ""
        index += 1

    return entries


def _render_function_table(names: Iterable[str], entries: Dict[str, DocEntry]) -> str:
    out = ["| Function | Summary |", "| --- | --- |"]
    for name in names:
        entry = entries.get(name)
        if entry is None:
            raise ValueError(f"function summary requested unknown function: {name}")
        signature = _markdown_escape(entry.signature)
        summary = _markdown_escape(entry.summary)
        out.append(f"| `{signature}` | {summary} |")
    return "\n".join(out)


def _render_symbol_table(names: Iterable[str], entries: Dict[str, DocEntry]) -> str:
    out = ["| Name | Meaning |", "| --- | --- |"]
    for name in names:
        entry = entries.get(name)
        if entry is None:
            raise ValueError(f"symbol summary requested unknown name: {name}")
        symbol = _markdown_escape(entry.signature)
        summary = _markdown_escape(entry.summary)
        out.append(f"| `{symbol}` | {summary} |")
    return "\n".join(out)


def _header_path(prefix: str, config) -> Path:
    header_name = HEADER_BY_PREFIX.get(prefix)
    if header_name is None:
        known = ", ".join(sorted(HEADER_BY_PREFIX))
        raise ValueError(f"unknown stdlib table prefix {prefix!r}; known prefixes: {known}")
    repo_root = Path(config.config_file_path).resolve().parents[2]
    header = repo_root / header_name
    if not header.exists():
        raise FileNotFoundError(f"{header_name} not found: {header}")
    return header


def _generate_table(prefix: str, kind: str, names: str, config) -> str:
    marker_name = f"{prefix}-{kind}-TABLE"
    header = _header_path(prefix, config)
    if kind == "FUNCTION":
        entries = _parse_header_functions(header)
        selected = _resolve_function_names(names, entries, marker_name)
        return _render_function_table(selected, entries)
    if kind == "SYMBOL":
        entries = _parse_header_symbols(header)
        selected = _resolve_symbol_names(names, entries, marker_name)
        return _render_symbol_table(selected, entries)
    raise ValueError(f"unsupported stdlib table kind: {kind}")


def on_page_markdown(markdown, page, config, files):
    if "-FUNCTION-TABLE" not in markdown and "-SYMBOL-TABLE" not in markdown:
        return markdown

    def replace_marker(match: re.Match[str]) -> str:
        return _generate_table(match.group("prefix"), match.group("kind"), match.group("names"), config)

    return MARKER_RE.sub(replace_marker, markdown)
