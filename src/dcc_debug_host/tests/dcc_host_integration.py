#!/usr/bin/env python3
"""Compile real DCC programs and exercise full-system source debugging."""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

from dcc_host_mi_test import MISession, evaluate


def write_source(path, text):
    path.write_text(text.lstrip(), encoding="ascii")


def source_line(path, marker):
    for number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if marker in line:
            return number
    raise AssertionError(f"marker {marker!r} not found in {path}")


def build(dcc_root, directory, output, *sources, line_debug=False):
    command = [str(dcc_root / "dccmake"), "-g"]
    command.extend(str(source) for source in sources)
    command.extend(
        (
            f"dcc-output={output}",
            f"dcc-include-directory={dcc_root}",
            f"l80-command={dcc_root / 'l80c.com'}",
            f"dcc-runtime={dcc_root / 'DCCRTL.MAC'}",
            "dcc-build-dir=build",
        )
    )
    if line_debug:
        command.append("dcc-debug=lines")
    completed = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=120)
    if completed.returncode:
        raise AssertionError(f"DCC build failed:\n{completed.stdout}{completed.stderr}")
    program = directory / "build" / f"{output}.COM"
    metadata = directory / "build" / f"{output}.DBG"
    assert program.exists() and metadata.exists(), (program, metadata)
    return program


