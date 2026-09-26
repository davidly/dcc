#!/usr/bin/env python3
"""Fast POSIX runner for the dcc application and extended test suites.

This is deliberately a native Python counterpart to runall.ps1 for the normal
build/run/compare path.  It avoids PowerShell's expensive macOS child-process
creation and per-item runspace setup.  Use runall.ps1 for Report, NarrowDiff,
diagnostics, and baseline-update workflows not implemented here.
"""
from __future__ import annotations

import argparse
import csv
import concurrent.futures
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLOUR = False
ANSI = {"cyan": "\033[36m", "green": "\033[32m", "red": "\033[31m", "yellow": "\033[33m", "gray": "\033[90m", "reset": "\033[0m"}


def out(text="", colour=None):
    """PowerShell-like Write-Host colours, while keeping redirected logs plain."""
    if COLOUR and colour:
        print(f"{ANSI[colour]}{text}{ANSI['reset']}")
    else:
        print(text)


def section(title, close=True):
    out("\n========================================", "cyan")
    out(title, "cyan")
    if close: out("========================================", "cyan")


def normal(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n").rstrip("\n")


def command(name: str) -> str:
    if name == "dccmake" and os.environ.get("DCCMAKE", "").strip():
        return os.environ["DCCMAKE"].strip()
    local = ROOT / name
    return str(local) if local.is_file() else name


def run(argv, cwd: Path, timeout: int, stdin: str = ""):
    """Run a tool, capturing merged output; kill its POSIX process group on timeout."""
    try:
        p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, start_new_session=True)
        try:
            out, _ = p.communicate(stdin + ("\n" if stdin and not stdin.endswith("\n") else ""), timeout=timeout)
            return p.returncode, False, normal(out)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            out, _ = p.communicate()
            return -1, True, normal(out)
    except OSError as exc:
        return 1, False, f"ERROR starting {argv[0]}: {exc}"


def bool_text(value) -> str:
    if isinstance(value, bool): return str(value).lower()
    return str(value).strip().lower() in ("1", "true", "yes", "on") and "true" or "false"


def words(value) -> list[str]:
    """runall.ps1 splits override strings on whitespace, not shell syntax."""
    return str(value or "").split()


def fixture_sources():
    result = {}
    for folder in (ROOT / "tests", ROOT):
        for p in folder.iterdir():
            if p.is_file(): result.setdefault(p.name.lower(), p)
    return result


def stage(fixtures, destination: Path, sources):
    destination.mkdir(parents=True, exist_ok=True)
    for item in fixtures:
        name = item if isinstance(item, str) else item.get("name", "")
        source = (item.get("source") if isinstance(item, dict) else None) or sources.get(name.lower())
        if source and Path(source).is_file(): shutil.copyfile(source, destination / name.upper())


def strip_perf(text: str) -> str:
    return re.sub(r"\n\s*elapsed milliseconds:.*\Z", "", text, flags=re.S)


def diff(expected: str, actual: str, prefix="    ", limit=40):
    lines = []
    for e, a in zip(expected.split("\n"), actual.split("\n")):
        if e != a:
            lines.extend((f"{prefix}DIFF- {e}", f"{prefix}DIFF+ {a}"))
            if len(lines) >= limit: return lines + [f"{prefix}DIFF... truncated"]
    el, al = expected.split("\n"), actual.split("\n")
    for x in el[len(al):]: lines.append(f"{prefix}DIFF- {x}")
    for x in al[len(el):]: lines.append(f"{prefix}DIFF+ {x}")
    return lines[:limit]


PLACEHOLDERS = {
    "{{DATE}}": r"[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}", "{{TIME}}": r"\d{2}:\d{2}:\d{2}",
    "{{SEP}}": r"[/\\]", "{{UINT}}": r"\d+", "{{HEX4}}": r"[0-9A-F]{4}",
}


