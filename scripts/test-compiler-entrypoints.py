#!/usr/bin/env python3
"""Assert driver/include/shadow diagnostics using isolated real subprocesses."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid


ROOT = Path(__file__).resolve().parents[1]
VERSION = "dcc (DCC C89->Z80 compiler) 1.0\n"
USAGE = ("usage: dcc [-c|-module] [-f|-ffloatio|-fno-floatio] "
         "[-fl|-flongio|-fno-longio] [-fhexio|-fno-hexio] [-foctio|-fno-octio] "
         "[-fstack-check] [-fno-narrow] [-v] [-h] [-s|-stack bytes] "
         "[-Idir] [-Dname[=value]] [-Uname] input.c -o output.mac\n")
HELP = VERSION + """usage: dcc [options] input.c -o output.mac

options:
  -o <file>        write M80 assembly to <file> ('-' for stdout)
  -c, -module      emit a linkable helper module (not a final program)
  -f, -ffloatio    force every printf-family call to support %f, even
                   ones whose literal format string doesn't use it
                   (normally auto-detected per call; only needed for a
                   format string that isn't a compile-time literal)
  -fno-floatio     opposite: force every call to NOT support %f, even
                   a literal that uses it, or the conservative fallback
                   for a non-literal format string - use only when you
                   know no call site anywhere needs it, to shrink the
                   fallback's cost
  -fl, -flongio    same, but forces long formats (%ld/%lu/%lx/%lX/%ls)
  -fno-longio      -fno-floatio, but for long formats
  -fhexio          force every call to support %x/%X
  -fno-hexio       -fno-floatio, but for %x/%X
  -foctio          force every call to support %o
  -fno-octio       -fno-floatio, but for %o
  -s, -stack <bytes>   reserve <bytes> for the C stack (default 512)
  -g               emit source-level debug annotations
  -gline           emit optimized debug annotations and variable locations
  -fstack-check    abort gracefully if the stack overflows its reserve
  -fno-narrow      disable every int-array/scalar/for-counter byte-narrowing pass
  -I<dir>          add <dir> to the include search path
  -D<name>[=val]   define a preprocessor macro
  -U<name>         undefine a preprocessor macro
  -v, --version    print version and exit
  -h, --help       print this help and exit
"""
SCHEDULE_FIELDS = {
    "function", "valid", "blocks", "segments", "edges", "phi-edge-uses",
    "call-splits", "fixed", "pressure", "colored", "remat", "spills", "iy",
    "boundary-moves", "split-moves", "unsupported",
}
TARGET_FIELDS = {
    "function", "insns", "minimum-tstates", "minimum-bytes", "calls", "edges",
    "wide", "byte", "pseudo", "materialize", "address", "load", "store",
    "unary", "binary", "unsupported",
}
CACHE_CONTROLS = (
    "DCC_MIR_CACHE_VERIFY",
    "DCC_MIR_LABEL_CACHE_VERIFY",
    "DCC_MIR_RETURN_SUFFIX_CACHE_VERIFY",
    "DCC_MIR_FUSED_BYTE_CACHE_VERIFY",
    "DCC_MIR_WIDEN_CACHE_VERIFY",
    "DCC_MIR_NOT_BRANCH_CACHE_VERIFY",
    "DCC_MIR_LIVENESS_VERIFY",
)
BITSET_SIZES = (1, 2, 63, 64, 65, 127, 128, 129)
PP_CHARACTERS = (
    ("plain", "'A'", 65),
    ("newline", r"'\n'", 10),
    ("return", r"'\r'", 13),
    ("tab", r"'\t'", 9),
    ("alarm", r"'\a'", 7),
    ("backspace", r"'\b'", 8),
    ("formfeed", r"'\f'", 12),
    ("vertical-tab", r"'\v'", 11),
    ("backslash", r"'\\'", 92),
    ("quote", r"'\''", 39),
    ("double-quote", r"'\"'", 34),
    ("question", r"'\?'", 63),
    ("nul", r"'\0'", 0),
    ("octal-one", r"'\7'", 7),
    ("octal-two", r"'\77'", 63),
    ("octal-three", r"'\101'", 65),
    ("octal-byte", r"'\377'", 255),
    ("hex-digit", r"'\x7'", 7),
    ("hex-upper", r"'\x4A'", 74),
    ("hex-lower", r"'\x4a'", 74),
    ("hex-letter", r"'\xa'", 10),
    ("hex-byte", r"'\xff'", 255),
)

def validate_proof_manifest(manifest, executions, with_host):
    expect(manifest["version"] == 1, "unsupported branch proof manifest")
    expect(manifest["character_cases"] == [list(case) for case in PP_CHARACTERS],
           "changed/incomplete character proof inventory")
    expect(manifest["character_routes"] == ["if", "elif", "include"],
           "changed/incomplete character entry routes")
    expect(manifest["bitset_sizes"] == list(BITSET_SIZES),
           "changed/incomplete bitset proof inventory")
    expected = {"pp-character-" + name + "-" + route
                for name, _, _ in PP_CHARACTERS
                for route in manifest["character_routes"]}
    if with_host:
        expected |= {"bitset-values-" + str(size) for size in BITSET_SIZES}
    observed = []
    for execution in executions:
        cases = execution.get("cases", [execution["case"]] if "case" in execution else [])
        expect(isinstance(cases, list) and all(isinstance(case, str) for case in cases),
               "invalid named proof evidence")
        if cases:
            expect(execution["status"] == 0, "failed named proof evidence")
            observed.extend(cases)
    expect(len(observed) == len(set(observed)), "duplicate named proof evidence")
    expect(set(observed) == expected, "missing/unexpected named proof evidence")
    return dict(cases=len(observed), complete=with_host,
                scope="characters-and-bitsets" if with_host else "characters")


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def private_environment():
    # Diagnostics/mutations are caller-local; profiling is intentionally inherited.
    return {key: value for key, value in os.environ.items()
            if not key.startswith("DCC_MIR_") and key not in (
                "DCC_AST_DUMP", "DCC_AST_DUMP_FUNCTION")}


def parse_reports(stderr, schedule=True, target=True):
    result = {}
    for line in stderr.splitlines():
        match = re.fullmatch(r"; MIR (schedule|target)-plan (.*)", line)
        expect(match is not None, "unexpected diagnostic: " + line)
        kind, fields = match.groups()
        expect(kind not in result, "duplicate report: " + kind)
        pairs = [item.split("=", 1) for item in fields.split()]
        expect(all(len(item) == 2 for item in pairs), "malformed report")
        values = dict(pairs)
        expected = SCHEDULE_FIELDS if kind == "schedule" else TARGET_FIELDS
        expect(len(pairs) == len(values) and values.keys() == expected,
               "incomplete/duplicate report fields: " + line)
        for key, value in values.items():
            if key != "function":
                expect(value.isdigit(), "invalid report counter: " + line)
                values[key] = int(value)
        result[kind] = values
    expect(result.keys() == ({"schedule"} if schedule else set()) |
           ({"target"} if target else set()), "missing/unexpected report")
    return result


class Runner:
    def __init__(self, compiler, workspace, timeout):
        self.compiler = str(compiler.resolve())
        self.directory = workspace.resolve() / ("entrypoints-" + uuid.uuid4().hex)
        self.directory.mkdir(parents=True)
        self.timeout = timeout
        self.environment = private_environment()
        self.executions = []
        self.output = self.directory / "probe.MAC"
        self.source = self.directory / "probe.c"

    def run(self, args, status=0, stdout="", stderr="", env=None, compiler=None):
        self.output.unlink(missing_ok=True)
        process = subprocess.run(
            [compiler or self.compiler, *map(str, args)], cwd=self.directory,
            env={**self.environment, **(env or {})}, capture_output=True,
            text=True, timeout=self.timeout)
        self.executions.append(dict(args=list(map(str, args)), status=process.returncode,
                                    stdout=process.stdout, stderr=process.stderr))
        expect(process.returncode == status,
               f"{args}: status {process.returncode}, expected {status}: {process.stderr}")
        expect(process.stdout == stdout, f"{args}: unexpected stdout: {process.stdout!r}")
        if stderr is not None:
            expect(process.stderr == stderr, f"{args}: unexpected stderr: {process.stderr!r}")
        return process

    def compile(self, text, options=(), env=None, stderr=""):
        self.source.write_text(text)
        process = self.run(["-c", *options, self.source, "-o", self.output],
                           env=env, stderr=stderr)
        expect(self.output.is_file(), "successful compile did not emit assembly")
        return self.output.read_bytes(), process.stderr

    def cli(self):
        for args, stdout in ((["-v"], VERSION), (["--version"], VERSION),
                             (["-h"], HELP), (["--help"], HELP)):
            self.run(["-o", self.output, *args], stdout=stdout)
            expect(not self.output.exists(), "early exit emitted assembly")
        invalid = [[], ["-stack"], ["--stack"], ["-s"], ["-D"], ["-U"],
                   ["-I"], ["-o"]]
        invalid += [[option, value] for option in ("-stack", "--stack", "-s")
                    for value in ("-1", "32768", "junk", "12x")]
        invalid += [[option + "=" + value] for option in ("-stack", "--stack", "-s")
                    for value in ("-1", "32768", "junk")]
        invalid += [["-D" + value] for value in ("=1", "1bad=2", "bad-name", "x" * 64)]
        invalid += [["-D", ""], ["-D", "=7"]]
        for args in invalid:
            prefix = [] if args == ["-o"] else ["-o", self.output]
            self.run([*prefix, *args], status=1, stderr=USAGE)
            expect(not self.output.exists() and not (self.directory / "out.mac").exists(),
                   "usage failure emitted assembly")
        text = "int probe(void) { return 137; }\n"
        baseline, _ = self.compile(text)
        ignored, _ = self.compile(text, ["--unknown-switch"])
        expect(ignored == baseline, "unknown-switch compatibility changed")
        for args in (["-DVALUE=137"], ["-D", "VALUE=137"]):
            defined, _ = self.compile("int probe(void) { return VALUE; }\n", args)
            expect(defined == baseline, "command-line macro value not used")

    def includes(self):
        name = "dcc_probe_" + uuid.uuid4().hex + ".h"
        dirs = [self.directory / "include one", self.directory / "include two"]
        for directory, value in zip(dirs, (137, 241)):
            directory.mkdir()
            (directory / name).write_text(f"#define VALUE {value}\n")
        text = f"#include <{name}>\nint probe(void) {{ return VALUE; }}\n"
        for first, second, value in ((dirs[0], dirs[1], 137), (dirs[1], dirs[0], 241)):
            baseline, _ = self.compile(f"int probe(void) {{ return {value}; }}\n")
            for suffix in ("", os.sep):
                for split in (False, True):
                    options = (["-I", str(first) + suffix, "-I", str(second)] if split
                               else ["-I" + str(first) + suffix, "-I" + str(second)])
                    actual, _ = self.compile(text, options)
                    expect(actual == baseline, "include order/separator/value mismatch")

    def diagnostics(self):
        text = ("int other(int x) { return x + 1; }\n"
                "int probe(int x) { if (x) return other(x) + 137; return 241; }\n")
        report_env = {
            "DCC_MIR_SCHEDULE_REPORT": "1", "DCC_MIR_SCHEDULE_REQUIRE": "1",
            "DCC_MIR_SCHEDULE_FUNCTION": "probe",
            "DCC_MIR_TARGET_REPORT": "1", "DCC_MIR_TARGET_FUNCTION": "probe",
        }
        for debug in ([], ["-g"], ["-gline"]):
            for stack in ([], ["-fstack-check"]):
                options = debug + stack
                baseline, _ = self.compile(text, options)
                actual, stderr = self.compile(text, options, report_env, stderr=None)
                expect(actual == baseline, "shadow diagnostics changed assembly/debug metadata")
                reports = parse_reports(stderr)
                for kind, values in reports.items():
                    expect(values["function"] == "probe" and values["unsupported"] == 0,
                           "wrong filter or unsupported valid graph")
                schedule, target = reports["schedule"], reports["target"]
                expect(schedule["valid"] == 1 and schedule["blocks"] >= 2 and
                       schedule["edges"] > 0 and schedule["segments"] > 0 and
                       schedule["call-splits"] > 0 and target["calls"] > 0,
                       "branch/call graph not analyzed")
                if debug:
                    expect(b";@dcc-line " in actual and b";@dcc-func-begin " in actual,
                           "missing debug/metadata records")
                else:
                    expect(b";@dcc-line " not in actual, "release emitted debug lines")
                for kind in ("SCHEDULE", "TARGET"):
                    only = {f"DCC_MIR_{kind}_REPORT": "1",
                            f"DCC_MIR_{kind}_FUNCTION": "probe"}
                    actual, stderr = self.compile(text, options, only, stderr=None)
                    expect(actual == baseline, "single diagnostic changed output")
                    parse_reports(stderr, schedule=kind == "SCHEDULE", target=kind == "TARGET")
                unmatched = {**report_env, "DCC_MIR_SCHEDULE_FUNCTION": "absent",
                             "DCC_MIR_TARGET_FUNCTION": "absent"}
                actual, _ = self.compile(text, options, unmatched)
                expect(actual == baseline, "nonmatching filters changed output")
                actual, _ = self.compile(text, options, {
                    "DCC_MIR_SCHEDULE_FUNCTION": "probe", "DCC_MIR_TARGET_FUNCTION": "probe"})
                expect(actual == baseline, "filters alone enabled diagnostics")
                actual, _ = self.compile(text, options, {
                    "DCC_MIR_SCHEDULE_REQUIRE": "1", "DCC_MIR_SCHEDULE_FUNCTION": "probe"})
                expect(actual == baseline, "require-only changed output")

    def preprocessor_characters(self):
        text = "int probe(void) { return 137; }\n"
        baseline, _ = self.compile(text)
        header = self.directory / "pp-character.h"
        header.write_text(text)
        for name, literal, value in PP_CHARACTERS:
            for route in ("if", "elif", "include"):
                condition = f"{literal} == {value} && {literal} != {value + 1}"
                opening = "#if " if route != "elif" else "#if 0\n#elif "
                body = text if route != "include" else '#include "pp-character.h"\n'
                wrong = ("#error wrong character value\n" if route != "include" else
                         '#include "missing-pp-character.h"\n')
                actual, _ = self.compile(
                    opening + condition + "\n" + body + "#else\n" + wrong + "#endif\n")
                self.executions[-1]["case"] = "pp-character-" + name + "-" + route
                expect(actual == baseline,
                       f"preprocessor character value changed output: {name}/{route}")

    def cache_controls(self):
        text = (
            "int byte_lt(signed char *p) { return *p < 9; }\n"
            "int byte_gt(unsigned char *p) { return *p > 127; }\n"
            "int c_suffix(int f) { return f ? -3 : 9; }\n"
            "int b_suffix(_Bool a, _Bool b) { return !(a || !b); }\n"
            "int joined(unsigned int a, int f) {\n"
            "  unsigned int x;\n"
            "  if (f) x = a + 1U; else x = a - 1U;\n"
            "  while (a) { x ^= a; --a; }\n"
            "  return x;\n"
            "}\n"
        )
        strict = {"DCC_MIR_REQUIRE_COMPLETE": "1", "DCC_MIR_REQUIRE_EMIT": "1"}
        for debug in ([], ["-g"], ["-gline"]):
            for stack in ([], ["-fstack-check"]):
                options = debug + stack
                baseline, _ = self.compile(text, options, strict)
                for controls in (
                        *({name: "1"} for name in CACHE_CONTROLS),
                        {name: "1" for name in CACHE_CONTROLS}):
                    actual, _ = self.compile(text, options, {**strict, **controls})
                    expect(actual == baseline,
                           "cache/liveness verification changed assembly/debug metadata")

    def analysis_limits(self):
        prefix = "unsigned int over(volatile unsigned int *p)\n{\n"
        suffix = "  return *p;\n}\n"
        increment = "  *p += 1U;\n"
        self.compile(prefix + increment * 4 + suffix)
        self.source.write_text(prefix + increment * 4096 + suffix)
        args = ["-c", self.source, "-o", self.output]
        process = self.run(args, status=1, stderr=None)
        match = re.fullmatch(
            r"dcc: fatal: function 'over' is too large to compile: "
            r"([1-9]\d*) MIR instructions x ([1-9]\d*) values exceeds the "
            r"analysis limit \(536870912 cells, 8192 values\); "
            r"split it into smaller functions\n", process.stderr)
        expect(match is not None, "missing precise oversized-function diagnostic")
        instructions, values = map(int, match.groups())
        expect(values * values > 64 * 1024 * 1024 or
               instructions * values > 8 * 64 * 1024 * 1024,
               "oversized diagnostic does not describe an exceeded limit")
        expect(self.output.is_file() and self.output.read_text().rstrip().endswith("_over:"),
               "oversized failure emitted a partial function body or successful footer")
        self.run(args, status=1, stderr=(
            "MIR emission failed for function over: no generated candidate (reason=oversized)\n"
            "dcc: fatal: DCC_MIR_REQUIRE_EMIT requires MIR emission\n"), env={
                "DCC_MIR_REQUIRE_COMPLETE": "1", "DCC_MIR_REQUIRE_EMIT": "1"})
        expect(self.output.is_file() and self.output.read_text().rstrip().endswith("_over:"),
               "strict oversized failure emitted a partial function body or successful footer")

    def bitset_layout(self, host):
        expected = "".join(f"; MIR bitset-proof values={size} outcome=passed\n"
                           for size in BITSET_SIZES)
        self.run(["--bitset-proof"], compiler=str(host.resolve()),
                 stdout="MIR bitset layout checks=8 failures=0\n", stderr=expected)
        self.executions[-1]["cases"] = ["bitset-values-" + str(size) for size in BITSET_SIZES]

    def invalid_shadow(self, host):
        process = self.run(["--shadow-schedule-require-invalid"], compiler=str(host.resolve()),
                           stderr=None, env={
                               "DCC_MIR_SCHEDULE_REPORT": "1",
                               "DCC_MIR_SCHEDULE_FUNCTION": "coverage_unsupported"})
        report = parse_reports(process.stderr, target=False)["schedule"]
        expect(report["function"] == "coverage_unsupported" and
               report["valid"] == 0 and report["unsupported"] > 0,
               "report-only invalid shadow was not diagnosed")
        process = self.run(["--shadow-schedule-require-invalid"], compiler=str(host.resolve()),
                           status=1, stderr=None, env={
                               "DCC_MIR_SCHEDULE_REPORT": "1",
                               "DCC_MIR_SCHEDULE_REQUIRE": "1",
                               "DCC_MIR_SCHEDULE_FUNCTION": "coverage_unsupported"})
        lines = process.stderr.splitlines(keepends=True)
        expect(len(lines) == 2 and
               lines[1] == "dcc: fatal: cannot build MIR shadow schedule\n",
               "isolated shadow require failure missing fatal diagnostic")
        report = parse_reports(lines[0], target=False)["schedule"]
        expect(report["function"] == "coverage_unsupported" and
               report["valid"] == 0 and report["unsupported"] > 0,
               "invalid shadow was not rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, default=ROOT / "build/compiler-function-coverage")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--host", type=Path)
    args = parser.parse_args()
    runner = Runner(args.compiler, args.workspace, args.timeout)
    try:
        runner.cli()
        runner.includes()
        runner.preprocessor_characters()
        runner.diagnostics()
        runner.cache_controls()
        runner.analysis_limits()
        if args.host:
            runner.bitset_layout(args.host)
            runner.invalid_shadow(args.host)
        proof = validate_proof_manifest(
            json.loads((ROOT / "scripts/compiler-branch-proof.json").read_text()),
            runner.executions, with_host=bool(args.host))
        (runner.directory / "proof-evidence.json").write_text(json.dumps(proof, indent=2) + "\n")
        (runner.directory / "executions.json").write_text(
            json.dumps(runner.executions, indent=2) + "\n")
    except (AssertionError, OSError, ValueError, KeyError, TypeError,
            subprocess.SubprocessError) as error:
        print("compiler-entrypoints: " + str(error), file=sys.stderr)
        return 1
    print(f"Compiler entrypoint assertions: {len(runner.executions)} subprocesses; "
          f"artifacts: {runner.directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
