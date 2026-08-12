#!/usr/bin/env python3
"""Compile C modules and enforce a minimal exported-symbol surface."""

import argparse
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


FUNCTION_TYPES = set("TtFfIiWw")
WRITABLE_DATA_TYPES = set("BbDdCcSsGgOoVvu")
READ_ONLY_DATA_TYPES = set("RrAaNnPp")


class AuditError(Exception):
    """An actionable compiler, nm, or input error."""


def command_words(value):
    try:
        return shlex.split(value, posix=os.name != "nt")
    except ValueError as exc:
        raise AuditError("could not parse command %r: %s" % (value, exc)) from exc


def find_command(words, description):
    if not words:
        raise AuditError("%s command is empty" % description)
    executable = shutil.which(words[0])
    if executable is None:
        raise AuditError(
            "%s %r was not found on PATH; install it or pass the corresponding "
            "command-line option" % (description, words[0])
        )
    return [executable] + words[1:]


def default_compiler():
    if os.environ.get("CC"):
        return os.environ["CC"]
    if os.name == "nt":
        return "cl"
    if platform.system() == "Darwin":
        return "clang"
    return "gcc"


def default_nm():
    if os.environ.get("NM"):
        return os.environ["NM"]
    candidates = ("llvm-nm", "nm") if os.name == "nt" else ("nm", "llvm-nm")
    for candidate in candidates:
        if shutil.which(candidate):
            return candidate
    return "nm"


def is_msvc_style(compiler):
    name = Path(compiler[0]).name.lower()
    return name in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}


def compiler_flags(msvc_style):
    if msvc_style:
        return ["/nologo", "/GS-", "/O2", "/Zi", "/std:c11"]
    if os.environ.get("CFLAGS"):
        return command_words(os.environ["CFLAGS"])
    flags = ["-std=c11", "-Wall", "-Wextra", "-O2", "-g"]
    if platform.system() == "Darwin":
        flags.append("-fno-common")
    return flags


def compile_module(compiler, source, object_path, include_dir):
    msvc_style = is_msvc_style(compiler)
    flags = compiler_flags(msvc_style)
    if msvc_style:
        command = (
            compiler
            + flags
            + [
                "/I" + str(include_dir),
                "/c",
                str(source),
                "/Fo" + str(object_path),
                "/Fd" + str(object_path.parent / "audit.pdb"),
            ]
        )
    else:
        command = (
            compiler
            + flags
            + [
                "-I",
                str(include_dir),
                "-c",
                str(source),
                "-o",
                str(object_path),
            ]
        )

    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        message = "compiler failed for %s\n  command: %s" % (
            source,
            shlex.join(command),
        )
        if details:
            message += "\n  output:\n    " + details.replace("\n", "\n    ")
        raise AuditError(message)


def run_nm(nm_command, object_path):
    command = nm_command + ["-g", "--defined-only", str(object_path)]
    result = subprocess.run(command, text=True, capture_output=True)

    if result.returncode != 0:
        diagnostic = (result.stdout + result.stderr).lower()
        unsupported_option = any(
            word in diagnostic
            for word in (
                "unknown option",
                "illegal option",
                "invalid option",
                "unrecognized option",
            )
        )
        if unsupported_option:
            command = nm_command + ["-g", str(object_path)]
            result = subprocess.run(command, text=True, capture_output=True)

    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        message = "nm failed for %s\n  command: %s" % (
            object_path,
            shlex.join(command),
        )
        if details:
            message += "\n  output:\n    " + details.replace("\n", "\n    ")
        message += (
            "\n  install GNU nm or llvm-nm, or select one explicitly with --nm"
        )
        raise AuditError(message)
    return result.stdout


def parse_nm_output(output):
    symbols = []
    for raw_line in output.splitlines():
        parts = raw_line.strip().split()
        if len(parts) < 2:
            continue
        if len(parts[-2]) == 1:
            symbol_type, name = parts[-2], parts[-1]
        elif len(parts) >= 3 and len(parts[1]) == 1:
            name, symbol_type = parts[0], parts[1]
        else:
            continue
        if symbol_type == "U":
            continue
        symbols.append((name, symbol_type))
    return symbols


def display_symbol_name(name):
    if platform.system() == "Darwin" or os.name == "nt":
        if name.startswith("_"):
            name = name[1:]
        if os.name == "nt":
            name = re.sub(r"@\d+$", "", name)
    return name


def strip_c_noise(text):
    result = []
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "*":
                result.extend((" ", " "))
                index += 2
                state = "block-comment"
                continue
            if char == "/" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "line-comment"
                continue
            if char == '"':
                result.append(" ")
                state = "string"
            elif char == "'":
                result.append(" ")
                state = "character"
            else:
                result.append(char)
        elif state == "block-comment":
            if char == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
                continue
            result.append("\n" if char == "\n" else " ")
        elif state == "line-comment":
            result.append("\n" if char == "\n" else " ")
            if char == "\n":
                state = "code"
        elif state in {"string", "character"}:
            quote = '"' if state == "string" else "'"
            if char == "\\" and following:
                result.extend((" ", "\n" if following == "\n" else " "))
                index += 2
                continue
            result.append("\n" if char == "\n" else " ")
            if char == quote:
                state = "code"
        index += 1

    lines = []
    in_directive = False
    for line in "".join(result).splitlines(keepends=True):
        stripped = line.lstrip()
        if in_directive or stripped.startswith("#"):
            in_directive = line.rstrip("\r\n").endswith("\\")
            lines.append("\n" if line.endswith(("\n", "\r")) else "")
        else:
            lines.append(line)
    return "".join(lines)


