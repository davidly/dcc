#!/usr/bin/env python3
"""Audit DCCRTL public runtime symbols against unit-test source coverage.

The audit is based on the public headers plus dcc's C-name to assembler-name
mapping in src/dcc/dcc_asmname.c. It also accounts for public formatted-I/O
runtime variants selected by dcc_floatio/dcc_longio in tests/_test_overrides.json.
"""

import argparse
import json
import re
from pathlib import Path


IGNORE_HEADERS = {
    "stdarg.h",
    "stdbool.h",
    "stddef.h",
    "stdint.h",
    "float.h",
    "limits.h",
    "errno.h",
}

FMT_FUNCS = [
    "printf",
    "fprintf",
    "sprintf",
    "snprintf",
    "vprintf",
    "vfprintf",
    "vsprintf",
    "vsnprintf",
]
FMT_PUBLIC = [
    "_printf",
    "_pffio",
    "_pflng",
    "_pflio",
    "_sprintf",
    "_spfio",
    "_splng",
    "_splio",
    "_snfio",
    "_snlng",
    "_snlio",
    "_fprintf",
    "_fpfio",
    "_fplng",
    "_fplio",
    "_vprintf",
    "_vpfio",
    "_vplng",
    "_vplio",
    "_vsprintf",
    "_vsfio",
    "_vslng",
    "_vslio",
    "_vnfio",
    "_vnlng",
    "_vnlio",
    "_vfprintf",
    "_vffio",
    "_vflng",
    "_vflio",
]
KNOWN_PUBLIC_DATA = {"_stdin", "_stdout", "_stderr", "_errno", "_dcc_nan"}

# Public labels that are compiler/runtime internals rather than user-callable C
# APIs. They are reached by generated code (integer/long/float arithmetic,
# register and stack-frame helpers), are printf/scanf sub-labels (pf_*/sc_*), or
# are startup entry/data symbols. None can be called by a C name, so they are
# exercised indirectly by higher-level tests, not by a direct call to the label.
INTERNAL_LABEL_PREFIXES = ("__", "pf_", "sc_")
INTERNAL_LABEL_NAMES = {"start", "argv", "FPACKR", "FUNR", "FPKR", "FSHR"}


def is_internal_label(sym):
    return sym.startswith(INTERNAL_LABEL_PREFIXES) or sym in INTERNAL_LABEL_NAMES


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n\r]*", " ", text)
    return text


def read_runtime_public_labels(root):
    rtl_lines = (root / "DCCRTL.MAC").read_text(errors="replace").splitlines()
    publics = set()
    labels = {}

    for line_no, line in enumerate(rtl_lines, 1):
        code = line.split(";", 1)[0]
        public_match = re.search(r"\bpublic\s+(.+)", code, re.I)
        if public_match:
            publics.update(
                sym
                for sym in re.split(r"[\s,]+", public_match.group(1).strip())
                if sym
            )
        label_match = re.match(r"^([A-Za-z_?$][A-Za-z0-9_?$]*):", code)
        if label_match:
            labels[label_match.group(1)] = line_no

    return publics & set(labels), labels


def read_compiler_runtime_map(root):
    asm = (root / "src/dcc/dcc_asmname.c").read_text(errors="replace")
    map_block = asm.split("const char *asm_name_for_runtime", 1)[1].split(
        "const char *asm_name_for", 1
    )[0]
    return dict(
        re.findall(
            r'!strcmp\(cname,\s*"([^"]+)"\)\)\s*return\s*"([^"]+)"',
            map_block,
        )
    )


def asm_name_for(cname, compiler_map):
    return compiler_map.get(cname, "_" + cname)


