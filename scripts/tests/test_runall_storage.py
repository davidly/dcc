"""Low-memory hosts must not fill tmpfs during a full test pass."""
import argparse
import json
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from scripts import runall


class BuildStorageTests(unittest.TestCase):
    def test_storage_selection(self):
        gib = 1024 ** 3
        cases = [
            ("large host", "linux", False, True, 16, 8, 8, True),
            ("Pi 4", "linux", False, True, 4, 3, 2, False),
            ("8 GiB Pi with reserved RAM", "linux", False, True, 7.9, 6, 4, False),
            ("busy host", "linux", False, True, 16, 3, 8, False),
            ("small tmpfs", "linux", False, True, 16, 8, 2, False),
            ("explicit disk", "linux", True, True, 16, 8, 8, False),
            ("unwritable", "linux", False, False, 16, 8, 8, False),
            ("Windows", "win32", False, True, 16, 8, 8, False),
            ("macOS", "darwin", False, True, 16, 8, 8, False),
        ]
        for name, platform, disabled, writable, total, available, free, use_tmpfs in cases:
            with self.subTest(name=name), \
                 patch.object(runall.sys, "platform", platform), \
                 patch.object(runall.os, "access", return_value=writable), \
                 patch.object(Path, "read_text", return_value=
                              "MemTotal: %d kB\nMemAvailable: %d kB\n" %
                              (total * gib / 1024, available * gib / 1024)), \
                 patch.object(runall.shutil, "disk_usage", return_value=SimpleNamespace(free=free * gib)):
                expected = Path("/dev/shm/dcc-runall") if use_tmpfs else runall.ROOT / "custom-build"
                self.assertEqual(runall.select_build_base("custom-build", disabled), expected)

    def test_unavailable_memory_information_uses_disk(self):
        with patch.object(runall.sys, "platform", "linux"), \
             patch.object(runall.os, "access", return_value=True), \
             patch.object(Path, "read_text", side_effect=OSError("unavailable")):
            self.assertEqual(runall.select_build_base("build", False), runall.ROOT / "build")

    def test_a1_timeout_override(self):
        apps = json.loads((runall.ROOT / "tests/_test_overrides.json").read_text())["apps"]
        override = next(app for app in apps if app["name"] == "a1")
        for configured, expected in ((60, 300), (600, 600), (0, 0)):
            self.assertEqual(runall.app_timeout_args(argparse.Namespace(timeout=configured), override).timeout,
                             expected)


if __name__ == "__main__":
    unittest.main()
