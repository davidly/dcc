import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless((ROOT / "dccmake").is_file(), "built dccmake is required")
class CompositeProjectTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dccmake projects ")
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)

    def leaf(self, directory, output, source="int main(void) { return 0; }\n"):
        folder = self.folder / directory
        folder.mkdir(parents=True)
        (folder / "MAIN.C").write_text(source)
        (folder / "dccmake.txt").write_text(
            f"dcc-input=MAIN.C\ndcc-output={output}\n"
            f"dcc-runtime={ROOT / 'DCCRTL.MAC'}\n"
            f"dcc-include-directory={ROOT}\n"
            f"dcc-tool={ROOT / 'dcc'}\n"
            f"dccpeep-tool={ROOT / 'dccpeep'}\n"
            f"dccrtlstrip-tool={ROOT / 'dccrtlstrip'}\n"
            f"m80c-tool={ROOT / 'm80c'}\n"
            f"l80c-tool={ROOT / 'l80c'}\n")
        return folder

    def build(self, *arguments):
        return subprocess.run(
            [str(ROOT / "dccmake"), *arguments], cwd=self.folder,
            capture_output=True, text=True,
            env={**os.environ, "DCC_ARGS": ""})

    def test_builds_children_in_declared_order_and_forwards_options(self):
        first = self.leaf("first project", "ONE")
        second = self.leaf("second project", "TWO")
        (self.folder / "dccmake.txt").write_text(
            "dcc-projects=first project,second project\n")
        result = self.build("dcc-stack-bytes=1024",
                            "dcc-library-linker=not-needed-for-COM")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertLess(result.stdout.index("first project"),
                        result.stdout.index("second project"))
        for folder, output in ((first, "ONE"), (second, "TWO")):
            self.assertTrue((folder / f"build/{output}.COM").is_file())
            self.assertRegex((folder / f"build/{output}.MAC").read_text(),
                             r"__stack_size\s+equ\s+1024")
        self.assertIn("built composite", result.stdout)

    def test_failure_does_not_build_later_children(self):
        self.leaf("first", "ONE", "#error intentional child failure\n")
        second = self.leaf("second", "TWO")
        (self.folder / "dccmake.txt").write_text("dcc-projects=first,second\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("project 2/2", result.stdout)
        self.assertFalse((second / "build").exists())
        self.assertNotIn("built composite", result.stdout)

    def test_nested_projects_resolve_paths_from_their_own_directory(self):
        self.leaf("one", "ONE")
        self.leaf("two", "TWO")
        group = self.folder / "group"
        group.mkdir()
        (group / "dccmake.txt").write_text("dcc-projects=../one,../two\n")
        (self.folder / "dccmake.txt").write_text("dcc-projects=group\n")
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.folder / "one/build/ONE.COM").is_file())
        self.assertTrue((self.folder / "two/build/TWO.COM").is_file())

    def test_rejects_cycles(self):
        group = self.folder / "group"
        group.mkdir()
        (group / "dccmake.txt").write_text("dcc-projects=..\n")
        (self.folder / "dccmake.txt").write_text("dcc-projects=group\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("composite project cycle", result.stderr)

    def test_rejects_missing_child_configuration(self):
        (self.folder / "empty").mkdir()
        (self.folder / "dccmake.txt").write_text("dcc-projects=empty\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("has no dccmake.txt", result.stderr)

    def test_rejects_mixed_root_and_leaf_configuration(self):
        (self.folder / "dccmake.txt").write_text(
            "dcc-projects=lib,host\ndcc-input=MAIN.C\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("keep build settings in children", result.stderr)

    def test_rejects_empty_projects_and_command_line_project_lists(self):
        (self.folder / "dccmake.txt").write_text("dcc-projects=\n")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("at least one child", result.stderr)
        (self.folder / "dccmake.txt").write_text("")
        result = self.build("dcc-projects=lib,host")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not command-line overrides", result.stderr)