def read_public_header_functions(root):
    api_to_declared = {}
    macro_aliases = {}

    for header in sorted(root.glob("*.h")):
        if header.name in IGNORE_HEADERS:
            continue

        text = header.read_text(errors="replace")
        for api, target in re.findall(
            r"^\s*#\s*define\s+([A-Za-z_]\w*)(?:\([^)]*\))?\s+([A-Za-z_]\w*)\b",
            text,
            re.M,
        ):
            if api != target:
                macro_aliases.setdefault(target, set()).add(api)

        no_comments = strip_c_comments(text)
        decl_text = " ".join(
            line.strip()
            for line in no_comments.splitlines()
            if line.strip()
            and not line.strip().startswith("#")
            and not line.strip().startswith("typedef")
        )

        for decl in decl_text.split(";"):
            if "(" not in decl:
                continue

            fp_return = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\(", decl)
            if fp_return:
                name = fp_return.group(1)
            else:
                before = decl.split("(", 1)[0]
                names = re.findall(r"[A-Za-z_]\w*", before)
                if not names:
                    continue
                name = names[-1]

            if name in {"FILE", "DIR"}:
                continue
            api_to_declared.setdefault(name, set()).add(header.name)

    return api_to_declared, macro_aliases


def read_test_text(root):
    return {
        path: strip_c_comments(path.read_text(errors="replace"))
        for path in sorted((root / "tests").glob("*.c"))
    }


def read_overrides(root):
    override_path = root / "tests/_test_overrides.json"
    if not override_path.exists():
        return {}
    return {item["name"]: item for item in json.loads(override_path.read_text())["apps"]}


def app_flags(overrides, app):
    entry = overrides.get(app, {})
    return (
        bool(entry.get("dcc_floatio", True)),
        bool(entry.get("dcc_longio", True)),
        bool(entry.get("ignore", False)),
    )


def formatted_label(func, floatio, longio):
    variants = {
        "printf": ("_printf", "_pflng", "_pffio", "_pflio"),
        "sprintf": ("_sprintf", "_splng", "_spfio", "_splio"),
        "snprintf": ("_snprintf", "_snlng", "_snfio", "_snlio"),
        "fprintf": ("_fprintf", "_fplng", "_fpfio", "_fplio"),
        "vprintf": ("_vprintf", "_vplng", "_vpfio", "_vplio"),
        "vsprintf": ("_vsprintf", "_vslng", "_vsfio", "_vslio"),
        "vsnprintf": ("_vsnprintf", "_vnlng", "_vnfio", "_vnlio"),
        "vfprintf": ("_vfprintf", "_vflng", "_vffio", "_vflio"),
    }
    return variants[func][(2 if floatio else 0) + (1 if longio else 0)]


def audit_api_coverage(root, public_labels, labels, compiler_map, test_text):
    api_to_declared, macro_aliases = read_public_header_functions(root)
    search_names = {
        name: {name} | macro_aliases.get(name, set()) for name in api_to_declared
    }

    # assert() macro covers the runtime failure helper.
    search_names.setdefault("_asfl", {"_asfl"})
    search_names["_asfl"].add("assert")

    runtime_funcs = {}
    for name, headers in api_to_declared.items():
        aname = asm_name_for(name, compiler_map)
        if aname in public_labels:
            runtime_funcs[name] = (aname, sorted(headers), labels[aname])

    if "__asfl" in public_labels:
        runtime_funcs["_asfl"] = ("__asfl", ["assert.h"], labels["__asfl"])

    coverage = {}
    for name in runtime_funcs:
        names = sorted(search_names.get(name, {name}))
        patterns = [
            (
                candidate,
                re.compile(r"(?<![A-Za-z0-9_])" + re.escape(candidate) + r"\s*\("),
            )
            for candidate in names
        ]

        hits = []
        for path, text in test_text.items():
            used = [candidate for candidate, pattern in patterns if pattern.search(text)]
            if used:
                hits.append((str(path), ",".join(used)))
        coverage[name] = hits

    missing = [name for name, hits in coverage.items() if not hits]
    return runtime_funcs, coverage, search_names, missing


def audit_formatted_labels(public_labels, test_text, overrides):
    hits = {}
    for path, text in test_text.items():
        floatio, longio, ignored = app_flags(overrides, path.stem)
        for func in FMT_FUNCS:
            if re.search(r"(?<![A-Za-z0-9_])" + func + r"\s*\(", text):
                hits.setdefault(formatted_label(func, floatio, longio), []).append(
                    (str(path), func, ignored, floatio, longio)
                )

    missing = []
    rows = []
    for label in FMT_PUBLIC:
        if label not in public_labels:
            continue
        active = [entry for entry in hits.get(label, []) if not entry[2]]
        if active:
            rows.append((label, active[0]))
        else:
            missing.append(label)
            rows.append((label, None))
    return rows, missing


def read_std_macro_aliases(root, declared_functions):
    """Map standard function-like macro APIs to the runtime function they call.

    The project headers expose several standard C names as function-like macros
    rather than declarations: math.h aliases the C89 double names to the float
    runtime (fabs -> fabsf, sin -> sinf, ...), and ctype.h defines isgraph in
    terms of isprint/isspace. A dcc app can call these standard spellings, so
    they are part of the standard-library surface even though they are not plain
    declarations. Returns {api_name: underlying_declared_function}.
    """
    aliases = {}
    for header in sorted(root.glob("*.h")):
        if header.name in IGNORE_HEADERS:
            continue
        text = strip_c_comments(header.read_text(errors="replace"))
        # Function-like macros have '(' immediately after the name (no space),
        # which distinguishes them from object-like macros such as EOF (-1). The
        # body is confined to the same line so it cannot capture a later comment.
        for match in re.finditer(
            r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)\([^)]*\)[ \t]+([^\n]+)$",
            text,
            re.M,
        ):
            api, body = match.group(1), match.group(2)
            for ident in re.findall(r"[A-Za-z_]\w*", body):
                if ident != api and ident in declared_functions:
                    aliases[api] = ident
                    break
    return aliases


