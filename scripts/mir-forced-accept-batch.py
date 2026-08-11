#!/usr/bin/env python3
"""Batch-run forced-accept full-mode A/B for a list of MIR fallback candidates.

Proving a near-miss fallback candidate is a real (not just textually smaller)
win today means, for each candidate, a separate round trip:

    DCC_MIR_FORCE_ACCEPT_FINAL_FUNCTION=<function> \
        pwsh ./scripts/runall.ps1 -Apps <app> -Mode full -RunTimeout 30

one app/function at a time. Since each candidate's forced-accept build+run is
a fully independent process (distinct env var, distinct app), this script
runs a whole candidate list's forced-accept full-mode checks concurrently
(one subprocess per candidate, pool sized to the CPU count) and prints one
ranked report: correctness pass/fail plus peep/nopeep cycle-count and .COM
size deltas versus tests/perf_baselines.csv, replacing what would otherwise
be N sequential manual investigations.

This intentionally reuses runall.ps1's own on-by-default cycle-count/size
regression check (against tests/perf_baselines.csv) rather than adding a
second, separate performance-comparison mechanism -- the reported deltas are
exactly what a normal `-Mode full` run would show for a real acceptance, not
an approximation.

Candidates file format: one `app<TAB>function` (or `app:function`, or
`app,function`) per line; blank lines and `#`-prefixed comments are ignored.
This is the same shape produced by scripts/mir-gate-margins.py's near-miss
listing (`app:function  gen=... cap=... ...`), so its output lines can be fed
through directly after stripping the trailing metrics, e.g.:

    mir-gate-margins.py census.tsv --reason absolute-index-cost --top 5 \
        | awk '{print $1}' | grep : > candidates.txt
    mir-forced-accept-batch.py candidates.txt

Usage:
    python3 scripts/mir-forced-accept-batch.py candidates.txt [options]
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field


REGRESSION_RE = re.compile(
    r"^\s*-\s+(?P<app>\S+)\s+\((?P<mode>[^)]+)\)\s+"
    r"(?P<baseline>[\d,]+)\s+->\s+(?P<actual>[\d,]+)\s+(?P<metric>.+?)\s+"
    r"\((?P<sign>[+-])(?P<pct>[\d.]+)%\)\s*$"
)
PASS_FAIL_RE = re.compile(r"^\s*(?:\[[^\]]*\]\s*)?(?P<status>PASS|FAIL)\s+(?P<app>\S+)\s")


@dataclass
class CandidateResult:
    app: str
    function: str
    correctness_pass: bool | None = None
    regressions: list[str] = field(default_factory=list)
    improvements: list[str] = field(default_factory=list)
    error: str | None = None
    log_path: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("candidates", help="file of app<TAB>function candidates (one per line)")
    parser.add_argument(
        "--jobs", type=int, default=os.cpu_count() or 4,
        help="max concurrent forced-accept runs (default: CPU count)",
    )
    parser.add_argument(
        "--timeout", type=int, default=30,
        help="per-candidate runall.ps1 -RunTimeout in seconds (default: 30)",
    )
    parser.add_argument(
        "--scratch-root", default=None,
        help="root directory for per-candidate scratch build dirs "
        "(default: /dev/shm if writable, else system temp dir)",
    )
    parser.add_argument(
        "--keep-logs", action="store_true",
        help="do not delete per-candidate stdout/stderr logs on success",
    )
    parser.add_argument(
        "--ntvcm-dir", default=os.environ.get("NTVCM_REPO", "/home/dave/GitHub/ntvcm"),
        help="local ntvcm checkout to freshness-check before trusting cycle counts "
        "(default: $NTVCM_REPO or /home/dave/GitHub/ntvcm)",
    )
    parser.add_argument(
        "--skip-ntvcm-check", action="store_true",
        help="skip the ntvcm freshness preflight (not recommended; see T387)",
    )
    return parser.parse_args()


def check_ntvcm_freshness(ntvcm_dir: str) -> None:
    """Warn loudly if the local ntvcm checkout is behind origin/main.

    This is the T387 lesson: a stale local ntvcm binary silently undercounts
    some Z80 instructions' cycle cost, which can make every cycle-count claim
    in this tool's report look like an improvement when it is not. Best
    effort only -- never fail the whole batch over a network hiccup.
    """
    if not os.path.isdir(ntvcm_dir):
        print(f"warning: ntvcm dir '{ntvcm_dir}' not found; skipping freshness check", file=sys.stderr)
        return
    try:
        subprocess.run(
            ["git", "-C", ntvcm_dir, "fetch", "origin"],
            capture_output=True, text=True, timeout=20, check=True,
        )
        behind = subprocess.run(
            ["git", "-C", ntvcm_dir, "log", "--oneline", "HEAD..origin/main"],
            capture_output=True, text=True, timeout=10, check=True,
        )
        if behind.stdout.strip():
            print(
                "*** WARNING: local ntvcm checkout is behind origin/main; "
                "cycle-count deltas below may be unreliable (see T387). "
                "Run: git -C {} pull && rebuild ntvcm".format(ntvcm_dir),
                file=sys.stderr,
            )
        which_ntvcm = shutil.which("ntvcm")
        if which_ntvcm:
            binary_mtime = os.path.getmtime(which_ntvcm)
            src_mtime = max(
                (os.path.getmtime(os.path.join(ntvcm_dir, f))
                 for f in os.listdir(ntvcm_dir) if f.endswith((".cxx", ".hxx", ".h"))),
                default=0,
            )
            if src_mtime > binary_mtime:
                print(
                    f"*** WARNING: ntvcm binary at {which_ntvcm} is older than sources "
                    f"in {ntvcm_dir}; rebuild it before trusting cycle counts (see T387)",
                    file=sys.stderr,
                )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"warning: ntvcm freshness check failed ({exc}); proceeding anyway", file=sys.stderr)


def load_candidates(path: str) -> list[tuple[str, str]]:
    candidates: list[tuple[str, str]] = []
    with open(path) as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            # Accept mir-gate-margins.py's "app:function  gen=..." shape directly:
            # strip anything after the first run of whitespace.
            line = line.split()[0]
            for sep in ("\t", ":", ","):
                if sep in line:
                    app, function = line.split(sep, 1)
                    candidates.append((app.strip(), function.strip()))
                    break
            else:
                print(f"warning: skipping unparseable candidate line: {raw_line!r}", file=sys.stderr)
    return candidates


def repo_root() -> str:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
    )
    return out.stdout.strip()


def run_one_candidate(
    root: str, scratch_root: str, app: str, function: str, timeout: int, keep_logs: bool
) -> CandidateResult:
    result = CandidateResult(app=app, function=function)
    build_dir = tempfile.mkdtemp(prefix=f"mir-fab-{app}-{function}-", dir=scratch_root)
    log_fd, log_path = tempfile.mkstemp(prefix=f"mir-fab-{app}-{function}-", suffix=".log", dir=scratch_root)
    result.log_path = log_path
    env = dict(os.environ)
    env["DCC_MIR_FORCE_ACCEPT_FINAL_FUNCTION"] = function
    cmd = [
        "pwsh", "./scripts/runall.ps1",
        "-Apps", app, "-Mode", "full",
        "-RunTimeout", str(timeout),
        "-BuildDir", build_dir, "-NoRamDisk",
        "-FailuresOnly:$false",
    ]
    try:
        proc = subprocess.run(
            cmd, cwd=root, env=env, capture_output=True, text=True,
            timeout=timeout + 30,
        )
        with os.fdopen(log_fd, "w") as log:
            log.write(proc.stdout)
            log.write(proc.stderr)

        for line in proc.stdout.splitlines():
            m = PASS_FAIL_RE.match(line)
            if m and m.group("app") == app:
                result.correctness_pass = m.group("status") == "PASS"
            m = REGRESSION_RE.match(line)
            if m and m.group("app") == app:
                summary = (
                    f"{m.group('mode')}: {m.group('baseline')} -> {m.group('actual')} "
                    f"{m.group('metric')} ({m.group('sign')}{m.group('pct')}%)"
                )
                if m.group("sign") == "+":
                    result.regressions.append(summary)
                else:
                    result.improvements.append(summary)

        if result.correctness_pass is None:
            result.error = "could not find PASS/FAIL line in output (see log)"
    except subprocess.TimeoutExpired:
        os.close(log_fd) if not log_fd else None
        result.error = f"timed out after {timeout + 30}s"
    except OSError as exc:
        result.error = str(exc)
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)
        if not keep_logs and not result.error and not result.regressions:
            try:
                os.remove(log_path)
                result.log_path = None
            except OSError:
                pass
    return result


def print_report(results: list[CandidateResult]) -> int:
    exit_code = 0
    ranked = sorted(
        results,
        key=lambda r: (
            r.error is not None,
            r.correctness_pass is False,
            len(r.regressions),
            r.app, r.function,
        ),
    )
    for r in ranked:
        label = f"{r.app}:{r.function}"
        if r.error:
            print(f"  ERROR   {label:40s} {r.error}")
            exit_code = 1
            continue
        status = "PASS" if r.correctness_pass else "FAIL"
        if not r.correctness_pass:
            exit_code = 1
        line = f"  {status}    {label:40s}"
        if r.regressions:
            exit_code = 1
            line += "  REGRESSIONS: " + "; ".join(r.regressions)
        if r.improvements:
            line += "  improvements: " + "; ".join(r.improvements)
        if not r.regressions and not r.improvements:
            line += "  (no baseline row(s) to compare, or no metric change)"
        print(line)
        if r.log_path:
            print(f"           log: {r.log_path}")
    print()
    clean = sum(1 for r in results if r.correctness_pass and not r.regressions and not r.error)
    print(f"{clean}/{len(results)} candidate(s) pass correctness with zero cycle/size regressions.")
    return exit_code


def main() -> int:
    args = parse_args()
    root = repo_root()

    if not args.skip_ntvcm_check:
        check_ntvcm_freshness(args.ntvcm_dir)

    candidates = load_candidates(args.candidates)
    if not candidates:
        print("no candidates parsed from", args.candidates, file=sys.stderr)
        return 2

    scratch_root = args.scratch_root
    if scratch_root is None:
        scratch_root = "/dev/shm" if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK) else None

    print(f"Running {len(candidates)} candidate(s), up to {args.jobs} concurrently...\n")

    results: list[CandidateResult] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(run_one_candidate, root, scratch_root, app, function, args.timeout, args.keep_logs): (app, function)
            for app, function in candidates
        }
        for future in as_completed(futures):
            results.append(future.result())

    return print_report(results)


if __name__ == "__main__":
    sys.exit(main())
