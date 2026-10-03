#!/usr/bin/env python3
"""Reconcile whole-compiler source branch outcomes and enforce exact 70%."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "compiler_functions", ROOT / "scripts/compiler-function-coverage.py")
FUNCTIONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FUNCTIONS)


def source_name(filename, root):
    path = Path(filename).resolve()
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def coordinates(region):
    span = region[:4]
    if (len(span) != 4 or any(type(value) is not int or value <= 0 for value in span)
            or tuple(span[:2]) >= tuple(span[2:])):
        raise ValueError("invalid source coordinates")
    return tuple(span)


def native_branches(text, inventory):
    files, total = {}, None
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 13 or not parts[3].endswith("%"):
            continue
        value = FUNCTIONS.counts(int(parts[10]), int(parts[10]) - int(parts[11]))
        if parts[0] == "TOTAL":
            if total is not None:
                raise ValueError("duplicate native branch TOTAL")
            total = value
            continue
        candidates = [source for source in inventory
                      if Path(source).name == Path(parts[0]).name]
        if len(candidates) != 1 or candidates[0] in files:
            raise ValueError("unexpected/duplicate native branch source: " + parts[0])
        files[candidates[0]] = value
    if total is None or not files:
        raise ValueError("empty/missing native branch totals")
    return files, total


def function_outcomes(function, root):
    filenames = function["filenames"]
    if not isinstance(filenames, list) or not filenames:
        raise ValueError("missing function filenames")
    paths = [source_name(name, root) for name in filenames]
    regions = function["regions"]
    primary = FUNCTIONS.integer(regions[0][5])
    if primary >= len(paths):
        raise ValueError("invalid primary file ID")
    expansions = {}
    for region in regions:
        if len(region) != 8:
            raise ValueError("invalid LLVM region shape")
        if region[7] != 1:
            continue
        parent, child = map(FUNCTIONS.integer, region[5:7])
        if max(parent, child) >= len(paths) or child == primary or child in expansions:
            raise ValueError("invalid/duplicate expansion file ID")
        expansions[child] = (parent, coordinates(region))

    def expansion_path(file_id):
        chain, seen = [], set()
        while file_id != primary:
            if file_id in seen or file_id not in expansions:
                raise ValueError("cyclic/unmapped branch expansion")
            seen.add(file_id)
            parent, span = expansions[file_id]
            chain.append((paths[parent], *span))
            file_id = parent
        return tuple(reversed(chain))

    outcomes = {}
    for branch in function["branches"]:
        if (len(branch) != 9 or any(type(value) is not int for value in branch)
                or branch[8] != 4 or branch[7] != 0):
            raise ValueError("unsupported LLVM branch record")
        span = coordinates(branch)
        true, false, file_id = map(FUNCTIONS.integer, branch[4:7])
        if file_id >= len(paths):
            raise ValueError("invalid branch file ID")
        key = (expansion_path(file_id), paths[file_id], span)
        if key in outcomes:
            raise ValueError("duplicate branch/expansion site")
        outcomes[key] = (true, false)
    return outcomes


def file_mapping(file, records, root):
    direct, expanded = {}, {}
    for body, record in records:
        for key, pair in record["outcomes"].items():
            target = expanded if key[0] else direct
            identity = (body, key) if key[0] else key[2]
            old = target.get(identity, (0, 0))
            target[identity] = tuple(left + right for left, right in zip(old, pair))
    actual_direct = {}
    for branch in file["branches"]:
        if (len(branch) != 9 or any(type(value) is not int for value in branch)
                or branch[8] != 4 or branch[7] != 0):
            raise ValueError("invalid file branch record")
        span = coordinates(branch)
        true, false, _ = map(FUNCTIONS.integer, branch[4:7])
        pair = (true, false)
        old = actual_direct.get(span, (0, 0))
        actual_direct[span] = tuple(left + right for left, right in zip(old, pair))
    folded = []
    for span in actual_direct.keys() - direct.keys():
        # LLVM 18 exports folded branches in file arrays, but excludes them
        # from function arrays and native totals. Only zero pairs can qualify.
        if actual_direct[span] != (0, 0):
            raise ValueError("unexpected executed file branch")
        folded.append(list(span))
    if any(actual_direct.get(span) != pair for span, pair in direct.items()):
        raise ValueError("file/function branch count disagreement")
    actual_expanded = {}
    for expansion in file["expansions"]:
        region = expansion["source_region"]
        if len(region) != 8 or region[7] != 1:
            raise ValueError("invalid file expansion record")
        site = coordinates(region)
        function = dict(regions=expansion["target_regions"],
                        filenames=expansion["filenames"], branches=expansion["branches"])
        body = coordinates(function["regions"][0])
        for key, pair in function_outcomes(function, root).items():
            if not key[0] or key[0][0][1:] != site:
                raise ValueError("branch outside exported expansion site")
            identity = (body, key)
            old = actual_expanded.get(identity, (0, 0))
            actual_expanded[identity] = tuple(left + right for left, right in zip(old, pair))
    if actual_expanded != expanded:
        raise ValueError("file/function macro branch mapping disagreement")
    return sorted(folded)


def summarize(report, inventory, native, root):
    # This independently compiled body inventory also catches partial exports,
    # unexpected source bodies, missing aliases, and host-code scope inflation.
    functions = FUNCTIONS.summarize(report, inventory, native, root)
    names = {(item["source"], tuple(item["range"])): item["names"]
             for item in functions["functions"]}
    native_files, native_total = native_branches(native, inventory)
    exported_files, file_records = {}, {}
    for file in report["data"][0]["files"]:
        source = source_name(file["filename"], root)
        if source not in inventory:
            continue
        if source in exported_files:
            raise ValueError("duplicate source branch summary")
        if not isinstance(file["branches"], list) or not isinstance(file["expansions"], list):
            raise ValueError("missing file branch/expansion data")
        value = file["summary"]["branches"]
        counts = FUNCTIONS.counts(value["count"], value["covered"])
        if FUNCTIONS.integer(value["notcovered"]) != counts["missed"]:
            raise ValueError("inconsistent exported branch counts")
        exported_files[source] = counts
        file_records[source] = file

    groups = {}
    for function in report["data"][0]["functions"]:
        first = function["regions"][0]
        source = source_name(function["filenames"][first[5]], root)
        if source not in inventory:
            continue
        body = coordinates(first)
        records = groups.setdefault((source, body), [])
        if any(record["name"] == function["name"] for record in records):
            raise ValueError("duplicate function branch identity")
        outcomes = function_outcomes(function, root)
        records.append(dict(name=function["name"], outcomes=outcomes,
                            covered=sum(count > 0 for pair in outcomes.values()
                                        for count in pair)))

    folded = {}
    for source, file in file_records.items():
        folded[source] = file_mapping(file, [
            (body, record) for (name, body), records in groups.items()
            if name == source for record in records], root)
    files = {source: FUNCTIONS.counts(0, 0) for source in inventory}
    ledger, aliases = [], []
    for (source, body), records in sorted(groups.items()):
        representative = min(records, key=lambda item: (-item["covered"], item["name"]))
        shape = representative["outcomes"].keys()
        if any(record["outcomes"].keys() != shape for record in records):
            raise ValueError("alias branch mapping disagreement: " + source)
        # LLVM's BranchCoverageInfo::merge takes max(covered), not the union
        # of complementary outcomes from different instantiations.
        selected = representative["outcomes"]
        for record in records:
            if any(count > 0 and selected[key][side] == 0
                   for key, pair in record["outcomes"].items()
                   for side, count in enumerate(pair)):
                raise ValueError("alias outcomes cannot attribute native maximum: " + source)
        files[source]["count"] += 2 * len(selected)
        files[source]["covered"] += representative["covered"]
        files[source]["missed"] = files[source]["count"] - files[source]["covered"]
        if len(records) > 1:
            aliases.append(dict(source=source, body=list(body),
                                representative=representative["name"],
                                identities=[dict(name=item["name"], covered=item["covered"])
                                            for item in records]))
        for (chain, expression_source, span), pair in sorted(selected.items()):
            for side, count in zip(("true", "false"), pair):
                identity = [source, list(body), [list(site) for site in chain],
                            expression_source, list(span), side]
                ledger.append(dict(id=json.dumps(identity, separators=(",", ":")),
                                   source=source, body=list(body),
                                   names=names[(source, body)],
                                   expansions=[list(site) for site in chain],
                                   expression_source=expression_source, range=list(span),
                                   side=side, count=count))
    for source, value in files.items():
        if exported_files.get(source, FUNCTIONS.counts(0, 0)) != value:
            raise ValueError("canonical outcomes disagree with LLVM file summary: " + source)
        if native_files.get(source, FUNCTIONS.counts(0, 0)) != value:
            raise ValueError("canonical outcomes disagree with native branch summary: " + source)
    total = FUNCTIONS.counts(len(ledger), sum(item["count"] > 0 for item in ledger))
    if total["count"] == 0 or total != native_total:
        raise ValueError("empty/inconsistent whole-source branch totals")
    required = (7 * total["count"] + 9) // 10
    return dict(metric="LLVM source branch outcomes (native file summaries)",
                totals=total, required=required, deficit=max(0, required - total["covered"]),
                passes=10 * total["covered"] >= 7 * total["count"],
                files=files, aliases=aliases, folded_file_regions=folded, outcomes=ledger)


def require_target(summary):
    total = summary["totals"]
    value = FUNCTIONS.counts(total["count"], total["covered"])
    if not value["count"] or 10 * value["covered"] < 7 * value["count"]:
        raise ValueError("whole-source branch coverage below exact 70%: "
                         f"{value['covered']}/{value['count']}")


def compare(current, previous):
    before = {item["id"]: item for item in previous["outcomes"]}
    after = {item["id"]: item for item in current["outcomes"]}
    if len(before) != len(previous["outcomes"]) or len(after) != len(current["outcomes"]):
        raise ValueError("duplicate comparison outcome")
    if before.keys() != after.keys():
        raise ValueError("branch mapping changed; cannot attribute execution-only delta")
    for summary in (current, previous):
        counts = FUNCTIONS.counts(
            len(summary["outcomes"]),
            sum(FUNCTIONS.integer(item["count"]) > 0 for item in summary["outcomes"]))
        if counts != summary["totals"]:
            raise ValueError("comparison outcome/summary count disagreement")
    return dict(new=sorted(key for key in before if not before[key]["count"] and after[key]["count"]),
                lost=sorted(key for key in before if before[key]["count"] and not after[key]["count"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--native-report", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=ROOT)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--allow-below-target", action="store_true",
                        help="write reconciled diagnostic evidence without enforcing 70%%")
    args = parser.parse_args()
    try:
        inventory = json.loads(args.inventory.read_text())
        expected = {path.relative_to(args.repo.resolve()).as_posix()
                    for path in (args.repo.resolve() / "src/dcc").glob("*.c")}
        if not expected or set(inventory) != expected:
            raise ValueError("inventory differs from maintained src/dcc/*.c scope")
        result = summarize(json.loads(args.coverage.read_text()),
                           inventory,
                           args.native_report.read_text(), args.repo.resolve())
        if args.baseline:
            result["delta"] = compare(result, json.loads(args.baseline.read_text()))
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / "compiler-branch-coverage.json").write_text(
            json.dumps(result, indent=2) + "\n")
        print(json.dumps({key: result[key] for key in
                          ("totals", "required", "deficit", "passes")}, sort_keys=True))
        if not args.allow_below_target:
            require_target(result)
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        print("compiler-branch-coverage: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
