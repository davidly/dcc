#!/usr/bin/env python3
"""Fail closed on whole-compiler LLVM SOURCE-function coverage (not symbols)."""

import argparse
import bisect
import concurrent.futures
import json
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TARGETS = ("dcc", "mir-verify-test", "mir-scalar-dag-test",
           "mir-consteval-isolation-test", "mir-vla-smooth-isolation-test",
           "mir-selector-isolation-test")


def integer(value):
    if type(value) is not int or value < 0:
        raise ValueError("invalid nonnegative integer count")
    return value


def counts(total, covered):
    total, covered = integer(total), integer(covered)
    if covered > total:
        raise ValueError("covered exceeds count")
    return dict(count=total, covered=covered, missed=total - covered)


def ast_definitions(tree, root, primary=None):
    """Clang elides repeated filenames/lines; use byte offsets for body ranges."""
    found = {}
    current_file = None
    line_starts = {}

    def position(source, loc, end=False):
        loc = loc.get("expansionLoc", loc)
        offset = loc["offset"] + (loc.get("tokLen", 1) if end else 0)
        if source not in line_starts:
            text = source.read_bytes()
            line_starts[source] = [0] + [
                index + 1 for index, byte in enumerate(text) if byte == 10]
        starts = line_starts[source]
        line = bisect.bisect_right(starts, offset)
        return [line, offset - starts[line - 1] + 1]

    for node in tree.get("inner", []):
        loc = node.get("loc", {})
        loc = loc.get("expansionLoc", loc)
        if "file" in loc:
            current_file = Path(loc["file"]).resolve()
        if primary is not None and "includedFrom" not in loc:
            current_file = primary
        if node.get("kind") != "FunctionDecl" or current_file is None:
            continue
        if current_file.parent != root / "src/dcc" or current_file.suffix != ".c":
            continue
        bodies = [child for child in node.get("inner", [])
                  if child.get("kind") == "CompoundStmt"]
        if not bodies:
            continue
        body = bodies[0]["range"]
        span = position(current_file, body["begin"]) + position(
            current_file, body["end"], end=True)
        source = current_file.relative_to(root).as_posix()
        key = (source, *span)
        found.setdefault(key, set()).add(node["name"])
    return found


def compiled_inventory(database, root, jobs=4):
    entries = json.loads(database.read_text())
    expected_sources = {path.resolve() for path in (root / "src/dcc").glob("*.c")}
    if not isinstance(entries, list) or not entries or not expected_sources:
        raise ValueError("empty compilation/source inventory")
    target_sources = {target: set() for target in TARGETS}
    commands = set()
    for entry in entries:
        directory = Path(entry["directory"]).resolve()
        source = (directory / entry["file"]).resolve()
        args = entry.get("arguments") or shlex.split(entry["command"])
        output = entry.get("output", "")
        if not output and "-o" in args:
            output = args[args.index("-o") + 1]
        targets = [target for target in TARGETS
                   if f"CMakeFiles/{target}.dir/" in output]
        if not targets:
            continue
        target = targets[0]
        target_sources[target].add(source)
        clean = []
        skip = False
        for arg in args[1:]:
            if skip:
                skip = False
                continue
            if arg == "-o":
                skip = True
            elif arg != "-c":
                clean.append(arg)
        commands.add((str(directory), args[0], tuple(clean), str(source)))
    for target, sources in target_sources.items():
        required = expected_sources if target == "dcc" else expected_sources - {
            root / "src/dcc/dcc.c"}
        if not sources or not required <= sources:
            raise ValueError(f"incomplete compiled inventory for {target}: "
                             f"{sorted(str(p) for p in required - sources)}")
        if target != "dcc" and not any(p.parent == root / "tests/host" for p in sources):
            raise ValueError("missing host translation unit: " + target)
    actual = {path for sources in target_sources.values() for path in sources
              if path.parent == root / "src/dcc"}
    if actual != expected_sources:
        raise ValueError("compiled source inventory differs from src/dcc/*.c")

    def extract(command):
        directory, compiler, args, source = command
        process = subprocess.run(
            [compiler, *args, "-fsyntax-only", "-Xclang", "-ast-dump=json"],
            cwd=directory, check=True, capture_output=True, text=True, timeout=120)
        return ast_definitions(json.loads(process.stdout), root, Path(source))

    functions = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for found in pool.map(extract, sorted(commands)):
            for key, names in found.items():
                functions.setdefault(key, set()).update(names)
    inventory = {path.relative_to(root).as_posix(): [] for path in sorted(expected_sources)}
    for (source, *span), names in sorted(functions.items()):
        inventory[source].append(dict(range=span, names=sorted(names)))
    if not functions:
        raise ValueError("empty source-function inventory")
    return inventory


def native_totals(text, inventory):
    files = {}
    total = None
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 13 or not parts[3].endswith("%"):
            continue
        value = counts(int(parts[4]), int(parts[4]) - int(parts[5]))
        if parts[0] == "TOTAL":
            if total is not None:
                raise ValueError("duplicate native TOTAL")
            total = value
            continue
        candidates = [source for source in inventory
                      if Path(source).name == Path(parts[0]).name]
        if len(candidates) != 1 or candidates[0] in files:
            raise ValueError("unexpected/duplicate native source: " + parts[0])
        files[candidates[0]] = value
    if total is None or not files:
        raise ValueError("empty/missing native source totals")
    return files, total