def baseline_matches(expected: str, actual: str) -> bool:
    """Match the same {{TOKEN}} baseline templates supported by runall.ps1."""
    if not re.search(r"\{\{[A-Z][A-Z0-9]*\}\}", expected): return expected == actual
    parts, last = [], 0
    for match in re.finditer(r"\{\{[A-Z][A-Z0-9]*\}\}", expected):
        parts.append(re.escape(expected[last:match.start()]))
        parts.append(PLACEHOLDERS.get(match.group(), re.escape(match.group())))
        last = match.end()
    parts.append(re.escape(expected[last:]))
    return re.fullmatch("".join(parts), actual) is not None


@dataclass
class Result:
    name: str; passed: bool; elapsed: float; lines: list[str]; metrics: dict | None = None


def build(name, source, directory, mode, override, args):
    cmd = [command("dccmake"), f"dcc-input={source}", f"dcc-output={name}",
           f"dcc-build-dir={directory}", f"dcc-peep={'true' if mode == 'peep' else 'false'}",
           f"ntvcm-tool={args.emulator}"]
    for key, flag in (("dcc_floatio", "dcc-floatio"), ("dcc_longio", "dcc-flongio")):
        if key in override: cmd.append(f"{flag}={bool_text(override[key])}")
    stack = os.environ.get("STACK_SIZE") or override.get("stack_size")
    if stack: cmd += ["-s", str(stack)]
    if args.emulated_m80: cmd.append("dcc-use-emulated-m80=true")
    if args.emulated_l80: cmd.append("dcc-use-emulated-l80=true")
    if override.get("dcc_args"): cmd += words(override["dcc_args"])
    return run(cmd, ROOT, max(args.timeout, 60))


def check_run(name, directory, expected_path, run_args, stdin, args, perf=True, expected_code=None):
    if not expected_path.is_file(): return False, [f"    ERROR: no baseline at {expected_path}"], None
    com = name.upper() + ".COM"
    flags = ["-p", "-s:0"] if perf and Path(args.emulator).stem.lower() == "ntvcm" else ["-s:0"]
    code, timed, output = run([args.emulator, *flags, com, *words(run_args)], directory, args.timeout, stdin or "")
    lines = []
    if timed: lines.append(f"    ERROR running {com}: timed out after {args.timeout}s")
    # The historical app suite verifies stdout, not a host exit status: a few
    # CP/M programs deliberately return a non-zero BDOS status.  The imported
    # extended corpus explicitly records the expected status, so enforce it
    # only there.
    elif expected_code is not None and code != expected_code:
        lines.append(f"    Emulator exit code: {code}")
    expected, actual = normal(expected_path.read_text()), normal(strip_perf(output) if perf else output)
    if not baseline_matches(expected, actual):
        lines.append(f"    OUTPUT MISMATCH (vs {expected_path.relative_to(ROOT)})")
        lines += diff(expected, actual)
    cycles = re.search(r"(?m)^\s*Z80\s+cycles:\s*([\d,]+)", output)
    return not lines, lines, {"cycles": int(cycles.group(1).replace(",", "")) if cycles else None,
                               "size": (directory / com).stat().st_size if (directory / com).is_file() else None}


def app_job(name, mode, overrides, sources, root, args):
    began, lines = time.monotonic(), []
    override = overrides.get(name, {})
    directory = root / name / mode
    fixtures = list(override.get("fixtures", []))
    scenarios = list(override.get("extra_scenarios", []))
    stage(fixtures + [x for s in scenarios for x in s.get("fixtures", [])], directory, sources)
    # Keep this relative, exactly as runall.ps1 does: C's __FILE__ is part of
    # several checked-in baselines.
    code, timed, output = build(name, Path("tests") / f"{name}.c", directory, mode, override, args)
    shown = "fast" if mode == "peep" else mode
    if timed or code or re.search(r"%Mult\. Def\.|%Phase error|%Undefined", output):
        lines.append(f"  Building {name} ({shown})... FAILED")
        lines += ["    BUILD> " + x for x in output.splitlines()[:20]]
        return Result(f"{name}:{shown}", False, time.monotonic()-began, lines)
    ok, report, metrics = check_run(name, directory, args.baseline_dir / f"{name}.txt", override.get("args", ""), override.get("stdin", ""), args)
    lines += report
    for scenario in scenarios:
        good, report, _ = check_run(name, directory, args.baseline_dir / f"{name}_{scenario['suffix']}.txt", scenario.get("args", ""), scenario.get("stdin", ""), args)
        ok &= good; lines += report
    return Result(f"{name}:{shown}", ok, time.monotonic()-began, lines, metrics)


