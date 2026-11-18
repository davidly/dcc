#!/usr/bin/env python3
"""Cross-platform Python runner for the dcc application and extended test suites.

Runs the application and extended test suites with baseline, fixture,
diagnostic, reporting, narrow-diff, and performance checks.
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
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COLOUR = False
ANSI = {"cyan": "\033[36m", "green": "\033[32m", "red": "\033[31m", "yellow": "\033[33m", "gray": "\033[90m", "reset": "\033[0m"}
PERF_BLOCK = re.compile(r"\n\s*elapsed milliseconds:.*\Z", re.S)
CYCLE_COUNT = re.compile(r"(?m)^\s*Z80\s+cycles:\s*([\d,]+)")
COMMANDS: dict[str, str] = {}


def colour_enabled(mode: str) -> bool:
    """Enable Windows console ANSI processing before auto-detecting colours."""
    if mode == "never":
        return False
    if not sys.stdout.isatty():
        return mode == "always"
    if os.name == "nt":
        try:
            import ctypes
            import msvcrt
            from ctypes import wintypes

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.GetConsoleMode.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
            kernel32.GetConsoleMode.restype = wintypes.BOOL
            kernel32.SetConsoleMode.argtypes = (wintypes.HANDLE, wintypes.DWORD)
            kernel32.SetConsoleMode.restype = wintypes.BOOL
            handle = msvcrt.get_osfhandle(sys.stdout.fileno())
            console_mode = wintypes.DWORD()
            # Preserve existing flags; ENABLE_VIRTUAL_TERMINAL_PROCESSING = 4.
            if not kernel32.GetConsoleMode(handle, ctypes.byref(console_mode)):
                return mode == "always"
            if not kernel32.SetConsoleMode(handle, console_mode.value | 4):
                return mode == "always"
        except (AttributeError, ImportError, OSError, ValueError):
            return mode == "always"
    return True

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


def display_path(path: Path) -> Path:
    """Show repository-local paths relatively, retaining external absolute paths."""
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def command(name: str) -> str:
    if name in COMMANDS:
        return COMMANDS[name]
    if name == "dccmake" and os.environ.get("DCCMAKE", "").strip():
        COMMANDS[name] = os.environ["DCCMAKE"].strip()
        return COMMANDS[name]
    local = ROOT / name
    COMMANDS[name] = str(local) if local.is_file() else name
    return COMMANDS[name]


def run(argv, cwd: Path, timeout: int, stdin: str = "", env=None):
    """Run a tool, capturing merged output; terminate its process tree on timeout."""
    if os.environ.get("DCC_STRESS_SUPERVISE") == "1":
        try:
            try: from .process_supervision import Process
            except ImportError: from process_supervision import Process
            child = Process(argv, cwd, environment=env,
                            parent_scope=os.environ.get("DCC_PROCESS_SCOPE"), drain_timeout=1, stdin=stdin + ("\n" if stdin and not stdin.endswith("\n") else ""))
            try:
                while not child.ready(timeout if timeout > 0 else float("inf")):
                    time.sleep(.01)
                result = child.complete(0)
                return result["ExitCode"], result["TimedOut"], result["Output"]
            finally:
                child.stop()
        except OSError as exc:
            return 1, False, f"ERROR starting {argv[0]}: {exc}"
    try:
        popen_args = dict(cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, env=env)
        if os.name == "nt":
            # taskkill /T below terminates the complete descendant tree.  A
            # separate process group keeps the runner's control signals out
            # of that tree while it is being cleaned up.
            popen_args["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            # Each command owns a session, so a timeout cannot kill the
            # runner or unrelated children in the caller's process group.
            popen_args["start_new_session"] = True
        p = subprocess.Popen(argv, **popen_args)
        try:
            input_text = stdin + ("\n" if stdin and not stdin.endswith("\n") else "")
            out, _ = p.communicate(input_text, timeout=timeout if timeout > 0 else None)
            return p.returncode, False, out
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                # Windows has no killpg equivalent.  taskkill's /T follows
                # the parent/child tree, which covers dccmake's compiler,
                # assembler, linker, and emulator descendants.
                try:
                    subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, timeout=10, check=False)
                except (OSError, subprocess.TimeoutExpired):
                    pass
            else:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if p.poll() is None:
                try:
                    p.kill()
                except ProcessLookupError:
                    pass
            out, _ = p.communicate()
            return -1, True, out
    except OSError as exc:
        return 1, False, f"ERROR starting {argv[0]}: {exc}"


def ensure_extended_suite(repo_root: Path, suite_dir: Path,
                          submodule_path=Path("tests/extended-tests")) -> bool:
    """Initialize the default extended-test submodule, with clone fallback."""
    if suite_dir.is_dir():
        return True

    submodule_arg = submodule_path.as_posix()
    if (repo_root / ".git").exists():
        out(f"Extended test submodule not initialized; running git submodule update --init -- {submodule_arg}", "cyan")
        code, _, output = run(
            ["git", "submodule", "update", "--init", "--", submodule_arg],
            repo_root, 0)
        if code == 0 and suite_dir.is_dir():
            return True
        out("git submodule update failed; falling back to a direct clone of "
            f"the submodule's own URL.\n{output}", "yellow")
    else:
        out(f"{repo_root} is not a git repository (no .git found); cloning "
            "the extended-tests submodule directly.", "cyan")

    gitmodules = repo_root / ".gitmodules"
    if not gitmodules.is_file():
        out(f"Failed to initialize extended test submodule at {submodule_arg}: "
            f"no .gitmodules found at {gitmodules}.", "red")
        return False

    code, _, output = run(
        ["git", "config", "--file", str(gitmodules), "--get",
         f"submodule.{submodule_arg}.url"], repo_root, 0)
    submodule_url = output.strip()
    if code != 0 or not submodule_url:
        out(f"Failed to initialize extended test submodule at {submodule_arg}: "
            f"could not read its URL from {gitmodules}.\n{output}", "red")
        return False

    destination = repo_root / submodule_path
    out(f"Cloning {submodule_url} into {destination} ...", "cyan")
    code, _, output = run(
        ["git", "clone", "--depth", "1", submodule_url, str(destination)],
        repo_root, 0)
    if code != 0 or not suite_dir.is_dir():
        out(f"Failed to initialize extended test submodule at {submodule_arg}.\n{output}", "red")
        return False
    return True


def bool_text(value) -> str:
    if isinstance(value, bool): return str(value).lower()
    return str(value).strip().lower() in ("1", "true", "yes", "on") and "true" or "false"


def words(value) -> list[str]:
    """Override arguments are split on whitespace, not shell syntax."""
    return str(value or "").split()


def fixture_sources(fixtures):
    """Index only fixture names needed by this invocation."""
    wanted = set()
    for item in fixtures:
        name = item if isinstance(item, str) else item.get("name", "")
        explicit = item.get("source") if isinstance(item, dict) else None
        if name and not explicit:
            wanted.add(name.lower())
    if not wanted:
        return {}
    result = {}
    for folder in (ROOT / "tests", ROOT):
        for p in folder.iterdir():
            key = p.name.lower()
            if key in wanted and p.is_file():
                result.setdefault(key, p)
                wanted.discard(key)
        if not wanted:
            break
    return result


def stage(fixtures, destination: Path, sources):
    """Copy fixtures under CP/M names, applying read_only only to staged copies."""
    destination.mkdir(parents=True, exist_ok=True)
    missing = []
    for item in fixtures:
        name = item if isinstance(item, str) else item.get("name", "")
        explicit = item.get("source") if isinstance(item, dict) else None
        source = Path(explicit) if explicit else sources.get(name.lower())
        if source and not source.is_absolute():
            source = ROOT / source
        if name and source and source.is_file():
            target = destination / name.upper()
            # A repeat staging operation may replace a read-only fixture.
            if target.exists():
                target.chmod(target.stat().st_mode | stat.S_IWUSR)
            shutil.copyfile(source, target)
            if isinstance(item, dict) and item.get("read_only"):
                target.chmod(target.stat().st_mode &
                             ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))
        elif name:
            missing.append(name)
    return missing


def strip_perf(text: str) -> str:
    return PERF_BLOCK.sub("", text)


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
    """Match {{TOKEN}} baseline templates in expected output."""
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


@dataclass(frozen=True)
class Baseline:
    path: Path
    expected: str | None


@dataclass(frozen=True)
class WorkItem:
    fn: object
    values: tuple
    name: str
    slots: int = 1


@dataclass
class Execution:
    results: list[Result]
    not_started: list[str]


def load_baseline(path: Path) -> Baseline:
    return Baseline(path, normal(path.read_text()) if path.is_file() else None)


def is_ntvcm(emulator: str) -> bool:
    return Path(emulator).stem.lower() == "ntvcm"


def emulator_flags(emulator: str, perf: bool) -> list[str]:
    if not is_ntvcm(emulator):
        return []
    return ["-p", "-s:0"] if perf else ["-s:0"]


def build(name, source, directory, mode, override, args):
    cmd = [command("dccmake"), f"dcc-input={source}", f"dcc-output={name}",
           f"dcc-build-dir={directory}", f"dcc-peep={'true' if mode == 'peep' else 'false'}",
           f"ntvcm-tool={args.emulator}"]
    # Keep the established runner configuration: both extended printf
    # families are on unless a test explicitly opts out.  Leaving these
    # options absent uses dccmake's false defaults and changes linked I/O
    # routines, invalidating the shared performance baselines.
    for key, flag in (("dcc_floatio", "dcc-floatio"), ("dcc_longio", "dcc-flongio")):
        cmd.append(f"{flag}={bool_text(override.get(key, True))}")
    stack = os.environ.get("STACK_SIZE") or override.get("stack_size")
    if stack: cmd += ["-s", str(stack)]
    if args.emulated_m80: cmd.append("dcc-use-emulated-m80=true")
    if args.emulated_l80: cmd.append("dcc-use-emulated-l80=true")
    if override.get("dcc_args"): cmd += words(override["dcc_args"])
    return run(cmd, ROOT, 0 if args.timeout == 0 else getattr(args, "build_timeout", max(args.timeout, 60)))


def check_run(name, directory, baseline, run_args, stdin, args, perf=True,
              expected_code=None, collect_metrics=True):
    if baseline.expected is None:
        return False, [f"    ERROR: no baseline at {baseline.path}"], None
    com = name.upper() + ".COM"
    flags = emulator_flags(args.emulator, perf)
    code, timed, output = run([args.emulator, *flags, com, *words(run_args)], directory, args.timeout, stdin or "")
    lines = []
    if timed: lines.append(f"    ERROR running {com}: timed out after {args.timeout}s")
    # Historical app baselines judge stdout: several valid CP/M programs
    # deliberately leave a non-zero BDOS status.  Extended tests explicitly
    # declare their expected host status, so enforce it only for that corpus.
    elif expected_code is not None and code != expected_code:
        lines.append(f"    Emulator exit code: {code}")
    actual = normal(strip_perf(output) if perf else output)
    if not baseline_matches(baseline.expected, actual):
        lines.append(f"    OUTPUT MISMATCH (vs {display_path(baseline.path)})")
        lines += diff(baseline.expected, actual)
    if not collect_metrics:
        return not lines, lines, None
    cycles = CYCLE_COUNT.search(output)
    return not lines, lines, {"cycles": int(cycles.group(1).replace(",", "")) if cycles else None,
                               "size": (directory / com).stat().st_size if (directory / com).is_file() else None}


def app_timeout_args(args, override):
    """Give expensive tests a timeout floor while retaining an unlimited run."""
    timeout = max(args.timeout, override.get("run_timeout", 0)) if args.timeout else 0
    return argparse.Namespace(**{**vars(args), "timeout": timeout})


def batch_workers(limit, modes, count):
    """Reserve child slots without exceeding the suite-wide process limit."""
    return max(1, min(count, limit // modes))


def check_batches(name, directory, baseline, count, workers, args):
    # Only read-only COMs may use this contract: every numeric batch k/N
    # writes the same expected summary and runs in an independent emulator.
    def one(index):
        ok, lines, metrics = check_run(name, directory, baseline, f"{index} {count}", "", args,
                                       expected_code=0)
        return ok, [f"    [batch {index+1}/{count}] {line.strip()}" for line in lines], metrics
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(one, range(count)))
    cycles = [m["cycles"] for _, _, m in results if m]
    metrics = {"cycles": sum(cycles) if len(cycles) == count and None not in cycles else None,
               "size": (directory / (name.upper()+".COM")).stat().st_size}
    if is_ntvcm(args.emulator) and metrics["cycles"] is None:
        return False, ["    ERROR: missing cycle count in a batch"], metrics
    return all(ok for ok, _, _ in results), [line for _, lines, _ in results for line in lines], metrics


def build_has_diagnostics(output):
    """Reject assembler errors and MIR verifier diagnostics even after exit zero."""
    return re.search(r"%Mult\. Def\.|%Phase error|%Undefined|^\s*; MIR \S+:", output, re.MULTILINE) is not None


def app_job(name, mode, overrides, sources, baselines, root, args):
    began, lines = time.monotonic(), []
    override = overrides.get(name, {})
    args = app_timeout_args(args, override)
    directory = root / name / mode
    fixtures = list(override.get("fixtures", []))
    scenarios = list(override.get("extra_scenarios", []))
    required = [baselines[args.baseline_dir / f"{name}.txt"]]
    required += [baselines[args.baseline_dir / f"{name}_{s['suffix']}.txt"] for s in scenarios]
    batches = override.get("run_batches", 0)
    batch_baseline = baselines.get(args.baseline_dir / f"{name}_batch.txt") if batches else None
    missing_baselines = [x.path for x in required + ([batch_baseline] if batches else []) if x.expected is None]
    if missing_baselines:
        return Result(f"{name}:{'fast' if mode == 'peep' else mode}", False,
                      time.monotonic()-began,
                      [f"    ERROR: no baseline at {path}" for path in missing_baselines])
    missing_fixtures = stage(fixtures + [x for s in scenarios for x in s.get("fixtures", [])], directory, sources)
    if missing_fixtures:
        return Result(f"{name}:{'fast' if mode == 'peep' else mode}", False,
                      time.monotonic()-began,
                      [f"    ERROR: fixture not found: {name}" for name in missing_fixtures])
    # Keep this relative, consistently: C's __FILE__ is part of
    # several checked-in baselines.
    code, timed, output = build(name, Path("tests") / f"{name}.c", directory, mode, override, args)
    shown = "fast" if mode == "peep" else mode
    if timed or code or build_has_diagnostics(output):
        lines.append(f"  Building {name} ({shown})... FAILED")
        lines += ["    BUILD> " + x for x in output.splitlines()[:20]]
        return Result(f"{name}:{shown}", False, time.monotonic()-began, lines)
    if batches:
        ok, report, metrics = check_batches(name, directory, batch_baseline, batches,
                                           args.batch_workers, args)
    else:
        ok, report, metrics = check_run(name, directory, required[0], override.get("args", ""), override.get("stdin", ""), args)
    lines += report
    for scenario, baseline in zip(scenarios, required[1:]):
        good, report, _ = check_run(name, directory, baseline, scenario.get("args", ""), scenario.get("stdin", ""), args,
                                    collect_metrics=False)
        ok &= good; lines += report
    return Result(f"{name}:{shown}", ok, time.monotonic()-began, lines, metrics)


def extended_job(case, mode, overrides, baseline, root, args):
    began, name, lines = time.monotonic(), case.stem, []
    override = overrides.get(name.lower(), {})
    shown = "fast" if mode == "peep" else mode
    if baseline.expected is None:
        return Result(f"extended/{name}:{shown}", False, time.monotonic()-began,
                      [f"    ERROR: no expected output at {baseline.path}"])
    directory = root / name / mode
    directory.mkdir(parents=True, exist_ok=True)
    code, timed, output = build(name, case, directory, mode, override, args)
    if timed or code or build_has_diagnostics(output):
        return Result(f"extended/{name}:{shown}", False, time.monotonic()-began,
                      [f"  Building {name} ({shown})... FAILED", *["    BUILD> "+x for x in output.splitlines()[:20]]])
    ok, report, _ = check_run(name, directory, baseline, "", "", args, perf=False,
                              expected_code=override.get("expected_exit_code", 0), collect_metrics=False)
    return Result(f"extended/{name}:{shown}", ok, time.monotonic()-began, report)


def narrow_job(name, overrides, sources, root, args):
    """Build normally and with -fno-narrow, then compare observable stdout."""
    began, override, lines = time.monotonic(), overrides.get(name, {}), []
    args = app_timeout_args(args, override)
    fixtures = override.get("fixtures", [])
    normal_dir, wide_dir = root / name / "normal", root / name / "no-narrow"
    missing = stage(fixtures, normal_dir, sources) + stage(fixtures, wide_dir, sources)
    if missing:
        return Result(f"narrow/{name}", False, time.monotonic()-began,
                      [f"    ERROR: fixture not found: {fixture}" for fixture in sorted(set(missing))])
    wide_override = dict(override); wide_override["dcc_args"] = (wide_override.get("dcc_args", "") + " -fno-narrow").strip()
    for directory, config in ((normal_dir, override), (wide_dir, wide_override)):
        code, timed, output = build(name, Path("tests") / f"{name}.c", directory, "peep", config, args)
        if code or timed or re.search(r"%Mult\. Def\.|%Phase error|%Undefined", output):
            return Result(f"narrow/{name}", False, time.monotonic()-began,
                          [f"  Building {name} failed", *["    BUILD> "+x for x in output.splitlines()[:20]]])
        if not (directory / (name.upper() + ".COM")).is_file():
            return Result(f"narrow/{name}", False, time.monotonic()-began,
                          [f"    ERROR: {name.upper()}.COM not produced"])
    command_line = [args.emulator, *emulator_flags(args.emulator, True), name.upper()+".COM", *words(override.get("args", ""))]
    first = run(command_line, normal_dir, args.timeout, override.get("stdin", ""))
    second = run(command_line, wide_dir, args.timeout, override.get("stdin", ""))
    if first[1] or second[1]:
        return Result(f"narrow/{name}", False, time.monotonic()-began,
                      [f"    ERROR: narrow-diff emulator timed out after {args.timeout}s"])
    if first[2].startswith("ERROR starting ") or second[2].startswith("ERROR starting "):
        return Result(f"narrow/{name}", False, time.monotonic()-began,
                      ["    ERROR: narrow-diff emulator could not be started"])
    if first[0] != second[0]:
        return Result(f"narrow/{name}", False, time.monotonic()-began,
                      [f"    ERROR: emulator exit codes differ ({first[0]} vs {second[0]})"])
    one, two = normal(strip_perf(first[2])), normal(strip_perf(second[2]))
    for pattern, token in ((r"[A-Z][a-z]{2}\s+\d{1,2}\s+\d{4}", "{{DATE}}"),
                           (r"\d{2}:\d{2}:\d{2}", "{{TIME}}")):
        one, two = re.sub(pattern, token, one), re.sub(pattern, token, two)
    if one != two: return Result(f"narrow/{name}", False, time.monotonic()-began, ["    OUTPUT MISMATCH (narrowing on vs -fno-narrow)", *diff(one, two)])
    return Result(f"narrow/{name}", True, time.monotonic()-began, [])


def execute(items, workers, failures_only, fail_fast=False, fail_fast_check=None):
    """Run a bounded work queue so fail-fast never submits the whole suite."""
    results, next_item, stopped, occupied = [], 0, False, 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        pending = {}

        def submit_one():
            nonlocal next_item, occupied
            item = items[next_item]; next_item += 1
            pending[pool.submit(item.fn, *item.values)] = item
            occupied += item.slots

        while next_item < len(items) and occupied + items[next_item].slots <= workers:
            submit_one()
        while pending:
            done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                occupied -= pending.pop(future).slots
                result = future.result(); results.append(result)
                n = len(results)
                if not result.passed or not failures_only:
                    out(f"[{n:4}/{len(items)}] {'PASS' if result.passed else 'FAIL'} {result.name:<24} {result.elapsed:6.2f}s", "green" if result.passed else "red")
                    for line in result.lines: out(line, "red" if not result.passed else None)
                reason = fail_fast_check(result) if fail_fast and result.passed and fail_fast_check else None
                if fail_fast and (not result.passed or reason):
                    if not stopped:
                        why = reason or f"correctness failure in {result.name}"
                        out(f"  FAIL-FAST: {why}; no new work will be started", "yellow")
                    stopped = True
            while not stopped and next_item < len(items) and occupied + items[next_item].slots <= workers:
                submit_one()
    return Execution(results, [item.name for item in items[next_item:]])


def diagnostics(build_root: Path, workers: int):
    """Use the same diagnostic assertions as the standalone Python runner."""
    try:
        try:
            from .diagnostics import run_suite
        except ImportError:
            from diagnostics import run_suite
        results = run_suite(os.environ.get("DCC") or command("dcc"),
                            ROOT / "tests/diagnostics", ROOT / "tests/diagnostics/baselines",
                            build_root, workers, run, root=ROOT)
        return all(result.passed for result in results)
    except (OSError, RuntimeError):
        return False


def dccpeep_fixtures(workers: int, fixture_dir=None, peep=None):
    """Shared fixture, idempotence and negative controls for the Python runners."""
    fixture_dir = fixture_dir if fixture_dir is not None else ROOT / "tests/dccpeep"
    peep = peep or command("dccpeep")
    sources = sorted(fixture_dir.glob("*.in.mac"))
    if not sources:
        return False
    with tempfile.TemporaryDirectory(prefix="dccpeep-tests-") as temp:
        temp = Path(temp)
        def one(source):
            stem = source.name[:-len(".in.mac")]; actual, again = temp / f"{stem}.actual", temp / f"{stem}.again"
            expected = fixture_dir / f"{stem}.expected.mac"
            if not expected.is_file(): return False
            opts = ["-Os"] if stem.endswith(".os") else []
            if stem.endswith(".closed"): opts += ["-closed-program"]
            if run([peep, *opts, str(source), str(actual)], ROOT, 60)[0] or not actual.is_file(): return False
            actual_text = actual.read_text().replace("\r\n", "\n")
            if actual_text != expected.read_text().replace("\r\n", "\n"): return False
            if run([peep, *opts, str(actual), str(again)], ROOT, 60)[0] or not again.is_file(): return False
            return again.read_text().replace("\r\n", "\n") == actual_text
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            fixtures_ok = all(list(pool.map(one, sources)))
        if not fixtures_ok:
            return False
        swap = fixture_dir / "aggregate-swap-spilled-ldir.in.mac"
        if swap.is_file():
            swap_text = swap.read_text()
            for label, old, new in (
                ("reload", "ld l,(ix-2)\nld h,(ix-1)", "ld l,(ix-6)\nld h,(ix-5)"),
                ("spill", "ld h,b\nld (ix-2),l\nld (ix-1),h\nld hl,_temp\nex de,hl\nld l,(ix-2)\nld h,(ix-1)",
                 "ld h,b\nld (ix-6),l\nld (ix-5),h\nld hl,_temp\nex de,hl\nld l,(ix-6)\nld h,(ix-5)"),
                ("temporary", "ex de,hl\nld hl,_temp", "ex de,hl\nld hl,_other"),
                ("size", "ld hl,_temp\nld bc,13", "ld hl,_temp\nld bc,12"),
                ("pointer", "ldir\nld hl,(_left)", "ldir\nld hl,(_other)"),
            ):
                changed = swap_text.replace(old, new)
                if changed == swap_text:
                    return False  # Fixture drift must not silently drop a control.
                source, output = temp / ("swap-"+label+".mac"), temp / ("swap-"+label+".out.mac")
                source.write_text(changed)
                if run([peep, str(source), str(output)], ROOT, 60)[0] or not output.is_file():
                    return False
                if "peep: aggregate_swap_ldir" in output.read_text():
                    return False
        long_in, long_out = temp / "long.in.mac", temp / "long.out.mac"
        long_in.write_text("; " + "x" * 700 + "\nend\n")
        if run([peep, str(long_in), str(long_out)], ROOT, 60)[0] or not long_out.is_file():
            return False
        lines = long_out.read_text().splitlines()
        return len(lines) == 2 and len(lines[0]) == 702


def prepare_extended_suite(args, runroot: Path, modes: list[str], suite=None, overrides_path=None,
                           standards=None, names=(), allow_empty=False):
    """Create extended-test work items after the suite has been bootstrapped."""
    suite = Path(suite) if suite is not None else ROOT / "tests/extended-tests/tests/single-exec"
    overrides_path = Path(overrides_path) if overrides_path is not None else ROOT / "tests/_extended_test_overrides.json"
    ext_overrides = {x["name"].lower(): x for x in json.loads(overrides_path.read_text())["tests"]} if overrides_path.is_file() else {}
    requested = {Path(n).stem.lower() for item in names for n in item.split(",")}
    sources = sorted(suite.glob("*.c"))
    missing = requested - {p.stem.lower() for p in sources}
    if missing:
        return None, "Unknown extended tests: " + ", ".join(sorted(missing))
    cases = []
    skipped = []
    matching = 0
    for source in sources:
        if requested and source.stem.lower() not in requested:
            continue
        if standards is not None:
            tags_path = source.with_suffix(".c.tags")
            tags = {line.strip().lower() for line in tags_path.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith("#")} if tags_path.is_file() else set()
            accepted = {"c89"}
            if "c11" in standards: accepted.update(("c99", "c11"))
            elif "c99" in standards: accepted.add("c99")
            if not tags & accepted: continue
        matching += 1
        override = ext_overrides.get(source.stem.lower(), {})
        if override.get("ignore"):
            skipped.append((source.stem, override.get("reason", "ignored")))
        else:
            cases.append(source)
    args.extended_skipped = skipped
    if not cases and matching and allow_empty:
        return [], None
    if not cases:
        return None, f"Extended suite unavailable or contains no runnable tests: {suite}"
    baselines = {case: load_baseline(case.with_suffix(".c.expected")) for case in cases}
    items = [WorkItem(extended_job,
                      (case, mode, ext_overrides, baselines[case], runroot / "extended-tests", args),
                      f"extended/{case.stem}:{'fast' if mode == 'peep' else mode}")
             for case in cases for mode in modes]
    return items, None


def load_perf_baselines(path: Path):
    if not path.is_file():
        return {}
    with path.open(newline="") as f:
        return {row["app"]: row for row in csv.DictReader(f)}


def perf_regressions(results, overrides, baseline, excluded_apps=None):
    regressions = []
    failed_apps = {r.name.rsplit(":", 1)[0] for r in results if not r.passed}
    failed_apps.update(excluded_apps or ())
    for result in results:
        app, mode = result.name.rsplit(":", 1); mode = "peep" if mode == "fast" else mode
        if (not result.passed or app in failed_apps or app not in baseline or
                overrides.get(app, {}).get("perf_ignore") or not result.metrics):
            continue
        for key, label, column in (("cycles", "cycles", f"{mode}_cycles"),
                                   ("size", "bytes", f"{mode}_size")):
            value = result.metrics.get(key)
            if value is not None and baseline[app].get(column) and value > int(baseline[app][column]):
                regressions.append((app, mode, label, int(baseline[app][column]), value))
    return regressions


def update_perf_baseline(results, path: Path, excluded_apps=None):
    """Update only modes measured by this invocation, like -UpdatePerfBaseline."""
    rows = load_perf_baselines(path)
    fields = ["app", "peep_cycles", "nopeep_cycles", "peep_size", "nopeep_size"]
    failed_apps = {r.name.rsplit(":", 1)[0] for r in results if not r.passed}
    failed_apps.update(excluded_apps or ())
    for result in results:
        if not result.metrics or not result.passed: continue
        app, mode = result.name.rsplit(":", 1); mode = "peep" if mode == "fast" else mode
        if app.startswith("extended/") or app in failed_apps: continue
        row = rows.setdefault(app, {x: "" for x in fields}); row["app"] = app
        for key, suffix in (("cycles", "cycles"), ("size", "size")):
            if result.metrics.get(key) is not None: row[f"{mode}_{suffix}"] = str(result.metrics[key])
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("w", newline="", dir=path.parent,
                                         prefix=path.name + ".", delete=False) as f:
            temporary = Path(f.name)
            writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n"); writer.writeheader()
            writer.writerows(rows[name] for name in sorted(rows))
        os.chmod(temporary, (path.stat().st_mode & 0o777) if path.exists() else 0o644)
        os.replace(temporary, path)
    finally:
        if temporary:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass


def write_report(results, path: Path, clock_hz: int, excluded_apps=None):
    new_file = not path.exists(); path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["machine", "os", "utc-timestamp", "app", "peep_ms", "peep_cycles", "peep_size", "nopeep_ms", "nopeep_cycles", "nopeep_size", "clock_hz"]
    grouped = {}
    failed_apps = {r.name.rsplit(":", 1)[0] for r in results if not r.passed}
    failed_apps.update(excluded_apps or ())
    for result in results:
        if result.metrics and result.passed and not result.name.startswith("extended/"):
            app, mode = result.name.rsplit(":", 1)
            if app not in failed_apps:
                grouped.setdefault(app, {})["peep" if mode == "fast" else mode] = result.metrics
    import datetime, platform
    with path.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        if new_file: writer.writeheader()
        for app, data in sorted(grouped.items()):
            row = {x: "" for x in fields}; row.update({"machine": platform.node(), "os": platform.system(), "utc-timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(), "app": app, "clock_hz": clock_hz})
            for mode, prefix in (("peep", "peep"), ("nopeep", "nopeep")):
                metric = data.get(mode)
                if metric: row[f"{prefix}_cycles"], row[f"{prefix}_size"] = metric.get("cycles", ""), metric.get("size", ""); row[f"{prefix}_ms"] = round(metric["cycles"] / clock_hz * 1000, 3) if metric.get("cycles") else ""
            writer.writerow(row)


def select_build_base(build_dir, no_ram_disk):
    """Use tmpfs only when both system memory and its free space allow it."""
    disk_base = ROOT / build_dir
    if no_ram_disk or not sys.platform.startswith("linux"):
        return disk_base
    if not os.access("/dev/shm", os.W_OK):
        return disk_base
    try:
        # Leave small Linux hosts (including Pi 4 models) on disk. MemTotal
        # excludes reserved memory, so nominal 8 GiB hosts also fall back.
        memory = {}
        for line in Path("/proc/meminfo").read_text().splitlines():
            key, value = line.split(":", 1)
            memory[key] = int(value.split()[0]) * 1024
        gib = 1024 ** 3
        if memory.get("MemTotal", 0) < 8 * gib:
            return disk_base
        if memory.get("MemAvailable", 0) < 4 * gib:
            return disk_base
        if shutil.disk_usage("/dev/shm").free < 4 * gib:
            return disk_base
    except (OSError, ValueError, IndexError):
        return disk_base
    return Path("/dev/shm/dcc-runall")

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
    parser.add_argument("-noRamDisk", "--no-ram-disk", action="store_true",
                        help="disable automatic tmpfs use (requires 8 GiB RAM, 4 GiB available RAM and tmpfs space)")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto",
                        help="ANSI colour mode (default: auto)")
    args = parser.parse_args()
    global COLOUR
    COLOUR = colour_enabled(args.color)
    args.failures_only = not args.verbose
    if args.timeout < 0:
        parser.error("--timeout must be zero (no timeout) or a positive integer")
    if args.throttle_limit <= 0:
        parser.error("--throttle-limit must be a positive integer")
    if args.report_clock_hz <= 0:
        parser.error("--report-clock-hz must be a positive integer")
    # Report measurements are normal builds, not
    # stack-guard builds, and therefore intentionally skip perf comparison.
    if args.report: args.no_stack_check = True
    args.baseline_dir = (ROOT / args.baseline_dir).resolve() if not Path(args.baseline_dir).is_absolute() else Path(args.baseline_dir)
    args.perf_baseline_file = (ROOT / args.perf_baseline_file).resolve() if not Path(args.perf_baseline_file).is_absolute() else Path(args.perf_baseline_file)
    args.report_file = (ROOT / args.report_file).resolve() if not Path(args.report_file).is_absolute() else Path(args.report_file)
    if args.serial: args.throttle_limit = 1
    if args.update_perf_baseline and (args.no_stack_check or args.report or not is_ntvcm(args.emulator)):
        parser.error("--update-perf-baseline requires a stack-checked ntvcm run without --report")
    if not args.no_stack_check: os.environ["DCC_FORCE_STACK_CHECK"] = "1"
    else: os.environ.pop("DCC_FORCE_STACK_CHECK", None)
    if args.extended:
        # Bootstrap before discovering or dispatching the main corpus.  A
        # missing extended checkout must be resolved (or reported) before we
        # spend time compiling the regular tests.
        extended_suite_dir = ROOT / "tests/extended-tests/tests/single-exec"
        if not ensure_extended_suite(ROOT, extended_suite_dir):
            return 1
    overrides = {x["name"].lower(): x for x in json.loads((ROOT / "tests/_test_overrides.json").read_text())["apps"]}
    apps = sorted(p.stem for p in (ROOT / "tests").glob("*.c"))
    if not apps:
        parser.error(f"no test applications found in {ROOT / 'tests'}")
    if args.apps:
        wanted = {x.strip().lower() for x in args.apps.split(",") if x.strip()}
        if not wanted:
            parser.error("--apps did not contain any application names")
        unknown = wanted - set(apps)
        if unknown: parser.error("unknown app(s): " + ", ".join(sorted(unknown)))
        apps = [x for x in apps if x in wanted]
    total_apps = len(apps)
    skipped = sum(bool(overrides.get(x, {}).get("ignore")) for x in apps)
    apps = [x for x in apps if not overrides.get(x, {}).get("ignore")]
    modes = ["peep", "nopeep"] if args.mode == "full" else ["peep" if args.mode == "fast" else "nopeep"]
    build_base = select_build_base(args.build_dir, args.no_ram_disk)
    runroot = build_base / f"run-{os.getpid()}"
    began = time.monotonic()
    out("--- stack-check: building every app with -fstack-check (default; use --no-stack-check to disable) ---" if not args.no_stack_check else "--- stack-check disabled (--no-stack-check) ---", "cyan" if not args.no_stack_check else "gray")
    out(f"Found {total_apps} test applications", "cyan")
    baseline_count = len(list(args.baseline_dir.glob("*.txt"))) if args.baseline_dir.is_dir() else 0
    out(f"Using per-app baselines from {display_path(args.baseline_dir)} ({baseline_count} files)\n", "cyan")
    section("STARTING BUILD AND RUN SUITE", close=False)
    out(f"Mode: {'full (fast + nopeep)' if args.mode == 'full' else args.mode}", "cyan")
    run_style = "(serial)" if args.serial else f"(parallel, throttle = {args.throttle_limit})"
    output_style = "failures only (PASS lines suppressed)" if args.failures_only else "verbose"
    out(f"Output: {output_style}\n{run_style}\nBuild root: {display_path(runroot)}", "gray")
    out("========================================", "cyan")
    fixture_specs = []
    baseline_paths = []
    for app in apps:
        override = overrides.get(app, {})
        fixture_specs += list(override.get("fixtures", []))
        baseline_paths.append(args.baseline_dir / f"{app}.txt")
        if override.get("run_batches"):
            baseline_paths.append(args.baseline_dir / f"{app}_batch.txt")
        for scenario in override.get("extra_scenarios", []):
            fixture_specs += list(scenario.get("fixtures", []))
            baseline_paths.append(args.baseline_dir / f"{app}_{scenario['suffix']}.txt")
    sources = fixture_sources(fixture_specs)
    baselines = {path: load_baseline(path) for path in set(baseline_paths)}
    batched = [app for app in apps if overrides.get(app, {}).get("run_batches")]
    args.batch_workers = batch_workers(args.throttle_limit, max(1, len(batched)*len(modes)),
                                      max([1] + [overrides[app]["run_batches"] for app in batched]))
    # Start expensive batched apps first, so other work overlaps their runs.
    apps.sort(key=lambda app: app not in batched)
    main_items = [WorkItem(app_job, (app, mode, overrides, sources, baselines, runroot, args),
                           f"{app}:{'fast' if mode == 'peep' else mode}",
                           args.batch_workers if app in batched else 1)
                  for app in apps for mode in modes]
    extended_items = []
    if args.extended:
        extended_items, extended_error = prepare_extended_suite(args, runroot, modes)
        if extended_error:
            out(f"  {extended_error}", "red")
            return 1
        out(f"Found {len(extended_items) // len(modes)} runnable extended test applications", "cyan")
    # Use one bounded queue for both corpora.  Main jobs are queued first so
    # the extended compiler workload does not contend during the main suite's
    # high-throughput phase; extended jobs then fill slots as its long tail
    # drains.  This retains one global throttle and fail-fast boundary.
    items = main_items + extended_items
    perf_active = is_ntvcm(args.emulator) and not args.no_stack_check and not args.report and not args.no_perf_check
    perf_baseline = load_perf_baselines(args.perf_baseline_file) if perf_active else {}
    fail_fast_regressions = []
    def fail_fast_perf(result):
        if result.name.startswith("extended/"):
            return None
        found = perf_regressions([result], overrides, perf_baseline)
        fail_fast_regressions.extend(found)
        return f"performance regression in {result.name}" if found else None
    main_phase = time.monotonic()
    main_execution = execute(items, args.throttle_limit, args.failures_only, args.fail_fast,
                             fail_fast_perf if perf_active and not args.update_perf_baseline else None)
    results = main_execution.results
    main_results = [result for result in results if not result.name.startswith("extended/")]
    extended_results = [result for result in results if result.name.startswith("extended/")]
    main_elapsed = time.monotonic() - main_phase
    completed_modes = {}
    for result in main_results:
        completed_modes.setdefault(result.name.rsplit(":", 1)[0], 0)
        completed_modes[result.name.rsplit(":", 1)[0]] += 1
    incomplete_apps = {app for app in apps if completed_modes.get(app, 0) < len(modes)}
    regressions = perf_regressions(main_results, overrides, perf_baseline, incomplete_apps) if perf_active else []
    for regression in fail_fast_regressions:
        if regression not in regressions:
            regressions.append(regression)
    if args.update_perf_baseline:
        update_perf_baseline(main_results, args.perf_baseline_file, incomplete_apps); regressions = []
    if args.report: write_report(main_results, args.report_file, args.report_clock_hz, incomplete_apps)
    if perf_active or args.update_perf_baseline:
        section("PERFORMANCE (CYCLE COUNT & .COM SIZE) CHECK")
        if args.update_perf_baseline:
            out(f"  Updated {display_path(args.perf_baseline_file)}", "cyan")
        else:
            out(f"  Regressions:  {len(regressions)}", "green" if not regressions else "red")
            for app, mode, kind, old, new in regressions: out(f"    - {app} ({mode}) {old:,} -> {new:,} {kind}", "red")
    diag_ok = peep_ok = None
    if not args.apps:
        section("RUNNING DIAGNOSTICS SUITE")
        diag_ok = diagnostics(runroot / "diagnostics", args.throttle_limit)
        out("  Diagnostics passed (output suppressed by -FailuresOnly)" if diag_ok else "  Diagnostics failed", "gray" if diag_ok else "red")
        section("RUNNING DCCPEEP FIXTURES")
        peep_ok = dccpeep_fixtures(args.throttle_limit)
        out("  Dccpeep fixtures passed (output suppressed by -FailuresOnly)" if peep_ok else "  Dccpeep fixtures failed", "gray" if peep_ok else "red")
    narrow_ok = None
    if args.narrow_diff:
        section("NARROW-DIFF CHECK (narrowing on vs -fno-narrow)")
        narrow_apps = [app for app in apps if not overrides.get(app, {}).get("narrow_diff_ignore")]
        narrow_items = [WorkItem(narrow_job, (app, overrides, sources, runroot / "narrow-diff", args),
                                 f"narrow/{app}") for app in narrow_apps]
        narrow_execution = execute(narrow_items, args.throttle_limit, args.failures_only, args.fail_fast)
        narrow_results = narrow_execution.results
        narrow_ok = (not narrow_execution.not_started and len(narrow_results) == len(narrow_items)
                     and all(r.passed for r in narrow_results))
        matched = sum(r.passed for r in narrow_results)
        out(f"  Narrow-diff:  {matched}/{len(narrow_items)} matched", "green" if narrow_ok else "red")
    extended_ok = (not args.extended or
                   (not main_execution.not_started and len(extended_results) == len(extended_items)
                    and all(result.passed for result in extended_results)))
    if args.extended and extended_ok:
        out("  Extended suite passed (output suppressed by -FailuresOnly)", "gray")
    app_failed = {r.name.rsplit(":", 1)[0] for r in main_results if not r.passed}
    passed_apps = sum(app not in app_failed and app not in incomplete_apps for app in apps)
    skipped += len(incomplete_apps - app_failed)
    failed_total = len(app_failed) + len(regressions) + (diag_ok is False) + (peep_ok is False) + (extended_ok is False) + (narrow_ok is False)
    section("TEST SUITE SUMMARY")
    out(f"  Total apps:   {total_apps}\n  Passed:       {passed_apps}", "green")
    out(f"  Failed:       {failed_total}", "green" if not failed_total else "red"); out(f"  Skipped:      {skipped}")
    if args.extended: out(f"  Extended:     {'passed' if extended_ok else 'failed'}", "green" if extended_ok else "red")
    out(f"  Diagnostics:  {'skipped' if diag_ok is None else ('passed' if diag_ok else 'failed')}", "gray" if diag_ok is None else ("green" if diag_ok else "red"))
    out(f"  Dccpeep:      {'skipped' if peep_ok is None else ('passed' if peep_ok else 'failed')}", "gray" if peep_ok is None else ("green" if peep_ok else "red"))
    if args.update_perf_baseline:
        perf_label, perf_colour = "baseline updated", "cyan"
    elif not perf_active:
        perf_label, perf_colour = "skipped", "gray"
    elif regressions:
        perf_label, perf_colour = str(len(regressions)) + " regression(s)", "red"
    else:
        perf_label, perf_colour = "passed", "green"
    out(f"  Performance:  {perf_label}", perf_colour)
    if args.narrow_diff: out(f"  Narrow-diff:  {'passed' if narrow_ok else 'failed'}", "green" if narrow_ok else "red")
    out(f"  Total time:   {time.monotonic()-began:.2f}s\n  Optimisation: {'full (fast + nopeep)' if args.mode == 'full' else args.mode}")
    if args.timing_breakdown:
        section("TIMING BREAKDOWN")
        total = time.monotonic() - began
        corpus_label = "Main + extended corpus" if args.extended else "Main app suite"
        out(f"  {corpus_label:22} {main_elapsed:7.2f}s ({main_elapsed / total * 100:5.1f}%)")
        out("  Detailed per-stage accounting is available from the detailed build diagnostics.", "gray")
    out("\n>>> SUCCESS: All tests passed <<<" if not failed_total else "\n>>> FAILURE: tests failed <<<", "green" if not failed_total else "red")
    if not args.keep_build: shutil.rmtree(runroot, ignore_errors=True)
    return failed_total != 0

if __name__ == "__main__":
    raise SystemExit(main())