def static_top_level_function_count(text):
    clean = strip_c_noise(text)
    depth = 0
    start = 0
    count = 0
    for index, char in enumerate(clean):
        if char == "{" and depth == 0:
            header = clean[start:index].strip()
            if (
                re.search(r"\bstatic\b", header)
                and re.search(r"\b[A-Za-z_]\w*\s*\([^;{}]*\)\s*$", header, re.S)
            ):
                count += 1
            depth = 1
        elif char == "{":
            depth += 1
        elif char == "}" and depth:
            depth -= 1
            if depth == 0:
                start = index + 1
        elif depth == 0 and char == ";":
            start = index + 1
    return count


def resolve_source(path_text, repo_root):
    path = Path(path_text).expanduser()
    candidates = (
        [path] if path.is_absolute() else [Path.cwd() / path, repo_root / path]
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise AuditError(
        "C module %r was not found relative to the current directory or repository root"
        % path_text
    )


def relative_display(path, repo_root):
    try:
        return str(path.relative_to(repo_root))
    except ValueError:
        return str(path)


def format_symbols(symbols):
    if not symbols:
        return "(none)"
    return ", ".join("%s [%s]" % item for item in symbols)


def audit_module(source, object_path, repo_root, compiler, nm_command, allowed):
    text = source.read_text(errors="replace")
    compile_module(compiler, source, object_path, repo_root / "src/dcc")
    symbols = parse_nm_output(run_nm(nm_command, object_path))

    functions = []
    writable = []
    read_only = []
    unknown = []
    for raw_name, symbol_type in symbols:
        item = (display_symbol_name(raw_name), symbol_type)
        if symbol_type in FUNCTION_TYPES:
            functions.append(item)
        elif symbol_type in WRITABLE_DATA_TYPES:
            writable.append(item)
        elif symbol_type in READ_ONLY_DATA_TYPES:
            read_only.append(item)
        else:
            unknown.append(item)

    functions.sort()
    writable.sort()
    read_only.sort()
    unknown.sort()
    unexpected = [item for item in functions if item[0] not in allowed]

    print(relative_display(source, repo_root))
    print("  source LOC: %d" % len(text.splitlines()))
    print(
        "  static top-level functions (best effort): %d"
        % static_top_level_function_count(text)
    )
    print("  exported functions: %s" % format_symbols(functions))
    print("  exported read-only data: %s" % format_symbols(read_only))
    print("  writable data: %s" % format_symbols(writable))
    if unknown:
        print("  unclassified exports: %s" % format_symbols(unknown))

    errors = []
    if writable:
        errors.append(
            "exported writable data violates the zero-shared-state policy: "
            + format_symbols(writable)
        )
    for name, symbol_type in unexpected:
        errors.append(
            "unexpected exported function %s [%s]; allow it explicitly with "
            "--allow-function %s" % (name, symbol_type, name)
        )
    if unknown:
        errors.append(
            "unclassified exported symbol type; treat this as unsafe and inspect: "
            + format_symbols(unknown)
        )

    if errors:
        print("  result: FAIL")
        for error in errors:
            print("  ERROR: " + error)
        return False
    print("  result: PASS")
    return True


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description=(
            "Compile one or more C modules with dcc's host C11/include settings, "
            "inspect defined global symbols with nm, and enforce a zero-writable-"
            "shared-state/minimal-function-export policy."
        ),
        epilog=(
            "Examples:\n"
            "  python3 scripts/audit-c-module-exports.py src/dcc/module.c\n"
            "  python3 scripts/audit-c-module-exports.py src/dcc/module.c \\\n"
            "      --allow-function module_dispatch\n"
            "  python3 scripts/audit-c-module-exports.py --cc clang --nm llvm-nm \\\n"
            "      src/dcc/first.c src/dcc/second.c\n\n"
            "Relative module paths may be repository-relative even when invoked "
            "from another working directory. Temporary objects are created under "
            "the repository build directory and always removed."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("modules", nargs="+", metavar="C_MODULE")
    parser.add_argument(
        "--allow-function",
        action="append",
        default=[],
        metavar="NAME",
        help="permit one exported function; repeat for additional dispatch entries",
    )
    parser.add_argument(
        "--cc",
        default=default_compiler(),
        metavar="COMMAND",
        help="host C compiler command (default: CC, then platform repository default)",
    )
    parser.add_argument(
        "--nm",
        default=default_nm(),
        metavar="COMMAND",
        help="nm command, including llvm-nm (default: NM, nm, or llvm-nm)",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    try:
        compiler = find_command(command_words(args.cc), "C compiler")
        nm_command = find_command(command_words(args.nm), "nm")
        sources = [resolve_source(path, repo_root) for path in args.modules]
        build_dir = repo_root / "build"
        build_dir_existed = build_dir.exists()
        build_dir.mkdir(exist_ok=True)
        object_suffix = ".obj" if is_msvc_style(compiler) else ".o"
        passed = True
        try:
            with tempfile.TemporaryDirectory(
                prefix="audit-c-module-exports-", dir=build_dir
            ) as temporary:
                temporary_path = Path(temporary)
                for index, source in enumerate(sources):
                    if index:
                        print()
                    object_path = temporary_path / ("%03d%s" % (index, object_suffix))
                    try:
                        module_passed = audit_module(
                            source,
                            object_path,
                            repo_root,
                            compiler,
                            nm_command,
                            set(args.allow_function),
                        )
                        passed = passed and module_passed
                    except AuditError as exc:
                        print(relative_display(source, repo_root))
                        print("  result: ERROR")
                        print("  ERROR: " + str(exc).replace("\n", "\n  "))
                        passed = False
        finally:
            if not build_dir_existed:
                try:
                    build_dir.rmdir()
                except OSError:
                    pass
        return 0 if passed else 1
    except AuditError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