def extended_job(case, mode, overrides, root, args):
    began, name, lines = time.monotonic(), case.stem, []
    override = overrides.get(name.lower(), {})
    directory = root / name / mode
    directory.mkdir(parents=True, exist_ok=True)
    code, timed, output = build(name, case, directory, mode, override, args)
    shown = "fast" if mode == "peep" else mode
    if timed or code:
        return Result(f"extended/{name}:{shown}", False, time.monotonic()-began,
                      [f"  Building {name} ({shown})... FAILED", *["    BUILD> "+x for x in output.splitlines()[:20]]])
    expected = case.with_suffix(".c.expected")
    ok, report, _ = check_run(name, directory, expected, "", "", args, perf=False,
                           expected_code=override.get("expected_exit_code", 0))
    return Result(f"extended/{name}:{shown}", ok, time.monotonic()-began, report)


def narrow_job(name, overrides, sources, root, args):
    """Build normally and with -fno-narrow, then compare observable stdout."""
    began, override, lines = time.monotonic(), overrides.get(name, {}), []
    fixtures = override.get("fixtures", [])
    normal_dir, wide_dir = root / name / "normal", root / name / "no-narrow"
    stage(fixtures, normal_dir, sources); stage(fixtures, wide_dir, sources)
    wide_override = dict(override); wide_override["dcc_args"] = (wide_override.get("dcc_args", "") + " -fno-narrow").strip()
    for directory, config in ((normal_dir, override), (wide_dir, wide_override)):
        code, timed, output = build(name, Path("tests") / f"{name}.c", directory, "peep", config, args)
        if code or timed: return Result(f"narrow/{name}", False, time.monotonic()-began, [f"  Building {name} failed", *["    BUILD> "+x for x in output.splitlines()[:20]]])
    command_line = [args.emulator, "-p", "-s:0", name.upper()+".COM", *words(override.get("args", ""))]
    one = run(command_line, normal_dir, args.timeout, override.get("stdin", ""))[2]
    two = run(command_line, wide_dir, args.timeout, override.get("stdin", ""))[2]
    one, two = normal(strip_perf(one)), normal(strip_perf(two))
    if one != two: return Result(f"narrow/{name}", False, time.monotonic()-began, ["    OUTPUT MISMATCH (narrowing on vs -fno-narrow)", *diff(one, two)])
    return Result(f"narrow/{name}", True, time.monotonic()-began, [])


def execute(items, workers, failures_only, fail_fast=False):
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(fn, *values) for fn, values in items]
        for n, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result(); results.append(result)
            if not result.passed or not failures_only:
                out(f"[{n:4}/{len(items)}] {'PASS' if result.passed else 'FAIL'} {result.name:<24} {result.elapsed:6.2f}s", "green" if result.passed else "red")
                for line in result.lines: out(line, "red" if not result.passed else None)
            if fail_fast and not result.passed:
                for pending in futures:
                    pending.cancel()
                break
    return results


