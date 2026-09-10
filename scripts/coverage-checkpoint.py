#!/usr/bin/env python3
"""Validate immutable inputs and completed profiles for staged coverage runs."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


class CheckpointError(ValueError):
    pass


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def input_files(root, paths):
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", *paths],
        check=True, capture_output=True,
    )
    files = {}
    for raw in sorted(set(result.stdout.split(b"\0")) - {b""}):
        name = raw.decode("utf-8")
        path = root / name
        if path.suffix == ".md":
            continue
        if path.is_dir():
            top = subprocess.check_output(
                ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
                text=True).strip()
            if Path(top).resolve() != path.resolve():
                raise CheckpointError(f"uninitialized coverage submodule: {name}")
            for child, value in input_files(path, []).items():
                files[f"{name}/{child}"] = value
            continue
        if not path.is_file():
            raise CheckpointError(f"coverage input missing or not a file: {name}")
        files[name] = digest(path)
    if not files:
        raise CheckpointError("no coverage inputs found")
    return files


def inputs(root):
    return input_files(root, ["src/dcc", "tests", "scripts", "*.h", "DCCRTL.MAC"])


def write_record(path, record):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def read_record(path):
    if not path.is_file():
        raise CheckpointError(f"missing coverage checkpoint: {path}; run build/collect first")
    record = json.loads(path.read_text())
    if not isinstance(record, dict) or record.get("version") != 1:
        raise CheckpointError(f"invalid coverage checkpoint: {path}")
    return record


def tool_records(paths):
    records = {}
    for name, path in paths.items():
        path = Path(path).absolute()
        if not path.is_file():
            raise CheckpointError(f"coverage tool missing: {name}: {path}")
        records[name] = {"path": str(path), "sha256": digest(path)}
    return records


def check_tools(records):
    for name, record in records.items():
        path = Path(record["path"])
        if not path.is_file() or digest(path) != record["sha256"]:
            raise CheckpointError(f"coverage tool changed: {name}: {path}")


def check_inputs(root, record):
    current = inputs(root)
    previous = record["inputs"]
    changed = sorted(name for name in current.keys() | previous.keys()
                     if current.get(name) != previous.get(name))
    if changed:
        raise CheckpointError("coverage inputs changed; rebuild and recollect: " +
                              ", ".join(changed[:8]))


def profile_records(build):
    raw = build / "raw"
    paths = sorted(raw.rglob("*.profraw"))
    if not paths or any(path.stat().st_size == 0 for path in paths):
        raise CheckpointError("missing or empty coverage profiles")
    return {str(path.relative_to(build)): digest(path) for path in paths}


def prepare(root, build, tools):
    for name in ("build.json", "collection.json"):
        (build / name).unlink(missing_ok=True)
    write_record(build / "inputs.json", {
        "version": 1, "inputs": inputs(root), "tools": tool_records(tools),
        "revision": subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
    })


def finish_build(root, build, binaries):
    record = read_record(build / "inputs.json")
    check_inputs(root, record)
    check_tools(record["tools"])
    record["binaries"] = tool_records(binaries)
    write_record(build / "build.json", record)


def check_build(root, build):
    record = read_record(build / "build.json")
    check_inputs(root, record)
    check_tools(record["tools"])
    check_tools(record["binaries"])
    return record


def start_collection(root, build):
    # An unsuccessful rerun must not leave a success stamp for earlier profiles.
    (build / "collection.json").unlink(missing_ok=True)
    check_build(root, build)


def finish_collection(root, build):
    check_build(root, build)
    manifest = build / "report" / "mir-clobber-executions.json"
    if not manifest.is_file():
        raise CheckpointError("missing clobber execution manifest")
    keys = json.loads(manifest.read_text(encoding="utf-8-sig"))
    if (not isinstance(keys, list) or not keys or
            any(not isinstance(key, str) or not key for key in keys) or
            len(set(keys)) != len(keys)):
        raise CheckpointError("invalid clobber execution manifest")
    write_record(build / "collection.json", {
        "version": 1, "build_sha256": digest(build / "build.json"),
        "profiles": profile_records(build),
        "execution_manifest_sha256": digest(manifest),
    })


def check_collection(root, build):
    check_build(root, build)
    record = read_record(build / "collection.json")
    if record["build_sha256"] != digest(build / "build.json"):
        raise CheckpointError("coverage collection belongs to a different build")
    if record["profiles"] != profile_records(build):
        raise CheckpointError("coverage profiles changed after collection")
    manifest = build / "report" / "mir-clobber-executions.json"
    if (not manifest.is_file() or
            record["execution_manifest_sha256"] != digest(manifest)):
        raise CheckpointError("clobber execution manifest changed after collection")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "built", "check",
                                          "start", "collected", "report"))
    parser.add_argument("--repo", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--tool", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()
    root, build = args.repo.resolve(), args.build_dir.resolve()
    try:
        tools = {}
        for item in args.tool:
            name, separator, path = item.partition("=")
            if not name or not separator or not path or name in tools:
                raise CheckpointError(f"invalid or repeated coverage tool: {item}")
            tools[name] = path
        if args.action == "prepare":
            prepare(root, build, tools)
        elif args.action == "built":
            finish_build(root, build, tools)
        elif args.action == "check":
            check_build(root, build)
        elif args.action == "start":
            start_collection(root, build)
        elif args.action == "collected":
            finish_collection(root, build)
        else:
            check_collection(root, build)
    except (CheckpointError, OSError, KeyError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        print(f"compiler-coverage: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
