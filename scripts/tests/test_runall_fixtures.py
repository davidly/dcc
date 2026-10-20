"""Compiler-independent fixture staging contracts for the Python runner."""

import stat
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts import runall


class FixtureStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "source.txt"
        self.source.write_bytes(b"Read-only fixture.\r\n")
        self.destination = self.root / "staged"
        self.target = self.destination / "READONLY.TXT"
        self.sources = {"readonly.txt": self.source}
        self.fixture = {"name": "readonly.txt", "read_only": True}
        self.addCleanup(self.cleanup)

    def cleanup(self):
        if self.destination.exists():
            for target in self.destination.iterdir():
                target.chmod(target.stat().st_mode | stat.S_IWUSR)
        self.temp.cleanup()

    def test_read_only_applies_only_to_uppercase_staged_copy(self):
        source_mode = self.source.stat().st_mode
        self.assertEqual(runall.stage([self.fixture], self.destination, self.sources), [])
        self.assertEqual([p.name for p in self.destination.iterdir()], ["READONLY.TXT"])
        self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
        self.assertEqual(self.target.stat().st_mode &
                         (stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH), 0)
        self.assertEqual(self.source.stat().st_mode, source_mode)

    def test_read_only_fixture_can_be_restaged(self):
        runall.stage([self.fixture], self.destination, self.sources)
        self.source.write_bytes(b"Replacement contents")
        self.assertEqual(runall.stage([self.fixture], self.destination, self.sources), [])
        self.assertEqual(self.target.read_bytes(), b"Replacement contents")
        self.assertEqual(self.target.stat().st_mode &
                         (stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH), 0)

    def test_writable_specs_can_replace_read_only_copy(self):
        for fixture in ("readonly.txt", {"name": "readonly.txt"},
                        {"name": "readonly.txt", "read_only": False}):
            with self.subTest(fixture=fixture):
                runall.stage([self.fixture], self.destination, self.sources)
                self.assertEqual(runall.stage([fixture], self.destination, self.sources), [])
                self.assertTrue(self.target.stat().st_mode & stat.S_IWUSR)
                self.target.write_bytes(b"Writable")
                self.assertEqual(self.source.read_bytes(), b"Read-only fixture.\r\n")

    def test_read_only_explicit_sources(self):
        for source in (str(self.source), self.source.name):
            with self.subTest(source=source), patch.object(runall, "ROOT", self.root):
                fixture = dict(self.fixture, source=source)
                self.assertEqual(runall.stage([fixture], self.destination, {}), [])
                self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
                self.assertFalse(self.target.stat().st_mode & stat.S_IWUSR)

    def test_missing_read_only_source_is_reported(self):
        self.assertEqual(runall.stage([self.fixture], self.destination, {}), ["readonly.txt"])
        self.assertFalse(self.target.exists())

    def test_permission_errors_are_not_suppressed(self):
        with patch.object(Path, "chmod", side_effect=PermissionError("fixture chmod denied")):
            with self.assertRaisesRegex(PermissionError, "fixture chmod denied"):
                runall.stage([self.fixture], self.destination, self.sources)


if __name__ == "__main__":
    unittest.main()
