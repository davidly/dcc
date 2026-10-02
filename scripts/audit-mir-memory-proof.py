#!/usr/bin/env python3
"""Check a frozen live-memory assertion/guard inventory against healthy evidence."""

import argparse
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def branch_expression(lines, branch):
    start, column, end, end_column = branch[:4]
    if not (1 <= start <= end <= len(lines) and column > 0 and end_column > 0):
        raise ValueError("invalid branch source coordinates")
    parts = lines[start - 1:end]
    if start == end:
        return parts[0][column - 1:end_column - 1]
    return "\n".join([parts[0][column - 1:], *parts[1:-1],
                      parts[-1][:end_column - 1]])


def audit(inventory, coverage, source, host_log, mutations, previous, mutation_logs):
    cases = inventory["dominated_cases"] + inventory["endian_cases"]
    if not cases or len(cases) != len(set(cases)):
        raise ValueError("empty or duplicate case inventory")
    evidence = re.findall(
        r"^; MIR memory-proof case=(\S+) outcome=(passed|failed)\r?$",
        host_log, re.M)
    if (len(evidence) != len(cases) or
            {name for name, _ in evidence} != set(cases) or
            any(outcome != "passed" for _, outcome in evidence) or
            not re.search(r"^MIR verifier failures=0\r?$", host_log, re.M)):
        raise ValueError("missing, duplicate, failed or incomplete host case evidence")
    if (coverage.get("type") != "llvm.coverage.json.export" or
            len(coverage.get("data", [])) != 1):
        raise ValueError("invalid healthy LLVM export")
    required = inventory["mandatory_branch_guards"]
    functions = {}
    for function in coverage["data"][0]["functions"]:
        name = function["name"].rsplit(":", 1)[-1]
        if name not in required:
            continue
        if name in functions or not any(
                Path(path).resolve() == source.resolve()
                for path in function["filenames"]):
            raise ValueError("duplicate or wrong-source function evidence: " + name)
        functions[name] = function
    if not required or functions.keys() != required.keys():
        raise ValueError("missing mandatory live-memory functions")
    lines = source.read_text().splitlines()
    guards = []
    for name, expressions in required.items():
        if not expressions or len(expressions) != len(set(expressions)):
            raise ValueError("empty or duplicate guard inventory: " + name)
        for expression in expressions:
            branches = [
                branch for branch in functions[name]["branches"]
                if expression in branch_expression(lines, branch)]
            if not branches:
                raise ValueError(f"missing guard: {name}: {expression}")
            for branch in branches:
                if any(type(count) is not int or count <= 0 for count in branch[4:6]):
                    raise ValueError(f"unexecuted mandatory outcome: {name}: {expression}")
            guards.append(dict(function=name, expression=expression,
                               outcomes=[branch[:6] for branch in branches]))
    mapping = inventory["mutant_assertions"]
    if not mapping or any(case not in cases for case in mapping.values()):
        raise ValueError("invalid mutant-to-assertion inventory")
    results = {result["mutation"]: result for result in mutations}
    prior = {result["mutation"]: result for result in previous}
    if (len(results) != len(mutations) or len(prior) != len(previous) or
            not prior or "baseline" not in prior or
            set(results) != set(prior) | set(mapping)):
        raise ValueError("changed or incomplete compiler mutant inventory")
    for name, result in results.items():
        expected = "passed" if name == "baseline" else "killed"
        if (result["outcome"] != expected or
                result["exitCode"] != (0 if name == "baseline" else 1)):
            raise ValueError("failed compiler mutant control: " + name)
    if mutation_logs.keys() != mapping.keys():
        raise ValueError("missing intended mutant assertion logs")
    for name, case in mapping.items():
        log = mutation_logs[name]
        if (not re.search(r"^FAIL memory rewrite " + re.escape(case) + r"\r?$", log, re.M) or
                not re.search(r"^; MIR memory-proof case=" + re.escape(case) +
                              r" outcome=failed\r?$", log, re.M) or
                not re.search(r"^MIR verifier failures=[1-9]\d*\r?$", log, re.M)):
            raise ValueError("missing intended mutant assertion: " + name)
    return dict(cases=len(cases), guards=guards, new_mutants=len(mapping),
                existing_mutants=len(prior) - 1, outcome="passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--host-log", type=Path, required=True)
    parser.add_argument("--mutations", type=Path, required=True)
    parser.add_argument("--previous-mutations", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=ROOT / "src/dcc/dcc_mir.c")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        read = lambda path: json.loads(path.read_text(encoding="utf-8-sig"))
        args.output.unlink(missing_ok=True)
        inventory = read(args.inventory)
        logs = {name: (args.mutations.parent / name / "test.log").read_text()
                for name in inventory["mutant_assertions"]}
        result = audit(inventory, read(args.coverage), args.source,
                       args.host_log.read_text(), read(args.mutations),
                       read(args.previous_mutations), logs)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"memory-proof: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Memory proof: {result['cases']} cases, {len(result['guards'])} guards, "
          f"{result['existing_mutants']} existing + {result['new_mutants']} new mutants")


if __name__ == "__main__":
    main()
