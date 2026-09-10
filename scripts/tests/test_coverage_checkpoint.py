import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "coverage_checkpoint", Path(__file__).resolve().parents[1] / "coverage-checkpoint.py")
checkpoint = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checkpoint)


class CoverageCheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Test",
                        "-c", "user.email=test@example.invalid", "commit",
                        "--allow-empty", "-qm", "test"], check=True)
        self.source = self.root / "src/dcc/a.c"
        self.source.parent.mkdir(parents=True)
        self.source.write_text("int x;\n")
        self.build = self.root / "build"
        self.build.mkdir()
        self.tool = self.build / "tool"
        self.tool.write_bytes(b"tool")
        checkpoint.prepare(self.root, self.build, {"clang": self.tool})
        checkpoint.finish_build(self.root, self.build, {"dcc": self.tool})

    def complete(self):
        checkpoint.start_collection(self.root, self.build)
        (self.build / "raw").mkdir(exist_ok=True)
        (self.build / "raw/p.profraw").write_bytes(b"profile")
        (self.build / "report").mkdir(exist_ok=True)
        (self.build / "report/mir-clobber-executions.json").write_text(
            json.dumps(["case|stack-peep", "case|nostack-nopeep"]))
        checkpoint.finish_collection(self.root, self.build)

    def test_matching_checkpoint(self):
        self.complete()
        checkpoint.check_collection(self.root, self.build)

    def test_source_change(self):
        self.source.write_text("int y;\n")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "inputs changed"):
            checkpoint.check_build(self.root, self.build)

    def test_new_input(self):
        self.source.with_name("b.c").write_text("int z;\n")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "inputs changed"):
            checkpoint.check_build(self.root, self.build)

    def test_docs_only_change(self):
        (self.source.parent / "README.md").write_text("Documentation")
        checkpoint.check_build(self.root, self.build)

    def test_uninitialized_submodule(self):
        child = self.root / "tests/submodule"
        child.mkdir(parents=True)
        subprocess.run(
            ["git", "-C", str(self.root), "update-index", "--add",
             "--cacheinfo", "160000",
             subprocess.check_output(
                 ["git", "-C", str(self.root), "rev-parse", "HEAD"],
                 text=True).strip(), "tests/submodule"], check=True)
        with self.assertRaisesRegex(checkpoint.CheckpointError, "uninitialized"):
            checkpoint.inputs(self.root)

    def test_submodule_inputs_are_hashed(self):
        child = self.root / "tests/submodule"
        child.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(child)], check=True)
        (child / "fixture.c").write_text("int x;\n")
        subprocess.run(["git", "-C", str(child), "add", "fixture.c"], check=True)
        subprocess.run(["git", "-C", str(child), "-c", "user.name=Test",
                        "-c", "user.email=test@example.invalid", "commit",
                        "-qm", "fixture"], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "tests/submodule"],
                       check=True, capture_output=True)
        old = checkpoint.inputs(self.root)
        (child / "fixture.c").write_text("int y;\n")
        self.assertNotEqual(old, checkpoint.inputs(self.root))

    def test_changed_binary(self):
        self.tool.write_bytes(b"different")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "tool changed"):
            checkpoint.check_build(self.root, self.build)

    def test_source_changed_during_build(self):
        self.source.write_text("changed")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "inputs changed"):
            checkpoint.finish_build(self.root, self.build, {"dcc": self.tool})

    def test_report_requires_completed_collection(self):
        with self.assertRaisesRegex(checkpoint.CheckpointError, "missing coverage checkpoint"):
            checkpoint.check_collection(self.root, self.build)

    def test_empty_profiles_rejected(self):
        with self.assertRaisesRegex(checkpoint.CheckpointError, "missing clobber"):
            checkpoint.finish_collection(self.root, self.build)
        self.complete()
        (self.build / "raw/p.profraw").write_bytes(b"")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "empty coverage"):
            checkpoint.check_collection(self.root, self.build)

    def test_profile_change(self):
        self.complete()
        (self.build / "raw/p.profraw").write_bytes(b"other profile")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "profiles changed"):
            checkpoint.check_collection(self.root, self.build)

    def test_extra_profile(self):
        self.complete()
        (self.build / "raw/unexpected.profraw").write_bytes(b"extra")
        with self.assertRaisesRegex(checkpoint.CheckpointError, "profiles changed"):
            checkpoint.check_collection(self.root, self.build)

    def test_duplicate_execution_manifest(self):
        self.complete()
        (self.build / "report/mir-clobber-executions.json").write_text(
            '["same", "same"]')
        with self.assertRaisesRegex(checkpoint.CheckpointError, "invalid clobber"):
            checkpoint.finish_collection(self.root, self.build)

    def test_manifest_tampering(self):
        self.complete()
        (self.build / "report/mir-clobber-executions.json").write_text('["other"]')
        with self.assertRaisesRegex(checkpoint.CheckpointError, "manifest changed"):
            checkpoint.check_collection(self.root, self.build)

    def test_rerun_invalidates_old_success(self):
        self.complete()
        checkpoint.start_collection(self.root, self.build)
        with self.assertRaisesRegex(checkpoint.CheckpointError, "missing coverage checkpoint"):
            checkpoint.check_collection(self.root, self.build)


if __name__ == "__main__":
    unittest.main()