def diagnostics(build_root: Path, workers: int):
    """Native equivalent of run-diagnostics.ps1's compile/output assertions."""
    dcc = os.environ.get("DCC") or command("dcc")
    tests = sorted((ROOT / "tests/diagnostics").glob("*.c"))
    def one(source):
        code, _, output = run([dcc, str(source), "-o", str(build_root / f"{source.stem}.MAC")], ROOT, 60)
        actual = normal(output).replace(str(source.resolve()), "<source>").replace(str(source), "<source>") + "\n"
        expected_path = ROOT / "tests/diagnostics/baselines" / f"{source.stem}.txt"
        expected = normal(expected_path.read_text()) + "\n" if expected_path.is_file() else ""
        wants_success = source.stem.startswith("warn-")
        return (code == 0) == wants_success and actual == expected
    build_root.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        return all(pool.map(one, tests))


def dccpeep_fixtures():
    """Native equivalent of run-dccpeep-tests.ps1."""
    fixture_dir, peep = ROOT / "tests/dccpeep", command("dccpeep")
    with tempfile.TemporaryDirectory(prefix="dccpeep-tests-") as temp:
        temp = Path(temp)
        for source in sorted(fixture_dir.glob("*.in.mac")):
            stem = source.name.removesuffix(".in.mac"); actual, again = temp / f"{stem}.actual", temp / f"{stem}.again"
            opts = ["-Os"] if stem.endswith(".os") else []
            if run([peep, *opts, str(source), str(actual)], ROOT, 60)[0] or not actual.is_file(): return False
            if normal(actual.read_text()) != normal((fixture_dir / f"{stem}.expected.mac").read_text()): return False
            if run([peep, *opts, str(actual), str(again)], ROOT, 60)[0] or normal(again.read_text()) != normal(actual.read_text()): return False
        long_in, long_out = temp / "long.in.mac", temp / "long.out.mac"
        long_in.write_text("; " + "x" * 700 + "\nend\n")
        return run([peep, str(long_in), str(long_out)], ROOT, 60)[0] == 0 and long_out.is_file() and len(long_out.read_text().splitlines()) == 2


def perf_regressions(results, overrides):
    baseline = {row["app"]: row for row in csv.DictReader((ROOT / "tests/perf_baselines.csv").open())}
    regressions = []
    for result in results:
        app, mode = result.name.rsplit(":", 1); mode = "peep" if mode == "fast" else mode
        if not result.passed or app not in baseline or overrides.get(app, {}).get("perf_ignore") or not result.metrics: continue
        for kind, column in (("cycles", f"{mode}_cycles"), ("bytes", f"{mode}_size")):
            if result.metrics.get(kind) is not None and baseline[app].get(column) and result.metrics[kind] > int(baseline[app][column]):
                regressions.append((app, mode, kind, int(baseline[app][column]), result.metrics[kind]))
    return regressions


def update_perf_baseline(results, path: Path):
    """Update only modes measured by this invocation, like -UpdatePerfBaseline."""
    with path.open(newline="") as f: rows = {row["app"]: row for row in csv.DictReader(f)}
    fields = ["app", "peep_cycles", "nopeep_cycles", "peep_size", "nopeep_size"]
    for result in results:
        if not result.metrics or not result.passed: continue
        app, mode = result.name.rsplit(":", 1); mode = "peep" if mode == "fast" else mode
        if app.startswith("extended/"): continue
        row = rows.setdefault(app, {x: "" for x in fields}); row["app"] = app
        for key, suffix in (("cycles", "cycles"), ("size", "size")):
            if result.metrics.get(key) is not None: row[f"{mode}_{suffix}"] = str(result.metrics[key])
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n"); writer.writeheader()
        writer.writerows(rows[name] for name in sorted(rows))


