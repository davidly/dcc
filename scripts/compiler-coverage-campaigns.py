#!/usr/bin/env python3
"""Run compiler mutation campaigns within one global worker budget."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import sys
import time


@dataclass(frozen=True)
class Campaign:
    name: str
    script: str
    arguments: tuple[str, ...] = ()
    output_dir: str | None = None
    max_jobs: int | None = None


# Longest observed campaigns come first. Ordering affects throughput only;
# every campaign still runs to completion and retains its own result checks.
CAMPAIGNS = (
    Campaign("pointer-condition", "pointer-condition-wave25-audit.py",
             output_dir="pointer-condition-wave25-audit", max_jobs=2),
    Campaign("symbol-insert", "symbol-insert-wave22-audit.py",
             output_dir="symbol-insert-wave22-audit"),
    Campaign("wrapper-init", "wrapper-init-wave22-campaign.py",
             ("--skip-runtime",)),
    Campaign("bitfield-report", "bitfield-report-wave23-audit.py",
             output_dir="bitfield-report-wave23-audit"),
    Campaign("flagged-record", "flagged-record-wave24-campaign.py",
             ("--skip-runtime",), "flagged-record-wave24-audit"),
    Campaign("multidim", "multidim-wave19-audit.py",
             output_dir="multidim-wave19-audit"),
    Campaign("directory", "directory-wave19-campaign.py",
             ("--skip-runtime",)),
    Campaign("wide-string", "wide-string-wave23-campaign.py",
             ("--skip-runtime",)),
    Campaign("action", "action-wave24-audit.py",
             output_dir="action-wave24-audit"),
    Campaign("endgame", "endgame-scope-wave18-campaign.py"),
    Campaign("ctype-realloc", "ctype-realloc-wave21-audit.py",
             output_dir="ctype-realloc-wave21-audit"),
    Campaign("memory-exercise", "memory-exercise-wave27-campaign.py",
             ("--skip-runtime",)),
    Campaign("long-index", "long-index-wave20-campaign.py",
             ("--skip-runtime",)),
    Campaign("affine-fill", "affine-fill-wave25-audit.py",
             output_dir="affine-fill-wave25-audit"),
    Campaign("catalan", "catalan-wave23-audit.py",
             output_dir="catalan-wave23-audit"),
    Campaign("catalan-driver-schedule", "catalan-wave60-audit.py",
             output_dir="catalan-wave60-audit", max_jobs=2),
    Campaign("promotion", "promotion-wave25-campaign.py",
             ("--skip-runtime",), "promotion-wave25-audit"),
    Campaign("byte-rotate", "byte-rotate-wave24-audit.py",
             output_dir="byte-rotate-wave24-audit"),
    Campaign("sliding", "sliding-wave20-campaign.py",
             ("--skip-runtime",)),
    Campaign("lcs", "lcs-wave24-audit.py",
             output_dir="lcs-wave24-audit", max_jobs=2),
    Campaign("do-while", "do-while-wave21-campaign.py",
             ("--skip-runtime",)),
    Campaign("packed-record", "packed-record-wave20-audit.py",
             output_dir="packed-record-wave20-audit"),
    Campaign("cast-logical", "cast-logical-wave26-campaign.py",
             ("--skip-runtime",)),
    Campaign("additive", "additive-wave23-audit.py",
             output_dir="additive-wave23-audit", max_jobs=2),
    Campaign("byte-math", "byte-math-wave19-audit.py",
             output_dir="byte-math-wave19-audit"),
    Campaign("byte-equality", "byte-equality-wave28-audit.py",
             output_dir="byte-equality-wave28-audit", max_jobs=2),
    Campaign("fixed-softmax", "fixed-softmax-wave22-audit.py",
             output_dir="fixed-softmax-wave22-audit", max_jobs=2),
    Campaign("fortran-fatal", "fortran-fatal-wave21-campaign.py",
             ("--skip-runtime",)),
    Campaign("fileio", "fileio-wave25-audit.py",
             output_dir="fileio-wave25-audit"),
    Campaign("buffered-console", "buffered-console-wave27-audit.py",
             output_dir="buffered-console-wave27-audit"),
    Campaign("divmod", "divmod-wave22-audit.py",
             output_dir="divmod-wave22-audit"),
    Campaign("abort", "abort-wave27-audit.py",
             output_dir="abort-wave27-audit", max_jobs=2),
    Campaign("minimax", "minimax-wave26-audit.py",
             output_dir="minimax-wave26-audit", max_jobs=2),
    Campaign("nonlocal", "nonlocal-wave28-campaign.py",
             ("--skip-runtime",)),
    Campaign("matrix-add", "matrix-add-wave21-audit.py",
             output_dir="matrix-add-wave21-audit"),
    Campaign("allocation-lifetime", "allocation-lifetime-wave28-audit.py",
             output_dir="allocation-lifetime-wave28-audit", max_jobs=2),
    Campaign("symbol-find", "symbol-find-wave21-audit.py",
             output_dir="symbol-find-wave21-audit"),
    Campaign("softmax", "softmax-wave19-campaign.py"),
    Campaign("matrix-store", "matrix-store-wave26-audit.py",
             output_dir="matrix-store-wave26-audit"),
    Campaign("for-increment", "for-increment-wave26-audit.py",
             output_dir="for-increment-wave26-audit", max_jobs=2),
    Campaign("callback-registration", "callback-registration-wave27-audit.py",
             output_dir="callback-registration-wave27-audit", max_jobs=2),
    Campaign("compound-check", "compound-wave45-audit.py",
             output_dir="compound-wave45-audit", max_jobs=2),
    Campaign("symbol-insert-schedule", "symbol-insert-wave46-audit.py",
             output_dir="symbol-insert-wave46-audit", max_jobs=2),
    Campaign("ctype-realloc-schedule", "ctype-realloc-wave48-audit.py",
             output_dir="ctype-realloc-wave48-audit", max_jobs=2),
    Campaign("byte-math-flags-schedule", "byte-math-wave53-audit.py",
             output_dir="byte-math-wave53-audit", max_jobs=2),
    Campaign("symbol-find-schedule", "symbol-find-wave62-audit.py",
             output_dir="symbol-find-wave62-audit", max_jobs=2),
    Campaign("pointer-condition-schedule",
             "pointer-condition-wave63-audit.py",
             output_dir="pointer-condition-wave63-audit", max_jobs=2),
    Campaign("exec-recursion-schedule",
             "exec-recursion-wave65-audit.py",
             output_dir="exec-recursion-wave65-audit", max_jobs=2),
    Campaign("float-tangent-rational",
             "float-tangent-wave64-audit.py",
             output_dir="float-tangent-wave64-audit", max_jobs=2),
    Campaign("float-atan2-schedule",
             "float-atan2-wave150-audit.py",
             output_dir="float-atan2-wave150-audit", max_jobs=2),
    Campaign("float-asin-schedule",
             "float-asin-wave230-audit.py",
             output_dir="float-asin-wave230-audit", max_jobs=2),
    Campaign("whitespace-scan-schedule",
             "whitespace-scan-wave70-audit.py",
             output_dir="whitespace-scan-wave70-audit", max_jobs=2),
    Campaign("random-wide-fill-schedule",
             "random-wide-fill-wave71-audit.py",
             output_dir="random-wide-fill-wave71-audit", max_jobs=2),
    Campaign("fixed-embedding-build",
             "fixed-embedding-wave80-audit.py",
             output_dir="fixed-embedding-wave80-audit", max_jobs=2),
    Campaign("packed-byte-report-schedule",
             "packed-byte-report-wave82-audit.py",
             output_dir="packed-byte-report-wave82-audit", max_jobs=2),
    Campaign("call-safe-member-sum-schedule",
             "call-safe-member-sum-wave90-audit.py",
             output_dir="call-safe-member-sum-wave90-audit", max_jobs=2),
    Campaign("gnarly-runner-schedule",
             "gnarly-runner-wave100-audit.py",
             output_dir="gnarly-runner-wave100-audit", max_jobs=2),
    Campaign("matrix-product-kind-schedule",
             "matrix-product-kind-wave140-audit.py",
             output_dir="matrix-product-kind-wave140-audit", max_jobs=2),
    Campaign("aggregate-field-sum-schedule",
             "aggregate-field-sum-wave170-audit.py",
             output_dir="aggregate-field-sum-wave170-audit", max_jobs=2),
    Campaign("bcd-byte-math-schedule",
             "bcd-byte-math-wave200-audit.py",
             output_dir="bcd-byte-math-wave200-audit", max_jobs=2),
    Campaign("indexed-word-sum-schedule",
             "indexed-word-sum-wave220-audit.py",
             output_dir="indexed-word-sum-wave220-audit", max_jobs=2),
    Campaign("allocator-bridge-schedule",
             "allocator-bridge-wave240-audit.py",
             output_dir="allocator-bridge-wave240-audit", max_jobs=2),
    Campaign("indexed-member-write-schedule",
             "indexed-member-write-wave260-audit.py",
             output_dir="indexed-member-write-wave260-audit", max_jobs=2),
    Campaign("wraparound-bool-step",
             "wraparound-bool-step-wave340-audit.py",
             output_dir="wraparound-bool-step-wave340-audit", max_jobs=2),
    Campaign("modular-product-schedule",
             "modular-product-wave520-audit.py",
             output_dir="modular-product-wave520-audit", max_jobs=2),
    Campaign("byte-record-copy-schedule",
             "byte-record-copy-wave1000-audit.py",
             output_dir="byte-record-copy-wave1000-audit", max_jobs=2),
    Campaign("board-matrix-print-schedule",
             "board-matrix-print-wave1100-audit.py",
             output_dir="board-matrix-print-wave1100-audit", max_jobs=2),
    Campaign("qsort-edge-schedule",
             "qsort-edge-wave1200-audit.py",
             output_dir="qsort-edge-wave1200-audit", max_jobs=2),
    Campaign("fixed-softmax-schedule",
             "fixed-softmax-wave1300-audit.py",
             output_dir="fixed-softmax-wave1300-audit", max_jobs=2),
    Campaign("board-search-schedule",
             "board-search-wave1600-audit.py",
             output_dir="board-search-wave1600-audit", max_jobs=2),
    Campaign("scope-block-runner-schedule",
             "scope-block-wave1700-audit.py",
             output_dir="scope-block-wave1700-audit", max_jobs=2),
    Campaign("global-array-fma-schedule",
             "global-array-fma-wave1800-audit.py",
             output_dir="global-array-fma-wave1800-audit", max_jobs=2),
    Campaign("wide-hash33-schedule",
             "wide-hash33-wave1900-audit.py",
             output_dir="wide-hash33-wave1900-audit", max_jobs=2),
    Campaign("fortran-grow-schedule",
             "fortran-grow-wave2000-audit.py",
             output_dir="fortran-grow-wave2000-audit", max_jobs=2),
    Campaign("list-reverse-schedule",
             "list-reverse-wave2400-audit.py",
             output_dir="list-reverse-wave2400-audit", max_jobs=2),
    Campaign("variadic-join-report-schedule",
             "variadic-join-report-wave2300-audit.py",
             output_dir="variadic-join-report-wave2300-audit", max_jobs=2),
    Campaign("recursive-byte-minimax-schedule",
             "recursive-byte-minimax-wave2600-audit.py",
             output_dir="recursive-byte-minimax-wave2600-audit",
             max_jobs=2),
    Campaign("allocator-stress-schedule",
             "allocator-stress-wave2800-audit.py",
             output_dir="allocator-stress-wave2800-audit", max_jobs=2),
    Campaign("anonymous-initializer-report-schedule",
             "anonymous-initializer-report-wave3200-audit.py",
             output_dir="anonymous-initializer-report-wave3200-audit",
             max_jobs=2),
    Campaign("pi-digit-schedule",
             "pi-digit-wave4000-audit.py",
             output_dir="pi-digit-wave4000-audit", max_jobs=2),
    Campaign("float-sweep-schedule",
             "float-sweep-wave4100-audit.py",
             output_dir="float-sweep-wave4100-audit", max_jobs=2),
    Campaign("board-attack-schedule",
             "board-attack-wave6000-audit.py",
             output_dir="board-attack-wave6000-audit", max_jobs=2),
    Campaign("unnamed-bitfield-report-schedule",
             "unnamed-bitfield-report-wave6100-audit.py",
             output_dir="unnamed-bitfield-report-wave6100-audit",
             max_jobs=2),
    Campaign("backward-pass-schedule",
             "backward-pass-wave7000-audit.py",
             output_dir="backward-pass-wave7000-audit",
             max_jobs=2),
    Campaign("arrow-path-schedule",
             "arrow-path-wave7100-audit.py",
             output_dir="arrow-path-wave7100-audit",
             max_jobs=2),
    Campaign("raw-conversion-check-schedule",
             "raw-conversion-check-wave8000-audit.py",
             output_dir="raw-conversion-check-wave8000-audit",
             max_jobs=2),
    Campaign("directory-enumeration-runner-schedule",
             "directory-enumeration-runner-wave8100-audit.py",
             output_dir="directory-enumeration-runner-wave8100-audit",
             max_jobs=2),
    Campaign("nested-for-runner-schedule",
             "nested-for-runner-wave8200-audit.py",
             output_dir="nested-for-runner-wave8200-audit",
             max_jobs=2),
    Campaign("matrix-multiply-schedule",
             "matrix-multiply-wave8300-audit.py",
             output_dir="matrix-multiply-wave8300-audit",
             max_jobs=2),
)


def campaign_job_count(campaign, budget, default_jobs):
    jobs = min(budget, default_jobs)
    if campaign.max_jobs is not None:
        jobs = min(jobs, campaign.max_jobs)
    return jobs


def take_ready(pending, available, budget, default_jobs):
    ready = []
    waiting = []
    for campaign in pending:
        jobs = campaign_job_count(campaign, budget, default_jobs)
        if jobs <= available:
            ready.append((campaign, jobs))
            available -= jobs
        else:
            waiting.append(campaign)
    return ready, waiting, available


def campaign_command(campaign, jobs, root, build_dir):
    command = [
        sys.executable,
        str(root / "scripts" / campaign.script),
        "--jobs",
        str(jobs),
        *campaign.arguments,
    ]
    if campaign.output_dir is not None:
        command.extend(
            ["--output-dir", str(build_dir / campaign.output_dir)]
        )
    return command


def emit_log(path, failed):
    output = sys.stderr.buffer if failed else sys.stdout.buffer
    with path.open("rb") as source:
        while chunk := source.read(65536):
            output.write(chunk)
    output.flush()


def run_campaigns(root, build_dir, raw_dir, campaign_dir, budget,
                  default_jobs, selected_campaigns=CAMPAIGNS):
    pending = list(selected_campaigns)
    running = {}
    failed = []
    available = budget
    raw_dir.mkdir(parents=True, exist_ok=True)
    campaign_dir.mkdir(parents=True, exist_ok=True)

    try:
        while pending or running:
            ready, pending, available = take_ready(
                pending, available, budget, default_jobs
            )
            for campaign, jobs in ready:
                log_path = campaign_dir / f"{campaign.name}.log"
                log = log_path.open("wb")
                environment = os.environ.copy()
                environment["LLVM_PROFILE_FILE"] = str(
                    raw_dir / f"dcc-{campaign.name}-%8m.profraw"
                )
                command = campaign_command(
                    campaign, jobs, root, build_dir
                )
                print(
                    f"mutation scheduler: start {campaign.name} "
                    f"jobs={jobs}",
                    flush=True,
                )
                process = subprocess.Popen(
                    command,
                    cwd=root,
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
                running[process.pid] = (
                    process, campaign, jobs, log, log_path
                )

            if not running:
                raise RuntimeError(
                    "no mutation campaign fits the worker budget"
                )

            completed = []
            while not completed:
                for pid, state in running.items():
                    if state[0].poll() is not None:
                        completed.append(pid)
                if not completed:
                    time.sleep(0.1)

            for pid in completed:
                process, campaign, jobs, log, log_path = running.pop(pid)
                log.close()
                available += jobs
                campaign_failed = process.returncode != 0
                emit_log(log_path, campaign_failed)
                if campaign_failed:
                    failed.append(campaign.name)
                print(
                    f"mutation scheduler: finish {campaign.name} "
                    f"jobs={jobs} status={process.returncode}",
                    file=sys.stderr if campaign_failed else sys.stdout,
                    flush=True,
                )
    except BaseException:
        for process, _, _, log, _ in running.values():
            process.terminate()
            log.close()
        for process, _, _, _, _ in running.values():
            process.wait()
        raise

    if failed:
        print(
            "compiler-coverage: mutation campaigns failed: "
            + ", ".join(failed),
            file=sys.stderr,
        )
        return 1
    return 0


def positive_integer(value):
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=positive_integer, required=True)
    parser.add_argument(
        "--campaign-jobs", type=positive_integer, default=4
    )
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--campaign-dir", type=Path, required=True)
    parser.add_argument(
        "--campaign",
        action="append",
        choices=[campaign.name for campaign in CAMPAIGNS],
        dest="campaign_names",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    selected = (
        CAMPAIGNS
        if args.campaign_names is None
        else tuple(
            campaign for campaign in CAMPAIGNS
            if campaign.name in args.campaign_names
        )
    )
    return run_campaigns(
        root,
        args.build_dir.resolve(),
        args.raw_dir.resolve(),
        args.campaign_dir.resolve(),
        args.jobs,
        args.campaign_jobs,
        selected,
    )


if __name__ == "__main__":
    raise SystemExit(main())
