#!/usr/bin/env python3
"""Validate static mixed-AST classifications and report scoped LLVM coverage."""

import argparse
import csv
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MIXED = tuple(sorted((ROOT / "src/dcc").glob("dcc_ast_gen*.c")))
CATEGORIES = {"production", "diagnostic", "legacy"}


def walk(node):
    yield node
    for child in node.get("inner", []):
        yield from walk(child)


def definitions(tree, source):
    result = {}
    for node in tree.get("inner", []):
        location = node.get("loc", {})
        if node.get("kind") != "FunctionDecl" or "includedFrom" in location:
            continue
        if "file" in location and Path(location["file"]).resolve() != source.resolve():
            continue
        bodies = [child for child in node.get("inner", [])
                  if child.get("kind") == "CompoundStmt"]
        if not bodies:
            continue
        calls = {child["referencedDecl"]["name"] for child in walk(bodies[0])
                 if child.get("referencedDecl", {}).get("kind") == "FunctionDecl"}
        result[node["name"]] = {
            "line": location.get("line"),
            "calls": sorted(calls),
        }
    return result


def inventory(compiler, sources=MIXED):
    result = {}
    for source in sources:
        process = subprocess.run(
            [compiler, "-std=c11", "-fsyntax-only", "-Xclang", "-ast-dump=json", str(source)],
            check=True, capture_output=True, text=True,
        )
        result[source.relative_to(ROOT).as_posix()] = definitions(json.loads(process.stdout), source)
    return result


def read_manifest(filename):
    result = {}
    manifest = json.loads(filename.read_text(encoding="utf-8"))
    for source, groups in manifest["sources"].items():
        for category, names in groups.items():
            if category not in CATEGORIES or not manifest["reasons"].get(category):
                raise ValueError("invalid category or missing rationale: " + category)
            for name in names:
                key = (source, name)
                if key in result:
                    raise ValueError("duplicate classification: " + str(key))
                result[key] = category
    return result, manifest["guarded_edges"]


def validate(found, classified):
    expected = {(source, name) for source, functions in found.items() for name in functions}
    missing = expected - classified.keys()
    stale = classified.keys() - expected
    if missing or stale:
        raise ValueError(f"unclassified definitions: {sorted(missing)}; stale entries: {sorted(stale)}")


def validate_edges(found, classified, guards):
    by_name = {name: category for (_, name), category in classified.items()}
    expected = {(caller, callee) for caller, callee, reason in guards if reason}
    actual = set()
    for source, functions in found.items():
        for caller, body in functions.items():
            if classified[(source, caller)] == "legacy":
                continue
            for callee in body["calls"]:
                if by_name.get(callee) == "legacy":
                    actual.add((caller, callee))
    if actual != expected:
        raise ValueError(f"changed legacy boundaries: new={sorted(actual - expected)}, "
                         f"stale={sorted(expected - actual)}")


def select_functions(report, classified, active_sources):
    selected = set()
    seen = set()
    for data in report["data"]:
        for function in data["functions"]:
            source = Path(function["filenames"][0]).resolve()
            if not source.is_relative_to(ROOT):
                continue
            relative = source.relative_to(ROOT).as_posix()
            name = function["name"].split(":")[-1]
            key = (relative, name)
            if relative not in active_sources and key not in classified:
                continue
            seen.add(key)
            if classified.get(key) == "legacy":
                if function["count"]:
                    raise ValueError("executed legacy exclusion: " + str(key))
            else:
                selected.add(function["name"])
    missing = {key for key, category in classified.items() if category != "legacy"} - seen
    if missing or not selected:
        raise ValueError(f"coverage missing classified functions: {sorted(missing)}")
    return sorted(selected)


def excluded_functions(report, classified):
    excluded = set()
    for data in report["data"]:
        for function in data["functions"]:
            source = Path(function["filenames"][0]).resolve()
            if not source.is_relative_to(ROOT):
                continue
            relative = source.relative_to(ROOT).as_posix()
            name = function["name"].split(":")[-1]
            if classified.get((relative, name)) == "legacy":
                excluded.add(function["name"])
    return excluded


def summarize_native(text, selected, excluded=None):
    excluded = excluded or set()
    if selected & excluded:
        raise ValueError("native selected and excluded functions overlap")
    functions = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 10 or parts[0] == "TOTAL" or not parts[3].endswith("%"):
            continue
        name = parts[0]
        if name in excluded:
            continue
        if name not in selected or name in functions:
            raise ValueError("unexpected or duplicate native function: " + name)
        functions[name] = {}
        for metric, offset in (("regions", 1), ("lines", 4), ("branches", 7)):
            total, missed = int(parts[offset]), int(parts[offset + 1])
            if not 0 <= missed <= total:
                raise ValueError("invalid native counts: " + name)
            functions[name][metric] = {"count": total, "covered": total - missed}
    if functions.keys() != set(selected):
        raise ValueError("native report did not honor exact function selection")
    totals = {metric: {key: sum(function[metric][key] for function in functions.values())
                       for key in ("count", "covered")}
              for metric in ("regions", "lines", "branches")}
    return {"functions": functions, "totals": totals}