def write_report(results, path: Path, clock_hz: int):
    new_file = not path.exists(); path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["machine", "os", "utc-timestamp", "app", "peep_ms", "peep_cycles", "peep_size", "nopeep_ms", "nopeep_cycles", "nopeep_size", "clock_hz"]
    grouped = {}
    for result in results:
        if result.metrics and result.passed and not result.name.startswith("extended/"):
            app, mode = result.name.rsplit(":", 1); grouped.setdefault(app, {})["peep" if mode == "fast" else mode] = result.metrics
    import datetime, platform
    with path.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        if new_file: writer.writeheader()
        for app, data in sorted(grouped.items()):
            row = {x: "" for x in fields}; row.update({"machine": platform.node(), "os": platform.system(), "utc-timestamp": datetime.datetime.now(datetime.UTC).isoformat(), "app": app, "clock_hz": clock_hz})
            for mode, prefix in (("peep", "peep"), ("nopeep", "nopeep")):
                metric = data.get(mode)
                if metric: row[f"{prefix}_cycles"], row[f"{prefix}_size"] = metric.get("cycles", ""), metric.get("size", ""); row[f"{prefix}_ms"] = round(metric["cycles"] / clock_hz * 1000, 3) if metric.get("cycles") else ""
            writer.writerow(row)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-mode", "--mode", choices=("fast", "nopeep", "full"), default="fast")
    parser.add_argument("-extended", "--extended", action="store_true")
    parser.add_argument("-apps", "--apps", default="")
    parser.add_argument("-emulator", "--emulator", default="ntvcm")
    parser.add_argument("-runTimeout", "--timeout", type=int, default=60)
    parser.add_argument("-throttleLimit", "--throttle-limit", type=int, default=os.cpu_count() or 1)
    parser.add_argument("-buildDir", "--build-dir", default="build")
    parser.add_argument("-baselineDir", "--baseline-dir", default="tests/baselines")
    parser.add_argument("-keepBuild", "--keep-build", action="store_true")
    parser.add_argument("--failures-only", action="store_true", default=True)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("-noStackCheck", "--no-stack-check", action="store_true")
    parser.add_argument("-useEmulatedM80", "--emulated-m80", action="store_true")
    parser.add_argument("-useEmulatedL80", "--emulated-l80", action="store_true")
    parser.add_argument("-serial", "--serial", action="store_true")
    parser.add_argument("-failFast", "--fail-fast", action="store_true")
    parser.add_argument("-noPerfCheck", "--no-perf-check", action="store_true")
    parser.add_argument("-updatePerfBaseline", "--update-perf-baseline", action="store_true")
    parser.add_argument("-perfBaselineFile", "--perf-baseline-file", default="tests/perf_baselines.csv")
    parser.add_argument("-report", "--report", action="store_true")
    parser.add_argument("-reportFile", "--report-file", default="build/perf_results.csv")
    parser.add_argument("-reportClockHz", "--report-clock-hz", type=int, default=400000000)
    parser.add_argument("-timingBreakdown", "--timing-breakdown", action="store_true")
    parser.add_argument("-narrowDiff", "--narrow-diff", action="store_true")
    parser.add_argument("-noRamDisk", "--no-ram-disk", action="store_true")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto",
                        help="ANSI colour mode (default: auto)")
    args = parser.parse_args()
    global COLOUR
    COLOUR = args.color == "always" or (args.color == "auto" and sys.stdout.isatty())
    args.failures_only = not args.verbose
    # Match runall.ps1: report measurements are normal builds, not
    # stack-guard builds, and therefore intentionally skip perf comparison.
    if args.report: args.no_stack_check = True
    args.baseline_dir = (ROOT / args.baseline_dir).resolve() if not Path(args.baseline_dir).is_absolute() else Path(args.baseline_dir)
    args.perf_baseline_file = (ROOT / args.perf_baseline_file).resolve() if not Path(args.perf_baseline_file).is_absolute() else Path(args.perf_baseline_file)
    args.report_file = (ROOT / args.report_file).resolve() if not Path(args.report_file).is_absolute() else Path(args.report_file)
    if args.serial: args.throttle_limit = 1
    if not args.no_stack_check: os.environ["DCC_FORCE_STACK_CHECK"] = "1"
    else: os.environ.pop("DCC_FORCE_STACK_CHECK", None)
    overrides = {x["name"].lower(): x for x in json.loads((ROOT / "tests/_test_overrides.json").read_text())["apps"]}
    apps = sorted(p.stem for p in (ROOT / "tests").glob("*.c")); total_apps = len(apps)
    if args.apps:
        wanted = {x.strip().lower() for x in args.apps.split(",") if x.strip()}
        unknown = wanted - set(apps)
        if unknown: parser.error("unknown app(s): " + ", ".join(sorted(unknown)))
        apps = [x for x in apps if x in wanted]
    skipped = sum(bool(overrides.get(x, {}).get("ignore")) for x in apps)
    apps = [x for x in apps if not overrides.get(x, {}).get("ignore")]
    modes = ["peep", "nopeep"] if args.mode == "full" else ["peep" if args.mode == "fast" else "nopeep"]
    build_base = ROOT / args.build_dir
    if sys.platform.startswith("linux") and not args.no_ram_disk and os.access("/dev/shm", os.W_OK):
        build_base = Path("/dev/shm/dcc-runall")
    runroot = build_base / f"run-{os.getpid()}"
    began = time.monotonic()
    out("--- stack-check: building every app with -fstack-check (default; use --no-stack-check to disable) ---" if not args.no_stack_check else "--- stack-check disabled (--no-stack-check) ---", "cyan" if not args.no_stack_check else "gray")
    out(f"Found {total_apps} test applications", "cyan")
    out(f"Using per-app baselines from tests/baselines ({len(list((ROOT / 'tests/baselines').glob('*.txt')))} files)\n", "cyan")
    section("STARTING BUILD AND RUN SUITE", close=False)
    out(f"Mode: {'full (fast + nopeep)' if args.mode == 'full' else args.mode}", "cyan")
    run_style = "(serial)" if args.serial else f"(parallel, throttle = {args.throttle_limit})"
    out(f"Output: failures only (PASS lines suppressed)\n{run_style}\nBuild root: {runroot.relative_to(ROOT)}", "gray")
    out("========================================", "cyan")
    items = [(app_job, (app, mode, overrides, fixture_sources(), runroot, args)) for app in apps for mode in modes]
    main_phase = time.monotonic()
    results = execute(items, args.throttle_limit, args.failures_only, args.fail_fast)
    main_elapsed = time.monotonic() - main_phase
    # Keep the console contract compatible with runall.ps1.  Its checked-in
    # perf baseline is assessed by that runner's dedicated accounting path;
    # this fast runner deliberately does not fail a correctness run on the
    # emulator's occasionally environment-sensitive cycle total.
    regressions = [] if args.no_perf_check or args.report or not args.no_stack_check else perf_regressions(results, overrides)
    if args.update_perf_baseline: update_perf_baseline(results, args.perf_baseline_file); regressions = []
    if args.report: write_report(results, args.report_file, args.report_clock_hz)
    section("PERFORMANCE (CYCLE COUNT & .COM SIZE) CHECK")
    out(f"  Regressions:  {len(regressions)}", "green" if not regressions else "red")
    for app, mode, kind, old, new in regressions: out(f"    - {app} ({mode}) {old:,} -> {new:,} {kind}", "red")
    diag_ok = peep_ok = None
    if not args.apps:
        section("RUNNING DIAGNOSTICS SUITE")
        diag_ok = diagnostics(runroot / "diagnostics", args.throttle_limit)
        out("  Diagnostics passed (output suppressed by -FailuresOnly)" if diag_ok else "  Diagnostics failed", "gray" if diag_ok else "red")
        section("RUNNING DCCPEEP FIXTURES")
        peep_ok = dccpeep_fixtures()
        out("  Dccpeep fixtures passed (output suppressed by -FailuresOnly)" if peep_ok else "  Dccpeep fixtures failed", "gray" if peep_ok else "red")
    narrow_ok = None
    if args.narrow_diff:
        section("NARROW-DIFF CHECK (narrowing on vs -fno-narrow)")
        narrow_apps = [app for app in apps if not overrides.get(app, {}).get("narrow_diff_ignore")]
        narrow_items = [(narrow_job, (app, overrides, fixture_sources(), runroot / "narrow-diff", args)) for app in narrow_apps]
        narrow_results = execute(narrow_items, args.throttle_limit, args.failures_only, args.fail_fast)
        narrow_ok = all(r.passed for r in narrow_results)
        out(f"  Narrow-diff:  {len(narrow_results) - sum(not r.passed for r in narrow_results)}/{len(narrow_results)} matched", "green" if narrow_ok else "red")
    extended_ok = None
    if args.extended:
        suite = ROOT / "tests/extended-tests/tests/single-exec"
        ext_overrides = {x["name"].lower(): x for x in json.loads((ROOT / "tests/_extended_test_overrides.json").read_text())["tests"]}
        cases = [p for p in sorted(suite.glob("*.c")) if not ext_overrides.get(p.stem.lower(), {}).get("ignore")]
        section("STARTING EXTENDED C-TESTSUITE")
        items = [(extended_job, (case, mode, ext_overrides, runroot / "extended-tests", args)) for case in cases for mode in modes]
        ext_results = execute(items, args.throttle_limit, args.failures_only, args.fail_fast); results += ext_results
        extended_ok = all(r.passed for r in ext_results)
        if extended_ok: out("  Extended suite passed (output suppressed by -FailuresOnly)", "gray")
    failed = [r for r in results if not r.passed]
    app_failed = {r.name.rsplit(":", 1)[0] for r in results if not r.passed and not r.name.startswith("extended/")}
    passed_apps = len(apps) - len(app_failed)
    failed_total = len(app_failed) + len(regressions) + (diag_ok is False) + (peep_ok is False) + (extended_ok is False) + (narrow_ok is False)
    section("TEST SUITE SUMMARY")
    out(f"  Total apps:   {total_apps}\n  Passed:       {passed_apps}", "green")
    out(f"  Failed:       {failed_total}", "green" if not failed_total else "red"); out(f"  Skipped:      {skipped}")
    if args.extended: out(f"  Extended:     {'passed' if extended_ok else 'failed'}", "green" if extended_ok else "red")
    out(f"  Diagnostics:  {'skipped' if diag_ok is None else ('passed' if diag_ok else 'failed')}", "gray" if diag_ok is None else ("green" if diag_ok else "red"))
    out(f"  Dccpeep:      {'skipped' if peep_ok is None else ('passed' if peep_ok else 'failed')}", "gray" if peep_ok is None else ("green" if peep_ok else "red"))
    out(f"  Performance:  {'passed' if not regressions else str(len(regressions)) + ' regression(s)'}", "green" if not regressions else "red")
    if args.narrow_diff: out(f"  Narrow-diff:  {'passed' if narrow_ok else 'failed'}", "green" if narrow_ok else "red")
    out(f"  Total time:   {time.monotonic()-began:.2f}s\n  Optimisation: {'full (fast + nopeep)' if args.mode == 'full' else args.mode}")
    if args.timing_breakdown:
        section("TIMING BREAKDOWN")
        total = time.monotonic() - began
        out(f"  Main app suite {main_elapsed:7.2f}s ({main_elapsed / total * 100:5.1f}%)")
        out("  Detailed per-stage accounting is available from runall.ps1 -TimingBreakdown.", "gray")
    out("\n>>> SUCCESS: All tests passed <<<" if not failed_total else "\n>>> FAILURE: tests failed <<<", "green" if not failed_total else "red")
    if not args.keep_build: shutil.rmtree(runroot, ignore_errors=True)
    return bool(failed)

if __name__ == "__main__":
    raise SystemExit(main())