def audit_std_library_surface(root, runtime_funcs, coverage, test_text):
    """Audit the standard-library API surface a dcc app can call.

    Source of truth is the project's standard headers: every declared function
    plus the standard function-like macro aliases. Each surface API must resolve
    to a runtime function that has test coverage. Alias spellings that are never
    called directly (the underlying function is still covered) are reported as
    informational, not failures.
    """
    api_to_declared, _macro_aliases = read_public_header_functions(root)
    declared = set(api_to_declared)
    macro_aliases = read_std_macro_aliases(root, declared)

    surface = {}
    for name, headers in api_to_declared.items():
        surface[name] = ("function", name, sorted(headers))
    for api, target in macro_aliases.items():
        surface.setdefault(api, ("macro", target, []))
    # assert() is a special macro whose failure path calls the _asfl helper.
    if "_asfl" in runtime_funcs:
        surface.setdefault("assert", ("macro", "_asfl", ["assert.h"]))

    uncovered = []
    alias_not_called = []
    for api in sorted(surface):
        kind, underlying, _headers = surface[api]
        if underlying not in runtime_funcs:
            if kind == "function":
                uncovered.append((api, kind, underlying, "no public runtime label"))
            # A macro whose target is not a runtime function is a header-only
            # helper (e.g. STR_HELPER), not a standard-library API.
            continue
        if not coverage.get(underlying):
            uncovered.append((api, kind, underlying, "no test coverage"))
            continue
        if kind == "macro":
            pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(api) + r"\s*\(")
            if not any(pattern.search(text) for text in test_text.values()):
                alias_not_called.append((api, underlying))

    return surface, uncovered, alias_not_called