def coverage_gaps(report, selected):
    outcomes = {}
    unexecuted = set()
    for data in report["data"]:
        for function in data["functions"]:
            if function["name"] not in selected:
                continue
            if function["count"] == 0:
                unexecuted.add(function["name"])
            for branch in function["branches"]:
                source = Path(function["filenames"][branch[6]]).resolve()
                if not source.is_relative_to(ROOT):
                    raise ValueError("branch source outside repository: " + str(source))
                relative = source.relative_to(ROOT).as_posix()
                for outcome, count in (("true", branch[4]), ("false", branch[5])):
                    key = (relative, function["name"], *branch[:4], outcome)
                    outcomes[key] = max(outcomes.get(key, 0), count)
    gaps = []
    for key, count in sorted(outcomes.items()):
        if count:
            continue
        source, function, line, column, end_line, end_column, outcome = key
        gaps.append(dict(source=source, function=function, line=line, column=column,
                         end_line=end_line, end_column=end_column, outcome=outcome,
                         review="unreviewed"))
    return {"unexecuted_functions": sorted(unexecuted), "uncovered_branch_outcomes": gaps}


def annotate_reviews(gaps, reviews):
    used = set()
    for review in reviews:
        source = ROOT / review["source"]
        lines = source.read_text(encoding="utf-8").splitlines()
        matches = [(index + 1, text.index(review["expression"]) + 1)
                   for index, text in enumerate(lines) if review["expression"] in text]
        if len(matches) != 1 or not review.get("evidence"):
            raise ValueError("stale or unsupported coverage review: " + review["function"])
        line, column = matches[0]
        key = (review["source"], review["function"], line, column, review["outcome"])
        if key in used:
            raise ValueError("duplicate coverage review")
        used.add(key)
        for gap in gaps["uncovered_branch_outcomes"]:
            if (gap["source"], gap["function"].split(":")[-1], gap["line"],
                    gap["column"], gap["outcome"]) == key:
                gap["review"] = review["classification"]
                gap["evidence"] = review["evidence"]
    return gaps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--manifest", type=Path, default=ROOT / "scripts/ast-function-coverage.json")
    parser.add_argument("--coverage", type=Path)
    parser.add_argument("--allowlist", type=Path)
    parser.add_argument("--native-report", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--gaps", type=Path)
    args = parser.parse_args()
    found = inventory(args.clang)
    if args.inventory:
        args.inventory.write_text(json.dumps(found, indent=2) + "\n", encoding="utf-8")
    else:
        classified, guards = read_manifest(args.manifest)
        validate(found, classified)
        validate_edges(found, classified, guards)
        if args.coverage:
            with (ROOT / "scripts/ast-mir-coverage.tsv").open(encoding="utf-8") as stream:
                active_sources = {row["source"] for row in csv.DictReader(stream, delimiter="\t")
                                  if row["category"] == "active-owner"}
            selected = select_functions(json.loads(args.coverage.read_text()), classified, active_sources)
            if not args.allowlist:
                parser.error("--coverage requires --allowlist")
            args.allowlist.write_text("[llvmcov]\n" + "".join(
                "allowlist_fun:" + name + "\n" for name in selected), encoding="utf-8")
            if args.native_report:
                if not args.summary:
                    parser.error("--native-report requires --summary")
                report = json.loads(args.coverage.read_text())
                excluded = excluded_functions(report, classified)
                summary = summarize_native(
                    args.native_report.read_text(), set(selected), excluded)
                executed = {function["name"] for data in report["data"] for function in data["functions"]
                            if function["name"] in selected and function["count"] > 0}
                summary["totals"]["functions"] = {"count": len(selected), "covered": len(executed)}
                if args.gaps:
                    gaps = coverage_gaps(report, set(selected))
                    reviews = json.loads((ROOT / "scripts/ast-coverage-reviews.json").read_text())
                    annotate_reviews(gaps, reviews)
                    args.gaps.write_text(json.dumps(gaps, indent=2) + "\n", encoding="utf-8")
                    unreviewed = sum(gap["review"] == "unreviewed" for gap in gaps["uncovered_branch_outcomes"])
                    print(f"Unreviewed branch outcomes: {unreviewed}")
                args.summary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
                for metric, counts in summary["totals"].items():
                    total, covered = counts["count"], counts["covered"]
                    print(f"{metric}: {covered}/{total} ({100 * covered / total if total else 100:.2f}%)")
            print(f"Selected {len(selected)} AST/MIR functions")
        print(f"Validated {len(classified)} mixed-AST function classifications")


if __name__ == "__main__":
    main()