def summarize(report, inventory, native, root):
    if not isinstance(inventory, dict) or not inventory or not any(inventory.values()):
        raise ValueError("empty inventory")
    expected = {}
    for source, functions in inventory.items():
        spans = {}
        if not isinstance(functions, list):
            raise ValueError("invalid inventory")
        for function in functions:
            span = tuple(function["range"])
            if (len(span) != 4 or any(type(v) is not int or v <= 0 for v in span)
                    or span in spans or not function["names"]):
                raise ValueError("invalid/duplicate inventory range")
            spans[span] = function["names"]
        expected[source] = spans
    data = report.get("data")
    if report.get("type") != "llvm.coverage.json.export" or not isinstance(data, list) or len(data) != 1:
        raise ValueError("invalid/empty LLVM coverage export")
    files, identities = {}, {}
    for file in data[0]["files"]:
        path = Path(file["filename"]).resolve()
        if path.parent != root / "src/dcc" or path.suffix != ".c":
            continue
        source = path.relative_to(root).as_posix()
        if source not in expected or source in files:
            raise ValueError("unexpected/duplicate exported source: " + source)
        value = file["summary"]["functions"]
        files[source] = counts(value["count"], value["covered"])
    for function in data[0]["functions"]:
        if not function["regions"]:
            raise ValueError("function without source range")
        region = function["regions"][0]
        path = Path(function["filenames"][region[5]]).resolve()
        if path.parent != root / "src/dcc" or path.suffix != ".c":
            continue
        source = path.relative_to(root).as_posix()
        span = tuple(region[:4])
        if source not in expected or span not in expected[source]:
            raise ValueError(f"function outside independent inventory: {source}:{span}")
        key = (source, span)
        identities.setdefault(key, []).append(
            dict(name=function["name"], count=integer(function["count"])))
    native_files, native_total = native_totals(native, inventory)
    gaps, aliases, details = [], [], []
    for source, spans in expected.items():
        absent = spans.keys() - {span for file, span in identities if file == source}
        if absent:
            raise ValueError(f"coverage missing source functions: {source}: {sorted(absent)}")
        hit = sum(any(item["count"] > 0 for item in identities[(source, span)]) for span in spans)
        value = counts(len(spans), hit)
        if spans and source not in files:
            raise ValueError("coverage missing source file: " + source)
        if files.get(source, counts(0, 0)) != value:
            raise ValueError("LLVM file summary disagrees with source inventory: " + source)
        if native_files.get(source, counts(0, 0)) != value:
            raise ValueError("native source totals disagree: " + source)
        for span, names in spans.items():
            records = identities[(source, span)]
            exported_names = {item["name"].split(":")[-1] for item in records}
            if not set(names) <= exported_names:
                raise ValueError(f"coverage missing function identities: "
                                 f"{source}:{span}: {sorted(set(names) - exported_names)}")
            if exported_names != set(names):
                raise ValueError(f"coverage unexpected function identities: "
                                 f"{source}:{span}: {sorted(exported_names - set(names))}")
            detail = dict(source=source, range=list(span), names=names,
                          covered=any(item["count"] for item in records), identities=records)
            details.append(detail)
            if not detail["covered"]:
                gaps.append(detail)
            if len(records) > 1 or any(item["count"] == 0 for item in records):
                aliases.append(detail)
    totals = counts(sum(len(spans) for spans in expected.values()),
                    sum(bool(item["covered"]) for item in details))
    if totals["count"] == 0 or totals != native_total:
        raise ValueError("empty/inconsistent whole-compiler native totals")
    return dict(metric="LLVM source functions (file summaries)", totals=totals,
                files={source: counts(len(spans), sum(
                    bool(item["covered"]) for item in details if item["source"] == source))
                       for source, spans in expected.items()},
                gaps=gaps, aliases=aliases, functions=details)


def require_complete(summary):
    value = summary["totals"]
    if value["count"] <= 0 or value["missed"] != 0 or value["covered"] != value["count"]:
        raise ValueError(f"whole-compiler source-function coverage incomplete: "
                         f"{value['covered']}/{value['count']}, missed={value['missed']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--native-report", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        inventory = compiled_inventory(args.compile_commands, args.repo.resolve())
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / "compiler-function-inventory.json").write_text(
            json.dumps(inventory, indent=2) + "\n")
        summary = summarize(json.loads(args.coverage.read_text()), inventory,
                            args.native_report.read_text(), args.repo.resolve())
        for filename, value in (
                ("compiler-function-coverage.json", summary),
                ("compiler-function-gaps.json", summary["gaps"]),
                ("compiler-function-aliases.json", summary["aliases"])):
            (args.output_dir / filename).write_text(json.dumps(value, indent=2) + "\n")
        print(json.dumps(summary["totals"], sort_keys=True))
        require_complete(summary)
    except (ValueError, KeyError, IndexError, TypeError, OSError, subprocess.SubprocessError) as error:
        print("compiler-function-coverage: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