def main():
    parser = argparse.ArgumentParser(
        description="Audit public DCCRTL runtime API and formatted-I/O label coverage."
    )
    parser.add_argument(
        "root",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="dcc repository root (default: parent of scripts/)",
    )
    args = parser.parse_args()
    root = args.root.resolve()

    public_labels, labels = read_runtime_public_labels(root)
    compiler_map = read_compiler_runtime_map(root)
    test_text = read_test_text(root)
    overrides = read_overrides(root)

    runtime_funcs, coverage, search_names, missing_api = audit_api_coverage(
        root, public_labels, labels, compiler_map, test_text
    )

    print("API-level public runtime coverage")
    print(f"  compiler-mapped public header/runtime functions: {len(runtime_funcs)}")
    print(
        "  covered by tests/*.c API or mapped-name call: "
        f"{len(runtime_funcs) - len(missing_api)}"
    )
    print(f"  missing: {len(missing_api)}")
    for name in sorted(missing_api):
        aname, headers, line = runtime_funcs[name]
        searched = ",".join(sorted(search_names.get(name, {name})))
        print(
            f"    {name:12s} asm={aname:12s} line={line:5d} "
            f"headers={','.join(headers):16s} searched={searched}"
        )

    formatted_rows, missing_fmt = audit_formatted_labels(
        public_labels, test_text, overrides
    )

    print("\nFormatted-I/O public label coverage under tests/_test_overrides.json")
    for label, sample in formatted_rows:
        if sample is None:
            print(f"  {label:10s} MISSING")
        else:
            path, func, _ignored, floatio, longio = sample
            print(
                f"  {label:10s} covered by {path} via {func} "
                f"floatio={floatio} longio={longio}"
            )
    print(f"  missing formatted labels: {len(missing_fmt)}")

    header_asm = {entry[0] for entry in runtime_funcs.values()}
    fmt_asm = set(FMT_PUBLIC)

    # Reconcile EVERY public DCCRTL label so nothing is silently skipped: each
    # label is either a public C API (header-mapped), a formatted-I/O variant, a
    # public data object, a compiler/runtime internal, or genuinely unexpected.
    accounted = {
        "public C API (header-mapped)": [],
        "formatted-I/O variant": [],
        "public data object": [],
        "internal / not C-callable": [],
    }
    unexpected = []
    for sym in sorted(public_labels):
        if sym in header_asm:
            accounted["public C API (header-mapped)"].append(sym)
        elif sym in fmt_asm:
            accounted["formatted-I/O variant"].append(sym)
        elif sym in KNOWN_PUBLIC_DATA:
            accounted["public data object"].append(sym)
        elif is_internal_label(sym):
            accounted["internal / not C-callable"].append(sym)
        else:
            unexpected.append(sym)

    print("\nReconciliation of all DCCRTL public labels")
    print(f"  total public labels:             {len(public_labels)}")
    for category, syms in accounted.items():
        print(f"  {category:32s} {len(syms)}")
    print(f"  {'UNEXPECTED (unaccounted)':32s} {len(unexpected)}")
    for sym in unexpected:
        print(f"    {sym:14s} line={labels[sym]:5d}")

    reconciled = sum(len(syms) for syms in accounted.values()) + len(unexpected)
    assert reconciled == len(public_labels), (
        f"reconciliation mismatch: {reconciled} != {len(public_labels)}"
    )

    surface, uncovered_surface, alias_not_called = audit_std_library_surface(
        root, runtime_funcs, coverage, test_text
    )

    print("\nStandard C library header surface (dcc app-callable APIs)")
    print(f"  standard API names on the dcc app surface: {len(surface)}")
    print(
        "  APIs resolving to a covered runtime function: "
        f"{len(surface) - len(uncovered_surface)}"
    )
    print(f"  APIs with no covered runtime function: {len(uncovered_surface)}")
    for api, kind, underlying, why in uncovered_surface:
        print(f"    {api:14s} ({kind} -> {underlying}) {why}")
    print(
        "  standard alias spellings never called directly "
        f"(informational): {len(alias_not_called)}"
    )
    for api, underlying in alias_not_called:
        print(
            f"    {api:10s} -> {underlying:10s} "
            "underlying covered; alias spelling not exercised"
        )

    if missing_api or missing_fmt or unexpected or uncovered_surface:
        raise SystemExit(1)


if __name__ == "__main__":
    main()