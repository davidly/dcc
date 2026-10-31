import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
EMULATOR = shutil.which("ntvcm")
TOOLS = ("dcc", "dccmake", "dccpeep", "dccrtlstrip", "m80c", "l80c")

SOURCE = r"""
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

struct guarded {
    unsigned int before;
    meminfo_t info;
    unsigned int after;
};

static int valid(const meminfo_t *info)
{
    return info->heap_start <= info->heap_break &&
           info->heap_break <= info->heap_limit &&
           info->heap_capacity == info->heap_limit - info->heap_start &&
           info->heap_committed == info->heap_break - info->heap_start &&
           info->heap_growth == info->heap_limit - info->heap_break &&
           info->heap_committed + info->heap_growth == info->heap_capacity;
}

int main(void)
{
    struct guarded snapshot;
    meminfo_t initial, grown, hole, reused, final;
    void *first, *second, *replacement;
    snapshot.before = 0x1234;
    snapshot.after = 0x5678;
    errno = EDOM;
    meminfo(&snapshot.info);
    if (sizeof(meminfo_t) != 14 || snapshot.before != 0x1234 ||
        snapshot.after != 0x5678 || errno != EDOM ||
        !valid(&snapshot.info) || snapshot.info.stack_size != EXPECT_STACK)
        return 1;
    meminfo(&initial);
    if (initial.heap_start != snapshot.info.heap_start ||
        initial.heap_limit != snapshot.info.heap_limit ||
        initial.heap_break != snapshot.info.heap_break ||
        initial.heap_limit != *(unsigned int *)6 - EXPECT_STACK ||
        initial.heap_committed != 0 || initial.heap_capacity < 32768U)
        return 2;
    first = malloc(100);
    second = malloc(200);
    if (!first || !second)
        return 3;
    meminfo(&grown);
    if (!valid(&grown) || grown.heap_start != initial.heap_start ||
        grown.heap_limit != initial.heap_limit ||
        grown.heap_committed != 308 || grown.heap_capacity != initial.heap_capacity)
        return 4;
    free(first);
    meminfo(&hole);
    if (!valid(&hole) || hole.heap_break != grown.heap_break ||
        hole.heap_committed != grown.heap_committed ||
        hole.heap_growth != grown.heap_growth)
        return 5;
    replacement = malloc(50);
    if (replacement != first)
        return 6;
    meminfo(&reused);
    if (!valid(&reused) || reused.heap_break != grown.heap_break)
        return 7;
    free(replacement);
    free(second);
    meminfo(&final);
    if (!valid(&final) || final.heap_committed != 0 ||
        final.heap_break != initial.heap_break ||
        final.heap_growth != initial.heap_growth)
        return 8;
    printf("MEMINFO start=%u break=%u limit=%u\n",
           initial.heap_start, initial.heap_break, initial.heap_limit);
    puts("PASS: meminfo bounds, stack, committed space, free-hole reuse and trimming");
    return 0;
}
"""


@unittest.skipUnless(EMULATOR and all((ROOT / tool).is_file() for tool in TOOLS),
                     "built DCC tools and ntvcm are required")
class MemoryInfoTests(unittest.TestCase):
    def test_ordinary_com_snapshots_with_different_stack_reserves(self):
        with tempfile.TemporaryDirectory(prefix="dcc meminfo ") as directory:
            folder = Path(directory)
            (folder / "MEMINFO.C").write_text(SOURCE)
            settings = {
                "dcc-input": "MEMINFO.C", "dcc-output": "MEMINFO",
                "dcc-include-directory": str(ROOT),
                "dcc-runtime": str(ROOT / "DCCRTL.MAC"),
                **{f"{tool}-tool": str(ROOT / tool) for tool in TOOLS
                   if tool != "dccmake"},
            }
            (folder / "dccmake.txt").write_text(
                "".join(f"{key}={value}\n" for key, value in settings.items()))
            for stack in (256, 512, 2048):
                with self.subTest(stack=stack):
                    result = subprocess.run(
                        [str(ROOT / "dccmake"), f"dcc-stack-bytes={stack}",
                         f"dcc-define=EXPECT_STACK={stack}"],
                        cwd=folder, capture_output=True, text=True,
                        env={**os.environ, "DCC_ARGS": ""}, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run(
                        [EMULATOR, "build/MEMINFO.COM"], cwd=folder,
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("PASS: meminfo bounds", result.stdout)
                    symbols = (folder / "build/MEMINFO.SYM").read_text()
                    heap_start = re.search(r"\b([0-9A-F]{4})\s+__HSTA\b", symbols)
                    self.assertIsNotNone(heap_start, symbols)
                    start = int(heap_start.group(1), 16)
                    self.assertIn(f"MEMINFO start={start} break={start} ", result.stdout)


if __name__ == "__main__":
    unittest.main()
