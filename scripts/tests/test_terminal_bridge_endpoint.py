"""Endpoint publication works without Unix APIs and closes files on failure."""
import importlib.util
import os
import queue
import socket
import subprocess
import sys
import threading
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

    # Newer Windows Python provides fchmod, but Windows mode bits do not
    # express Unix owner-only permissions.
    @unittest.skipUnless(os.name == "posix" and hasattr(os, "fchmod"),
                         "Unix file permissions required")
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

class BridgeLifecycleTests(unittest.TestCase):
    def check_shutdown(self, reason):
        with tempfile.TemporaryDirectory(prefix="dcc-bridge-lifecycle-") as directory:
            endpoint = Path(directory) / "terminal.endpoint"
            input_file = None
            if reason == "file":
                source = Path(directory) / "input.bin"
                source.write_bytes(b"K\x1d")
                input_file = source.open("rb")
            process = subprocess.Popen(
                [sys.executable, str(Path(BRIDGE.__file__)),
                 "--endpoint-file", str(endpoint)],
                stdin=input_file if input_file is not None else subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            lines = queue.Queue()

            def read_output():
                for line in process.stdout:
                    lines.put(line)

            reader = threading.Thread(target=read_output, daemon=True)
            reader.start()

            def wait_for(marker):
                for _ in range(4):
                    line = lines.get(timeout=5)
                    if marker in line:
                        return
                self.fail("bridge did not report " + repr(marker))

            client = None
            try:
                wait_for(b"DCC debug terminal ready")
                address, port, token = endpoint.read_text(encoding="ascii").split()
                client = socket.create_connection((address, int(port)), timeout=5)
                client.sendall((token + "\n").encode("ascii"))
                wait_for(b"DCC target terminal connected")
                # Confirm the input path is running before closing the peer.
                # Keep stdin open for disconnect/detach: closing it here masks
                # the Windows reader-thread shutdown race.
                if input_file is None:
                    process.stdin.write(b"K")
                    process.stdin.flush()
                self.assertEqual(client.recv(1), b"K")
                if reason == "disconnect":
                    client.close()
                    client = None
                elif reason == "detach":
                    process.stdin.write(b"\x1d")
                    process.stdin.flush()
                elif reason == "eof":
                    process.stdin.close()
                self.assertEqual(process.wait(timeout=5), 0)
                self.assertFalse(endpoint.exists(), "bridge left a stale endpoint")
            finally:
                if client is not None:
                    client.close()
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=5)
                if process.stdin is not None and not process.stdin.closed:
                    try:
                        process.stdin.close()
                    except BrokenPipeError:
                        pass
                if input_file is not None:
                    input_file.close()
                reader.join(timeout=5)
                process.stdout.close()
                process.stderr.close()

    def test_peer_disconnect_with_stdin_open(self):
        self.check_shutdown("disconnect")

    def test_detach_with_stdin_open(self):
        self.check_shutdown("detach")

    def test_stdin_eof(self):
        self.check_shutdown("eof")

    def test_redirected_file_input(self):
        self.check_shutdown("file")