def test_recursive_frames(host, dcc_root, root):
    source = root / "frame.c"
    write_source(
        source,
        """
static int recurse(int depth)
{
    int marker = depth; /* RECURSE_BREAK */
    if (depth) marker += recurse(depth - 1);
    return marker;
}

int main(void)
{
    return recurse(2) != 3;
}
""",
    )
    program = build(dcc_root, root, "DBGFRAME", source)
    line = source_line(source, "RECURSE_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        inserted = session.command(f'-break-insert -i 2 "{source}:{line}"')
        breakpoint = re.search(r'number="(\d+)"', inserted).group(1)
        session.command("-exec-run", stop=True)
        assert evaluate(session, "depth") == "0"
        frames = session.command("-stack-list-frames")
        assert frames.count('func="recurse"') == 3 and 'func="main"' in frames, frames
        inline = session.command("-stack-list-locals --frame 1 1")
        assert 'name="depth",value="1"' in inline, inline
        current_object = session.command('-var-create - * "depth"')
        current_name = re.search(r'name="([^"]+)"', current_object).group(1)
        assert 'value="0"' in current_object, current_object
        stale_object = session.command('-var-create --frame 2 - * "depth"')
        stale_name = re.search(r'name="([^"]+)"', stale_object).group(1)
        assert 'value="2"' in stale_object, stale_object
        session.command("-stack-select-frame 1")
        assert 'name="depth",value="1"' in session.command("-stack-list-locals 1")
        session.command(f"-break-delete {breakpoint}")
        _, stopped = session.command("-exec-finish", stop=True)
        assert 'reason="end-stepping-range"' in stopped, stopped
        frames = session.command("-stack-list-frames")
        assert frames.count('func="recurse"') == 1, frames
        update = session.command("-var-update --all-values *")
        current_update = next(item for item in update.split("{") if f'name="{current_name}"' in item)
        assert 'in_scope="false"' in current_update, update
        stale_update = next(item for item in update.split("{") if f'name="{stale_name}"' in item)
        assert 'in_scope="false"' in stale_update, update
    finally:
        session.close()


def test_rich_values(host, dcc_root, root):
    source = root / "values.c"
    write_source(
        source,
        """
struct Bits { unsigned int low : 3; signed int high : 4; };
union Number { int whole; unsigned char bytes[2]; };
int globals[3] = { 10, 20, 30 };

static int plus_one(int value) { return value + 1; }

int main(void)
{
    const int folded = 44;
    int local = 7;
    int array[3] = { 1, 2, 3 };
    struct Bits bits;
    union Number number;
    int *pointer = &array[1];
    int (*function_pointer)(int) = plus_one;
    bits.low = 5;
    bits.high = -2;
    number.whole = 0x1234;
    local += function_pointer(*pointer); /* VALUES_BREAK */
    {
        int local = 99;
        globals[0] = local; /* SHADOW_BREAK */
    }
    return local != 10 || folded != 44;
}
""",
    )
    program = build(dcc_root, root, "DBGVAL", source)
    values_line = source_line(source, "VALUES_BREAK")
    shadow_line = source_line(source, "SHADOW_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command(f'-break-insert -c "local == 7" "{source}:{values_line}"')
        session.command(f'-break-insert "{source}:{shadow_line}"')
        session.command("-exec-run", stop=True)
        locals_result = session.command("-stack-list-locals 1")
        for name in ("local", "array", "bits", "number", "pointer", "function_pointer"):
            assert f'name="{name}"' in locals_result, locals_result
        assert evaluate(session, "local") == "7"
        assert evaluate(session, "array[2]") == "3"
        assert evaluate(session, "globals[1]") == "20"
        assert evaluate(session, "bits.low") == "5"
        assert evaluate(session, "bits.high") == "-2"
        assert evaluate(session, "number.whole") == "4660"
        assert evaluate(session, "*pointer") == "2"
        function_pointer = session.command('-var-create - * "function_pointer"')
        assert 'type="int (*)()"' in function_pointer, function_pointer
        local_object = session.command('-var-create - * "local"')
        object_name = re.search(r'name="([^"]+)"', local_object).group(1)
        assert 'value="8"' in session.command(f'-var-assign {object_name} "8"')
        session.command("-exec-continue", stop=True)
        assert evaluate(session, "local") == "99"
    finally:
        session.close()


def test_static_local_metadata(host, dcc_root, root):
    source = root / "static.c"
    write_source(
        source,
        """
static unsigned char lookup(unsigned char index)
{
    static const unsigned char values[3] = { 4, 5, 6 };
    return values[index];
}

int main(void)
{
    return lookup(1) != 5;
}
""",
    )
    program = build(dcc_root, root, "DBGSTAT", source)
    metadata = program.with_suffix(".DBG").read_text(encoding="ascii")
    assert not any(
        line.startswith("variable-end ") and '"values"' in line
        for line in metadata.splitlines()
    )
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
    finally:
        session.close()


def test_vla(host, dcc_root, root):
    source = root / "vla.c"
    write_source(
        source,
        """
int main(void)
{
    int count = 3;
    int values[count];
    values[0] = 11;
    values[1] = 22;
    values[2] = 33;
    return values[2] != 33; /* VLA_BREAK */
}
""",
    )
    program = build(dcc_root, root, "DBGVLA", source)
    line = source_line(source, "VLA_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command(f'-break-insert "{source}:{line}"')
        session.command("-exec-run", stop=True)
        assert evaluate(session, "count") == "3"
        assert [evaluate(session, f"values[{index}]") for index in range(3)] == ["11", "22", "33"]
    finally:
        session.close()


def test_multidimensional_values(host, dcc_root, root):
    source = root / "matrix.c"
    write_source(
        source,
        """
struct Pair { int left; int right; };

int main(void)
{
    int rows = 2;
    int fixed[2][3];
    int dynamic[rows][3];
    struct Pair pairs[2];
    struct Pair *pair_pointer = &pairs[1];
    long long_value = -123456L;
    float float_value = 1.5f;
    _Bool flag = 7;
    fixed[1][0] = 40;
    fixed[1][2] = 42;
    dynamic[1][0] = 60;
    dynamic[1][2] = 62;
    pairs[1].left = 70;
    pairs[1].right = 71;
    return fixed[1][2] != 42; /* MATRIX_BREAK */
}
""",
    )
    program = build(dcc_root, root, "DBGMAT", source)
    line = source_line(source, "MATRIX_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command(f'-break-insert "{source}:{line}"')
        session.command("-exec-run", stop=True)
        metadata_lines = [
            record
            for record in program.with_suffix(".DBG").read_text(encoding="ascii").splitlines()
            if '"fixed"' in record or '"dynamic"' in record
        ]
        fixed_10 = evaluate(session, "fixed[1][0]")
        fixed_12 = evaluate(session, "fixed[1][2]")
        dynamic_10 = evaluate(session, "dynamic[1][0]")
        dynamic_12 = evaluate(session, "dynamic[1][2]")
        assert fixed_10 == "40", (fixed_10, metadata_lines)
        assert fixed_12 == "42", (fixed_12, metadata_lines)
        assert dynamic_10 == "60", (dynamic_10, metadata_lines)
        assert dynamic_12 == "62", (dynamic_12, metadata_lines)
        assert evaluate(session, "pairs[1].left") == "70"
        assert evaluate(session, "pairs[1].right") == "71"
        assert evaluate(session, "pair_pointer->left") == "70"
        assert evaluate(session, "pair_pointer->right") == "71"
        assert evaluate(session, "long_value") == "-123456"
        assert evaluate(session, "float_value") == "1.5"
        assert evaluate(session, "flag") == "1"
        fixed_object = session.command('-var-create - * "fixed"')
        assert 'type="int[2][3]"' in fixed_object, fixed_object
        pointer_object = session.command('-var-create - * "pair_pointer"')
        assert 'type="struct Pair *"' in pointer_object, pointer_object
    finally:
        session.close()


def test_multimodule(host, dcc_root, root):
    main_source = root / "main.c"
    module_source = root / "module.c"
    write_source(
        main_source,
        """
struct MainValue { int left; };
struct MainValue main_value = { 7 };
int inspect(void);
int main(void) { return inspect() != 49; }
""",
    )
    write_source(
        module_source,
        """
struct ModuleValue { int right; };
struct ModuleValue module_value = { 42 };
extern struct MainValue { int left; } main_value;
int inspect(void)
{
    int result = module_value.right + main_value.left; /* AGG_BREAK */
    return result;
}
""",
    )
    program = build(dcc_root, root, "DBGAGG", main_source, module_source)
    line = source_line(module_source, "AGG_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command(f'-break-insert "{module_source}:{line}"')
        session.command("-exec-run", stop=True)
        assert evaluate(session, "module_value.right") == "42"
        assert evaluate(session, "main_value.left") == "7"
    finally:
        session.close()


def test_arguments(host, dcc_root, root):
    source = root / "args.c"
    write_source(
        source,
        """
int main(int argc, char **argv)
{
    int valid = argc == 3 && argv[1][0] == 'O' && argv[2][2] == 'O';
    return !valid; /* ARGS_BREAK */
}
""",
    )
    program = build(dcc_root, root, "DBGARGS", source)
    line = source_line(source, "ARGS_BREAK")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command("-exec-arguments ONE TWO")
        session.command(f'-break-insert "{source}:{line}"')
        session.command("-exec-run", stop=True)
        assert evaluate(session, "argc") == "3"
        assert evaluate(session, "argv[1][0]") == "79"
        assert evaluate(session, "argv[2][2]") == "79"
        assert evaluate(session, "valid") == "1"
    finally:
        session.close()


def test_optimized_values(host, dcc_root, root):
    source = root / "optvars.c"
    write_source(
        source,
        """
struct Pair { int left; int right; };
struct Pair global_pair = { 3, 4 };

static int helper(int argument)
{
    const int fixed = 9;
    int array[2];
    int *pointer;
    int local = argument + 2;
    local = local * 3; /* OPT_REGISTER_BREAK */
    array[0] = local;
    array[1] = fixed;
    pointer = &array[0];
    global_pair.left = local;
    return *pointer + array[1] + global_pair.right; /* OPT_AGGREGATE_BREAK */
}

static long wide_helper(int argument)
{
    long wide = 0x12345678L;
    wide += argument; /* OPT_WIDE_BEFORE */
    return wide; /* OPT_WIDE_AFTER */
}

int main(void)
{
    int failed = helper(5) != 34;
    if (wide_helper(1) != 0x12345679L)
        failed = 1;
    return failed;
}
""",
    )
    program = build(dcc_root, root, "DBGOPT", source, line_debug=True)
    register_line = source_line(source, "OPT_REGISTER_BREAK")
    aggregate_line = source_line(source, "OPT_AGGREGATE_BREAK")
    wide_before_line = source_line(source, "OPT_WIDE_BEFORE")
    wide_after_line = source_line(source, "OPT_WIDE_AFTER")
    session = MISession(host)
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command(f'-break-insert "{source}:{register_line}"')
        session.command(f'-break-insert "{source}:{aggregate_line}"')
        session.command(f'-break-insert "{source}:{wide_before_line}"')
        session.command(f'-break-insert "{source}:{wide_after_line}"')
        session.command("-exec-run", stop=True)
        variables = session.command("-stack-list-variables 1")
        assert 'name="local",value="7"' in variables, variables
        assert 'name="argument",value="5"' in variables, variables
        assert evaluate(session, "fixed") == "9"
        created = session.command('-var-create - * "local"')
        variable_name = re.search(r'name="([^"]+)"', created).group(1)
        assert 'attr="editable"' in session.command(
            f"-var-show-attributes {variable_name}"
        )
        assert 'value="8"' in session.command(f"-var-assign {variable_name} 8")

        _, stopped = session.command("-exec-continue", stop=True)
        assert 'reason="breakpoint-hit"' in stopped, stopped
        assert f'line="{aggregate_line}"' in stopped, stopped
        assert evaluate(session, "fixed") == "9"
        assert evaluate(session, "array[0]") == "24"
        assert evaluate(session, "array[1]") == "9"
        assert evaluate(session, "*pointer") == "24"
        assert evaluate(session, "global_pair.left") == "24"
        assert evaluate(session, "global_pair.right") == "4"

        _, stopped = session.command("-exec-continue", stop=True)
        assert f'line="{wide_before_line}"' in stopped, stopped
        assert evaluate(session, "argument") == "1"
        assert evaluate(session, "wide") == "305419896"
        _, stopped = session.command("-exec-continue", stop=True)
        assert f'line="{wide_after_line}"' in stopped, stopped
        assert evaluate(session, "argument") == "1"
        assert evaluate(session, "wide") == "305419897"
    finally:
        session.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host", type=Path)
    parser.add_argument("dcc_root", type=Path)
    arguments = parser.parse_args()
    host = arguments.host.resolve()
    dcc_root = arguments.dcc_root.resolve()
    for required in (host, dcc_root / "dccmake", dcc_root / "DCCRTL.MAC"):
        if not required.exists():
            parser.error(f"required file not found: {required}")
    with tempfile.TemporaryDirectory(prefix="dcc-debug-") as directory:
        root = Path(directory)
        test_recursive_frames(host, dcc_root, root)
        test_rich_values(host, dcc_root, root)
        test_static_local_metadata(host, dcc_root, root)
        test_vla(host, dcc_root, root)
        test_multidimensional_values(host, dcc_root, root)
        test_multimodule(host, dcc_root, root)
        test_arguments(host, dcc_root, root)
        test_optimized_values(host, dcc_root, root)
    print("real DCC full-CP/M debugger integration passed")


if __name__ == "__main__":
    main()