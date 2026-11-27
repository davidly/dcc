"""Endpoint publication works without Unix APIs and closes files on failure."""
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "terminal_bridge", ROOT / "src/dcc_debug_host/dcc_host_terminal_bridge.py")
BRIDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRIDGE)


class EndpointTests(unittest.TestCase):
    def test_publish_without_fchmod_replaces_endpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            endpoint = Path(directory) / "terminal.endpoint"
            endpoint.write_text("old endpoint")
            with mock.patch.object(BRIDGE.os, "fchmod", None, create=True):
                BRIDGE.publish_endpoint(endpoint, 12345, "test-token")
            self.assertEqual(endpoint.read_text(), "127.0.0.1 12345 test-token\n")
            self.assertEqual(list(Path(directory).iterdir()), [endpoint])

    @unittest.skipUnless(hasattr(os, "fchmod"), "Unix file permissions required")
    def test_published_endpoint_is_private(self):
        with tempfile.TemporaryDirectory() as directory:
            endpoint = Path(directory) / "terminal.endpoint"
            BRIDGE.publish_endpoint(endpoint, 12345, "test-token")
            self.assertEqual(stat.S_IMODE(endpoint.stat().st_mode), 0o600)

    def test_permission_failure_closes_file_before_cleanup(self):
        handles = []

        def fail_permissions(handle, mode):
            handles.append(handle)
            raise OSError("permission update failed")

        with tempfile.TemporaryDirectory() as directory:
            endpoint = Path(directory) / "terminal.endpoint"
            endpoint.write_text("old endpoint")
            with mock.patch.object(BRIDGE.os, "fchmod", fail_permissions, create=True):
                with self.assertRaisesRegex(OSError, "permission update failed"):
                    BRIDGE.publish_endpoint(endpoint, 12345, "test-token")
            self.assertEqual(endpoint.read_text(), "old endpoint")
            self.assertEqual(list(Path(directory).iterdir()), [endpoint])
            self.assertEqual(len(handles), 1)
            with self.assertRaises(OSError):
                os.fstat(handles[0])
