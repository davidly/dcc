#!/usr/bin/env python3
"""End-to-end regression test for the full-CP/M DCC MI host."""

import json
import queue
import stat
import subprocess
import sys
import tempfile
import threading
import time
import argparse
from pathlib import Path

HOST_ARGUMENTS = []


class MISession:
    def __init__(self, host, arguments=None):
        self.process = subprocess.Popen(
            [str(host), "--interpreter=mi", *HOST_ARGUMENTS, *(arguments or [])],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lines = queue.Queue()
        self.transcript = []
        self.token = 1
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        self._wait(lambda line: line.strip() == "(gdb)")

    def _read_stdout(self):
        for line in self.process.stdout:
            line = line.rstrip("\r\n")
            self.transcript.append(line)
            self.lines.put(line)

    def _read_stderr(self):
        for line in self.process.stderr:
            self.transcript.append("stderr: " + line.rstrip("\r\n"))

    def _wait(self, predicate, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=deadline - time.time())
            except queue.Empty:
                break
            if predicate(line):
                return line
        raise AssertionError("MI timeout\n" + "\n".join(self.transcript))

    def command(self, command, stop=False):
        token = self.token
        self.token += 1
        self.process.stdin.write(f"{token}{command}\n")
        self.process.stdin.flush()
        result = self._wait(lambda line: line.startswith(f"{token}^"))
        if "^error" in result:
            raise AssertionError(result)
        if stop:
            return result, self._wait(lambda line: line.startswith("*stopped"))
        return result

    def command_error(self, command):
        token = self.token
        self.token += 1
        self.process.stdin.write(f"{token}{command}\n")
        self.process.stdin.flush()
        result = self._wait(lambda line: line.startswith(f"{token}^"))
        assert "^error" in result, result
        return result

    def target_text(self):
        return "".join(
            json.loads(line[1:])
            for line in self.transcript
            if line.startswith('@"')
        )

    def close(self):
        if self.process.poll() is None:
            try:
                self.command("-gdb-exit")
            except (AssertionError, BrokenPipeError):
                self.process.terminate()
        self.process.wait(timeout=5)


class TerminalBridge:
    def __init__(self, script, endpoint):
        self.process = subprocess.Popen(
            [sys.executable, str(script), "--endpoint-file", str(endpoint)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.output = bytearray()
        self.lock = threading.Lock()
        threading.Thread(target=self._read, daemon=True).start()
        self.wait_for(b"DCC debug terminal ready\n")
        if sys.platform != "win32":
            assert stat.S_IMODE(endpoint.stat().st_mode) == 0o600, oct(endpoint.stat().st_mode)

    def _read(self):
        while True:
            data = self.process.stdout.read(1)
            if not data:
                return
            with self.lock:
                self.output.extend(data)

    def wait_for(self, text, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                if text in self.output:
                    return
            if self.process.poll() is not None:
                break
            time.sleep(0.01)
        with self.lock:
            output = bytes(self.output)
        error = self.process.stderr.read().decode(errors="replace") if self.process.poll() is not None else ""
        raise AssertionError(f"terminal bridge timeout: {output!r}\n{error}")

    def send(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)


class DAPSession:
    def __init__(self, adapter):
        self.process = subprocess.Popen(
            [str(adapter)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.messages = queue.Queue()
        self.backlog = []
        self.sequence = 1
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            headers = {}
            while True:
                line = self.process.stdout.readline()
                if not line:
                    return
                if line == b"\r\n":
                    break
                key, value = line.decode().split(":", 1)
                headers[key.lower()] = value.strip()
            size = int(headers["content-length"])
            self.messages.put(json.loads(self.process.stdout.read(size)))

    def send(self, command, arguments=None):
        request = {
            "seq": self.sequence,
            "type": "request",
            "command": command,
            "arguments": arguments or {},
        }
        self.sequence += 1
        body = json.dumps(request).encode()
        header = f"Content-Length: {len(body)}\r\n\r\n".encode()
        self.process.stdin.write(header + body)
        self.process.stdin.flush()
        return request["seq"]

    def wait(self, predicate, timeout=30):
        for index, message in enumerate(self.backlog):
            if predicate(message):
                return self.backlog.pop(index)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                message = self.messages.get(timeout=deadline - time.time())
            except queue.Empty:
                break
            if predicate(message):
                return message
            self.backlog.append(message)
        raise AssertionError("DAP timeout\n" + json.dumps(self.backlog, indent=2))

    def response(self, request):
        return self.wait(
            lambda message: message.get("type") == "response"
            and message.get("request_seq") == request
        )

    def close(self):
        if self.process.poll() is None:
            self.send("disconnect", {"terminateDebuggee": True})
            self.process.terminate()
        self.process.wait(timeout=5)


def write_fixture(root):
    source = root / "fixture.c"
    program = root / "fixture.COM"
    metadata = root / "fixture.DBG"
    far_offset = 15000
    far_address = 0x100 + far_offset
    message_address = far_address + 22
    image = bytearray(17000)
    image[0:3] = bytes((0xC3, far_address & 0xFF, far_address >> 8))
    code = bytes(
        (
            0x11, message_address & 0xFF, message_address >> 8,
            0x0E, 0x09,
            0xCD, 0x05, 0x00,
            0x0E, 0x01,
            0xCD, 0x05, 0x00,
            0x5F,
            0x0E, 0x02,
            0xCD, 0x05, 0x00,
            0xC3, 0x00, 0x00,
        )
    )
    image[far_offset:far_offset + len(code)] = code
    message = b"\r\nREADY>\r\n$"
    image[far_offset + len(code):far_offset + len(code) + len(message)] = message
    program.write_bytes(image)
    source.write_text("int main(void)\n{\n    return 0;\n}\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        f'line 0100 1 "{source.name}"\n'
        f'function-begin {far_address:04X} "main" "main"\n'
        f'line {far_address:04X} 3 "{source.name}"\n'
        f'line {far_address:04X} 4 "{source.name}"\n'
        f'function-end {message_address:04X} "main" "main"\n',
        encoding="ascii",
    )
    return source, program, far_address


def write_rich_fixture(root):
    source = root / "values.c"
    program = root / "values.COM"
    metadata = root / "values.DBG"
    far_offset = 15000
    far_address = 0x100 + far_offset
    bits_address = 0x3D00
    numbers_address = 0x3D10
    vla_address = 0x3D20
    text_address = 0x3D30

    code = bytearray(
        (
            0xDD, 0xE5,                         # push ix
            0xDD, 0x21, 0x00, 0x00,             # ld ix,0
            0xDD, 0x39,                         # add ix,sp
            0x21, 0xE8, 0xFF,                   # ld hl,-24
            0x39,                               # add hl,sp
            0xF9,                               # ld sp,hl
            0xDD, 0x36, 0xFE, 0x07,             # local = 7
            0xDD, 0x36, 0xFF, 0x00,
            0xDD, 0x36, 0x04, 0x05,             # argument = 5
            0xDD, 0x36, 0x05, 0x00,
            0xDD, 0x36, 0xF6, 0x01,             # array = {1,2,3}
            0xDD, 0x36, 0xF7, 0x00,
            0xDD, 0x36, 0xF8, 0x02,
            0xDD, 0x36, 0xF9, 0x00,
            0xDD, 0x36, 0xFA, 0x03,
            0xDD, 0x36, 0xFB, 0x00,
            0xDD, 0xE5, 0xE1,                   # hl = ix
            0x11, 0xF8, 0xFF, 0x19,             # hl = &array[1]
            0xDD, 0x75, 0xF4, 0xDD, 0x74, 0xF5, # pointer
            0x21, vla_address & 0xFF, vla_address >> 8,
            0xDD, 0x75, 0xF2, 0xDD, 0x74, 0xF3, # VLA backing pointer
            0x21, text_address & 0xFF, text_address >> 8,
            0xDD, 0x75, 0xF0, 0xDD, 0x74, 0xF1, # char pointer
        )
    )
    callback_patch = len(code) + 1
    code.extend((0x21, 0x00, 0x00, 0xDD, 0x75, 0xEE, 0xDD, 0x74, 0xEF))
    break_address = far_address + len(code)
    code[callback_patch:callback_patch + 2] = bytes((break_address & 0xFF, break_address >> 8))
    code.append(0x00)
    runner_size = 22
    message_address = far_address + len(code) + runner_size
    code.extend(
        (
            0x11, message_address & 0xFF, message_address >> 8,
            0x0E, 0x09, 0xCD, 0x05, 0x00,
            0x0E, 0x01, 0xCD, 0x05, 0x00,
            0x5F, 0x0E, 0x02, 0xCD, 0x05, 0x00,
            0xC3, 0x00, 0x00,
        )
    )
    assert len(code) == message_address - far_address
    code.extend(b"\r\nVALUES>\r\n$")

    image = bytearray(17000)
    image[0:3] = bytes((0xC3, far_address & 0xFF, far_address >> 8))
    image[far_offset:far_offset + len(code)] = code
    image[bits_address - 0x100:bits_address - 0x100 + 2] = (5 | (14 << 3)).to_bytes(2, "little")
    for index, value in enumerate((10, 20, 30)):
        image[numbers_address - 0x100 + index * 2:numbers_address - 0x100 + index * 2 + 2] = value.to_bytes(2, "little")
    for index, value in enumerate((11, 22, 33)):
        image[vla_address - 0x100 + index * 2:vla_address - 0x100 + index * 2 + 2] = value.to_bytes(2, "little")
    image[text_address - 0x100:text_address - 0x100 + 6] = b"hello\0"
    program.write_bytes(image)
    source.write_text("int main(void)\n{\n    return 0; /* VALUES_BREAK */\n}\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        f'function-begin {far_address:04X} "_main" "main"\n'
        f'line {break_address:04X} 3 "{source.name}"\n'
        f'variable {far_address:04X} "_main" "local" 2 2 -2 2 0 0 2 0 ""\n'
        f'variable {far_address:04X} "_main" "argument" 2 3 4 2 0 0 2 0 ""\n'
        f'variable {far_address:04X} "_main" "array" 2 2 -10 6 1 0 2 0 "3"\n'
        f'variable {far_address:04X} "_main" "pointer" 18 2 -12 2 0 0 2 0 ""\n'
        f'variable {far_address:04X} "_main" "values" 2 2 -14 6 1 1 2 0 "0"\n'
        f'variable {far_address:04X} "_main" "text" 17 2 -16 2 0 0 1 0 ""\n'
        f'variable {far_address:04X} "_main" "callback" 18 2 -18 2 0 0 2 1 ""\n'
        f'global {bits_address:04X} "_bits" "bits" 1920 2 0 0 2 0 ""\n'
        f'global {numbers_address:04X} "_numbers" "numbers" 2 6 1 0 2 0 "3"\n'
        'struct 7 2 0 "Bits"\n'
        'field 7 "low" 34 0 2 0 2 3 0 ""\n'
        'field 7 "high" 2 0 2 0 2 4 3 ""\n'
        f'function-end {message_address:04X} "_main" "main"\n',
        encoding="ascii",
    )
    return source, program, break_address


def evaluate(session, expression):
    result = session.command(f'-data-evaluate-expression "{expression}"')
    marker = 'value="'
    assert marker in result, result
    return result.split(marker, 1)[1].split('"', 1)[0]


def write_loop_fixture(root):
    source = root / "loop.c"
    program = root / "loop.COM"
    metadata = root / "loop.DBG"
    address = 0x0200
    image = bytearray(512)
    image[0:3] = bytes((0xC3, address & 0xFF, address >> 8))
    offset = address - 0x100
    loop_address = address + 8
    message_address = address + 11
    image[offset:offset + 11] = bytes(
        (
            0x11, message_address & 0xFF, message_address >> 8,
            0x0E, 0x09, 0xCD, 0x05, 0x00,
            0xC3, loop_address & 0xFF, loop_address >> 8,
        )
    )
    image[offset + 11:offset + 20] = b"#LOOP#\r\n$"
    program.write_bytes(image)
    source.write_text("int main(void) { for (;;) {} }\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        f'function-begin {address:04X} "_main" "main"\n'
        f'line {address:04X} 1 "{source.name}"\n'
        f'function-end {message_address:04X} "_main" "main"\n',
        encoding="ascii",
    )
    return program


def write_bdos_exit_fixture(root):
    source = root / "bdosexit.c"
    program = root / "bdosexit.COM"
    metadata = root / "bdosexit.DBG"
    code = bytes((0x0E, 0x00, 0xCD, 0x05, 0x00, 0xC3, 0x05, 0x01))
    program.write_bytes(code)
    source.write_text("int main(void) { return 0; }\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        'function-begin 0100 "_main" "main"\n'
        f'line 0100 1 "{source.name}"\n'
        'function-end 0108 "_main" "main"\n',
        encoding="ascii",
    )
    return program


def write_refresh_fixture(root):
    source = root / "refresh.c"
    program = root / "refresh.COM"
    metadata = root / "refresh.DBG"
    program.write_bytes(bytes((0xED, 0x5F, 0x00, 0xC3, 0x00, 0x00)))  # LD A,R; NOP; JP 0
    source.write_text("int main(void) { return 0; }\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        'function-begin 0100 "_main" "main"\n'
        f'line 0102 1 "{source.name}"\n'
        'function-end 0106 "_main" "main"\n',
        encoding="ascii",
    )
    return program


def write_poll_fixture(root):
    source = root / "poll.c"
    program = root / "poll.COM"
    metadata = root / "poll.DBG"
    address = 0x3B00
    loop_address = address + 8
    breakpoint_address = address + 13
    message_address = address + 28
    code = bytes(
        (
            0x11, message_address & 0xFF, message_address >> 8,
            0x0E, 0x09, 0xCD, 0x05, 0x00,
            0x11, 0xFF, 0x00,
            0x0E, 0x06,
            0xCD, 0x05, 0x00,
            0xB7,
            0x28, (loop_address - (address + 19)) & 0xFF,
            0x5F,
            0x0E, 0x02,
            0xCD, 0x05, 0x00,
            0xC3, 0x00, 0x00,
        )
    )
    image = bytearray(17000)
    image[0:3] = bytes((0xC3, address & 0xFF, address >> 8))
    offset = address - 0x100
    image[offset:offset + len(code)] = code
    image[offset + len(code):offset + len(code) + 5] = b"\r\nA>$"
    program.write_bytes(image)
    source.write_text(
        "int main(void)\n{\n    do {\n        character = bdos(6, 0xff); /* POLL_BREAK */\n"
        "    } while (character == 0);\n}\n",
        encoding="ascii",
    )
    metadata.write_text(
        "DCCDBG 2\n"
        f'function-begin {address:04X} "_main" "main"\n'
        f'line {breakpoint_address:04X} 1 "{source.name}"\n'
        f'line {breakpoint_address:04X} 2 "{source.name}"\n'
        f'line {breakpoint_address:04X} 3 "{source.name}"\n'
        f'line {breakpoint_address:04X} 4 "{source.name}"\n'
        f'function-end {message_address:04X} "_main" "main"\n',
        encoding="ascii",
    )
    return source, program


def write_automatic_fixture_program(root, fixture_name="AUTO.DAT", automatic=True):
    root.mkdir(parents=True, exist_ok=True)
    source = root / "autofix.c"
    program = root / "AUTOFIX.COM"
    metadata = root / "AUTOFIX.DBG"
    if automatic:
        fixtures = root / "fixtures"
        fixtures.mkdir()
        (fixtures / fixture_name).write_bytes(b"fixture-data")

    image = bytearray(0x44)
    image[:0x1F] = bytes(
        (
            0x11, 0x20, 0x01,       # LD DE,0120h (FCB)
            0x0E, 0x0F,             # LD C,15 (open)
            0xCD, 0x05, 0x00,       # CALL BDOS
            0x3C,                   # INC A (FFh failure becomes zero)
            0x28, 0x0A,             # JR Z,failed
            0x1E, ord("Y"),         # success marker
            0x0E, 0x02,
            0xCD, 0x05, 0x00,
            0xC3, 0x00, 0x00,
            0x1E, ord("N"),         # failure marker
            0x0E, 0x02,
            0xCD, 0x05, 0x00,
            0xC3, 0x00, 0x00,
        )
    )
    base, extension = fixture_name.split(".", 1)
    image[0x20:0x2C] = f"\x00{base:<8}{extension:<3}".encode("ascii")
    program.write_bytes(image)
    source.write_text("int main(void) { return 0; }\n", encoding="ascii")
    metadata.write_text(
        "DCCDBG 2\n"
        f'function-begin 0100 "_main" "main"\n'
        f'line 0100 1 "{source.name}"\n'
        f'function-end 0120 "_main" "main"\n',
        encoding="ascii",
    )
    return program


def run_test(host):
    with tempfile.TemporaryDirectory(prefix="dcc-debug-host-test-") as directory:
        source, program, far_address = write_fixture(Path(directory))
        session = MISession(host)
        try:
            session.command(f'-file-exec-and-symbols "{program}"')
            entry = session.command("-break-insert -t *0x100")
            assert 'addr="0x100"' in entry, entry
            source_break = session.command(f'-break-insert -f "{source}" -l 3')
            assert f'addr="0x{far_address:x}"' in source_break, source_break

            _, stopped = session.command("-exec-run", stop=True)
            assert 'reason="breakpoint-hit"' in stopped and 'bkptno="1"' in stopped, stopped
            _, stopped = session.command("-exec-continue", stop=True)
            assert 'reason="breakpoint-hit"' in stopped and 'bkptno="2"' in stopped, stopped
            assert 'line="3"' in stopped and 'func="main"' in stopped, stopped
            assert 'line="3"' in session.command("-file-list-exec-source-file")
            assert 'line="3"' in session.command("-stack-info-frame")
            assert 'line="3"' in session.command("-stack-list-frames")

            session.command("-break-delete 2")
            _, stopped = session.command('-interpreter-exec console "input K"', stop=True)
            assert 'reason="exited-normally"' in stopped, stopped
            output = session.target_text()
            assert "READY>" in output, output
            assert output.count("K") >= 2, output
        finally:
            session.close()

        automatic_program = write_automatic_fixture_program(
            Path(directory) / "automatic-fixtures"
        )
        session = MISession(host)
        try:
            session.command(f'-file-exec-and-symbols "{automatic_program}"')
            _, stopped = session.command("-exec-run", stop=True)
            assert 'reason="exited-normally"' in stopped, stopped
            output = session.target_text()
            assert "Y" in output and "N" not in output, output
        finally:
            session.close()

        saved_fixtures = Path(directory) / "saved-fixtures"
        saved_fixtures.mkdir()
        (saved_fixtures / "STALE.DAT").write_bytes(b"stale")
        session = MISession(host, ["--save-fixtures", str(saved_fixtures)])
        try:
            session.command(f'-file-exec-and-symbols "{automatic_program}"')
            _, stopped = session.command("-exec-run", stop=True)
            assert 'reason="exited-normally"' in stopped, stopped
        finally:
            session.close()
        assert [path.name for path in saved_fixtures.iterdir()] == ["AUTO.DAT"]
        saved_auto = (saved_fixtures / "AUTO.DAT").read_bytes()
        assert saved_auto.startswith(b"fixture-data") and len(saved_auto) == 128

        explicit_root = Path(directory) / "explicit-fixtures"
        explicit_program = write_automatic_fixture_program(
            explicit_root / "program", automatic=False
        )
        binary_fixture = explicit_root / "binary" / "AUTO.DAT"
        binary_fixture.parent.mkdir(parents=True)
        binary_fixture.write_bytes(b"binary\x00fixture")
        binary_saved = explicit_root / "binary-saved"
        session = MISession(
            host,
            ["--fixture", str(binary_fixture), "--save-fixtures", str(binary_saved)],
        )
        try:
            session.command(f'-file-exec-and-symbols "{explicit_program}"')
            _, stopped = session.command("-exec-run", stop=True)
            assert 'reason="exited-normally"' in stopped, stopped
            assert "Y" in session.target_text(), session.target_text()
        finally:
            session.close()
        saved_binary = (binary_saved / "AUTO.DAT").read_bytes()
        assert saved_binary.startswith(b"binary\x00fixture") and len(saved_binary) == 128

        text_fixture = explicit_root / "text" / "AUTO.DAT"
        text_fixture.parent.mkdir(parents=True)
        text_fixture.write_bytes(b"line one\nline two\r\n")
        text_saved = explicit_root / "text-saved"
        session = MISession(
            host,
            ["--text-fixture", str(text_fixture), "--save-fixtures", str(text_saved)],
        )
        try:
            session.command(f'-file-exec-and-symbols "{explicit_program}"')
            _, stopped = session.command("-exec-run", stop=True)
            assert 'reason="exited-normally"' in stopped, stopped
            assert "Y" in session.target_text(), session.target_text()
        finally:
            session.close()
        saved_text = (text_saved / "AUTO.DAT").read_bytes()
        assert saved_text.startswith(b"line one\r\nline two\r\n\x1a")

        duplicate_fixture = explicit_root / "duplicate" / "auto.dat"
        duplicate_fixture.parent.mkdir(parents=True)
        duplicate_fixture.write_bytes(b"duplicate")
        session = MISession(
            host,
            ["--fixture", str(binary_fixture), "--fixture", str(duplicate_fixture)],
        )
        try:
            session.command(f'-file-exec-and-symbols "{explicit_program}"')
            error = session.command_error("-exec-run")
            assert "duplicate CP/M filename" in error, error
        finally:
            session.close()

        session = MISession(host, ["--fixture", str(explicit_root / "MISSING.DAT")])
        try:
            session.command(f'-file-exec-and-symbols "{explicit_program}"')
            error = session.command_error("-exec-run")
            assert "cannot read host file" in error, error
        finally:
            session.close()

        invalid_fixture = explicit_root / "TOO-LONG-NAME.DAT"
        invalid_fixture.write_bytes(b"invalid")
        session = MISession(host, ["--fixture", str(invalid_fixture)])
        try:
            session.command(f'-file-exec-and-symbols "{explicit_program}"')
            error = session.command_error("-exec-run")
            assert "not CP/M 8.3-compatible" in error, error
        finally:
            session.close()

        bdos_exit_program = write_bdos_exit_fixture(Path(directory))
        session = MISession(host)
        try:
            session.command(f'-file-exec-and-symbols "{bdos_exit_program}"')
            _, exited = session.command("-exec-run", stop=True)
            assert 'reason="exited-normally"' in exited, exited
        finally:
            session.close()

        refresh_program = write_refresh_fixture(Path(directory))
        session = MISession(host)
        try:
            session.command(f'-file-exec-and-symbols "{refresh_program}"')
            session.command("-break-insert *0x0102")
            session.command("-exec-run", stop=True)
            registers = session.command("-data-list-register-values x")
            af = next(item for item in registers.split("{") if 'number="0"' in item)
            refresh = next(item for item in registers.split("{") if 'number="13"' in item)
            accumulator = int(af.split('value="0x', 1)[1].split('"', 1)[0], 16) >> 8
            refresh_value = int(refresh.split('value="0x', 1)[1].split('"', 1)[0], 16)
            assert refresh_value != 0, registers
            assert (refresh_value & 0x7f) == ((accumulator + 1) & 0x7f), registers
        finally:
            session.close()

        cancelled_endpoint = Path(directory) / "cancelled.endpoint"
        cancelled_bridge = TerminalBridge(
            Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", cancelled_endpoint
        )
        assert cancelled_endpoint.exists(), cancelled_endpoint
        cancelled_bridge.close()
        assert not cancelled_endpoint.exists(), cancelled_endpoint

        endpoint = Path(directory) / "terminal.endpoint"
        endpoint.write_text("127.0.0.1 1 stale-token\n", encoding="ascii")
        bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
        session = MISession(host, ["--terminal-endpoint-file", str(endpoint)])
        try:
            bridge.wait_for(b"DCC target terminal connected")
            session.command(f'-file-exec-and-symbols "{program}"')
            running = session.command("-exec-run")
            assert "^running" in running, running
            bridge.wait_for(b"READY>")
            assert "READY>" not in session.target_text(), session.target_text()
            bridge.send(b"K")
            exited = session._wait(lambda line: 'reason="exited-normally"' in line)
            assert exited.startswith("*stopped"), exited
            bridge.wait_for(b"KK")
        finally:
            session.close()
            bridge.close()
        assert not endpoint.exists(), endpoint

        poll_source, poll_program = write_poll_fixture(Path(directory))
        if "--io-adapter" in HOST_ARGUMENTS:
            endpoint = Path(directory) / "terminal-cursor.endpoint"
            bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
            session = MISession(host, ["--terminal-endpoint-file", str(endpoint)])
            try:
                bridge.wait_for(b"DCC target terminal connected")
                session.command(f'-file-exec-and-symbols "{poll_program}"')
                assert "^running" in session.command("-exec-run")
                bridge.wait_for(b"A>")
                bridge.send(b"\x1b[D")
                exited = session._wait(lambda line: 'reason="exited-normally"' in line)
                assert exited.startswith("*stopped"), exited
                bridge.wait_for(b"\x13")
            finally:
                session.close()
                bridge.close()
            assert not endpoint.exists(), endpoint
        else:
            endpoint = Path(directory) / "terminal-raw.endpoint"
            bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
            session = MISession(host, ["--terminal-endpoint-file", str(endpoint)])
            try:
                bridge.wait_for(b"DCC target terminal connected")
                session.command(f'-file-exec-and-symbols "{poll_program}"')
                assert "^running" in session.command("-exec-run")
                bridge.wait_for(b"A>")
                bridge.send(b"\x1b[D")
                exited = session._wait(lambda line: 'reason="exited-normally"' in line)
                assert exited.startswith("*stopped"), exited
                bridge.wait_for(b"\x1b")
            finally:
                session.close()
                bridge.close()
            assert not endpoint.exists(), endpoint

        endpoint = Path(directory) / "terminal-control.endpoint"
        bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
        session = MISession(host, ["--terminal-endpoint-file", str(endpoint)])
        try:
            bridge.wait_for(b"DCC target terminal connected")
            session.command(f'-file-exec-and-symbols "{poll_program}"')
            assert "^running" in session.command("-exec-run")
            bridge.wait_for(b"A>")
            bridge.send(b"\x03")
            exited = session._wait(lambda line: 'reason="exited-normally"' in line)
            assert exited.startswith("*stopped"), exited
            bridge.wait_for(b"\x03")
        finally:
            session.close()
            bridge.close()
        assert not endpoint.exists(), endpoint

        endpoint = Path(directory) / "terminal-fallback.endpoint"
        bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
        session = MISession(host, ["--terminal-endpoint-file", str(endpoint)])
        try:
            bridge.wait_for(b"DCC target terminal connected")
            bridge.send(b"\x1d")
            bridge.process.wait(timeout=5)
            session.command(f'-file-exec-and-symbols "{program}"')
            _, waiting = session.command("-exec-run", stop=True)
            assert 'reason="end-stepping-range"' in waiting, waiting
            assert "READY>" in session.target_text(), session.target_text()
            _, exited = session.command('-interpreter-exec console "input K"', stop=True)
            assert 'reason="exited-normally"' in exited, exited
        finally:
            session.close()
            bridge.close()
        assert not endpoint.exists(), endpoint

        loop_program = write_loop_fixture(Path(directory))
        aborted_fixtures = Path(directory) / "aborted-fixtures"
        aborted_fixtures.mkdir()
        (aborted_fixtures / "KEEP.DAT").write_bytes(b"keep")
        session = MISession(host, ["--save-fixtures", str(aborted_fixtures)])
        try:
            session.command(f'-file-exec-and-symbols "{loop_program}"')
            running = session.command("-exec-run")
            assert "^running" in running, running
            _, interrupted = session.command("-exec-interrupt", stop=True)
            assert 'reason="end-stepping-range"' in interrupted, interrupted
            assert "signal-name" not in interrupted, interrupted
            assert "^running" in session.command("-exec-continue")
            assert "^exit" in session.command("-gdb-exit")
            session.process.wait(timeout=5)
        finally:
            session.close()
        assert [path.name for path in aborted_fixtures.iterdir()] == ["KEEP.DAT"]
        assert (aborted_fixtures / "KEEP.DAT").read_bytes() == b"keep"

        source, program, break_address = write_rich_fixture(Path(directory))
        session = MISession(host)
        try:
            session.command(f'-file-exec-and-symbols "{program}"')
            false_break = session.command(f'-break-insert -c"local == 8" "{source}:3"')
            true_break = session.command(f'-break-insert -c"local == 7" "{source}:3"')
            session.command_error(f'-break-insert -c"" "{source}:3"')
            session.command_error(f'-break-insert -i nope "{source}:3"')
            session.command_error(f'-break-insert -i -1 "{source}:3"')
            session.command_error("-break-insert *0x10000")
            session.command_error(f'-break-insert "{source}:3junk"')
            false_number = int(false_break.split('number="', 1)[1].split('"', 1)[0])
            true_number = int(true_break.split('number="', 1)[1].split('"', 1)[0])
            session.command_error(f"-break-delete {true_number} junk")
            assert f'number="{true_number}"' in session.command("-break-list")
            _, stopped = session.command("-exec-run", stop=True)
            assert f'bkptno="{true_number}"' in stopped, stopped
            assert f'addr="0x{break_address:x}"' in stopped, stopped
            source_files = session.command("-file-list-exec-source-files")
            assert f'file="{source.name}"' in source_files, source_files
            current_source = session.command("-file-list-exec-source-file")
            assert 'line="3"' in current_source and f'file="{source.name}"' in current_source

            locals_result = session.command("-stack-list-locals 1")
            session.command_error("-stack-list-locals --frame nope 1")
            session.command_error("-stack-list-locals --frame 999 1")
            for name in ("local", "argument", "array", "pointer", "values", "text", "callback"):
                assert f'name="{name}"' in locals_result, locals_result
            assert 'name="argument",value="5",type="int",arg="1"' in locals_result
            arguments = session.command("-stack-list-arguments 1 0 0")
            assert 'name="argument",value="5"' in arguments, arguments

            assert evaluate(session, "local + argument * 2") == "17"
            assert evaluate(session, "array[2]") == "3"
            assert evaluate(session, "*pointer") == "2"
            assert evaluate(session, "values[2]") == "33"
            assert evaluate(session, "numbers[1]") == "20"
            assert evaluate(session, "bits.low") == "5"
            assert evaluate(session, "bits.high") == "-2"
            assert evaluate(session, "text") == "0x3d30 'hello'"
            assert evaluate(session, "sizeof(array)") == "6"
            assert evaluate(session, "$pc") == f"0x{break_address:04x}"

            array_object = session.command('-var-create - * "array"')
            array_name = array_object.split('name="', 1)[1].split('"', 1)[0]
            assert 'numchild="3"' in array_object and 'type="int[3]"' in array_object
            children = session.command(f"-var-list-children --all-values {array_name}")
            assert children.count("child={") == 3 and 'value="2"' in children, children
            bits_object = session.command('-var-create - * "bits"')
            bits_name = bits_object.split('name="', 1)[1].split('"', 1)[0]
            bits_children = session.command(f"-var-list-children --all-values {bits_name}")
            assert 'exp="low"' in bits_children and 'exp="high"' in bits_children
            local_object = session.command('-var-create - * "local"')
            local_name = local_object.split('name="', 1)[1].split('"', 1)[0]
            assert 'type="int"' in session.command(f"-var-info-type {local_name}")
            assert 'attr="editable"' in session.command(f"-var-show-attributes {local_name}")
            assert 'value="8"' in session.command(f'-var-assign {local_name} "8"')
            assert evaluate(session, "local") == "8"
            assert local_name in session.command("-var-update --all-values *")

            ix = int(evaluate(session, "$ix"), 16)
            session.command(f"-data-write-memory-bytes 0x{(ix - 2) & 0xffff:04x} 0900")
            assert evaluate(session, "local") == "9"
            memory_result = session.command(f"-data-read-memory-bytes -o -0x2 0x{ix:04x} 2")
            assert 'contents="0900"' in memory_result, memory_result
            session.command_error("-data-read-memory-bytes -o -1 0x0 2")
            session.command_error("-data-read-memory-bytes 0xffff 2")
            session.command_error("-data-read-memory-bytes 0x10zz 1")
            session.command_error("-data-write-memory-bytes 0xffff aabb")
            session.command_error("-data-write-memory-bytes 0x10zz aa")
            session.command_error("-data-write-memory-bytes 0x100 gg")
            session.command_error(f"-data-write-memory-bytes 0x{(ix - 2) & 0xffff:04x} aaZZ")
            unchanged = session.command(f"-data-read-memory-bytes 0x{(ix - 2) & 0xffff:04x} 2")
            assert 'contents="0900"' in unchanged, unchanged
            session.command("-data-write-memory-bytes 0xffff dd")
            boundary = session.command("-data-disassemble -s 0xffff -e 0x10000 -- 0")
            assert boundary.count("address=") == 1 and 'opcodes="dd"' in boundary, boundary
            assert 'inst="db ddh"' in boundary, boundary
            session.command_error("-data-disassemble -s 0x100 -e 0x110 -- nope")
            session.command_error("-data-disassemble -s 0x100 -e 0x110")
            session.command_error("-data-disassemble -s invalid -e 0x110 -- 0")
            session.command_error("-data-disassemble -s 0x100 -e invalid -- 0")

            breakpoints = session.command("-break-list")
            assert breakpoints.count('times="1"') == 2, breakpoints
            session.command(f"-break-disable {true_number}")
            breakpoints = session.command("-break-list")
            entry = next(item for item in breakpoints.split("bkpt={") if f'number="{true_number}"' in item)
            assert 'enabled="n"' in entry, breakpoints
            session.command(f"-break-enable {true_number}")
            session.command(f"-break-delete {false_number} {true_number}")
            breakpoints = session.command("-break-list")
            assert 'nr_rows="0"' in breakpoints, breakpoints

            _, stepped = session.command("-exec-step-instruction", stop=True)
            assert 'reason="end-stepping-range"' in stepped, stepped
            _, until = session.command(f"-exec-until *0x{break_address + 9:04x}", stop=True)
            assert 'reason="breakpoint-hit"' in until, until
            _, waiting = session.command("-exec-continue", stop=True)
            assert 'reason="end-stepping-range"' in waiting, waiting
            _, exited = session.command('-interpreter-exec console "input K"', stop=True)
            assert 'reason="exited-normally"' in exited, exited
        finally:
            session.close()


def run_adapter_test(adapter, host):
    with tempfile.TemporaryDirectory(prefix="dcc-debug-host-dap-") as directory:
        source, program, _ = write_rich_fixture(Path(directory))
        session = DAPSession(adapter)
        try:
            request = session.send(
                "initialize",
                {
                    "adapterID": "cppdbg",
                    "linesStartAt1": True,
                    "columnsStartAt1": True,
                    "pathFormat": "path",
                },
            )
            assert session.response(request)["success"]
            launch = session.send(
                "launch",
                {
                    "name": "dcc-debug-host-test",
                    "type": "cppdbg",
                    "request": "launch",
                    "program": str(program),
                    "cwd": str(program.parent),
                    "MIMode": "gdb",
                    "miDebuggerPath": str(host),
                    "miDebuggerArgs": "--interpreter=mi",
                    "targetArchitecture": "x86",
                    "stopAtEntry": False,
                    "externalConsole": False,
                },
            )
            session.wait(lambda message: message.get("event") == "initialized")
            request = session.send(
                "setBreakpoints",
                {
                    "source": {"path": str(source)},
                    "breakpoints": [{"line": 3}],
                    "sourceModified": False,
                },
            )
            breakpoint_response = session.response(request)
            assert breakpoint_response["success"], breakpoint_response
            assert breakpoint_response["body"]["breakpoints"][0]["verified"], (
                breakpoint_response,
                session.backlog,
            )
            request = session.send("configurationDone")
            assert session.response(request)["success"]
            assert session.response(launch)["success"]

            stopped = session.wait(lambda message: message.get("event") == "stopped")
            request = session.send("stackTrace", {"threadId": stopped["body"]["threadId"]})
            stack = session.response(request)
            assert stack["success"] and stack["body"]["stackFrames"], (stack, session.backlog)
            frame = stack["body"]["stackFrames"][0]
            assert frame["line"] == 3 and Path(frame["source"]["path"]) == source, frame

            request = session.send("scopes", {"frameId": frame["id"]})
            scopes = session.response(request)
            assert scopes["success"] and scopes["body"]["scopes"], scopes
            local_scope = next(scope for scope in scopes["body"]["scopes"] if scope["name"] == "Locals")
            request = session.send("variables", {"variablesReference": local_scope["variablesReference"]})
            variables = session.response(request)
            assert variables["success"], variables
            by_name = {variable["name"]: variable for variable in variables["body"]["variables"]}
            assert by_name["local"]["value"] == "7", by_name
            assert by_name["argument"]["value"] == "5", by_name
            assert by_name["array"]["variablesReference"] != 0, by_name

            request = session.send(
                "variables", {"variablesReference": by_name["array"]["variablesReference"]}
            )
            children = session.response(request)
            assert children["success"], children
            assert [child["value"] for child in children["body"]["variables"]] == ["1", "2", "3"]

            request = session.send(
                "evaluate",
                {"expression": "bits.high", "context": "watch", "frameId": frame["id"]},
            )
            evaluated = session.response(request)
            assert evaluated["success"] and evaluated["body"]["result"] == "-2", evaluated

            request = session.send(
                "setVariable",
                {
                    "variablesReference": local_scope["variablesReference"],
                    "name": "local",
                    "value": "8",
                },
            )
            assigned = session.response(request)
            assert assigned["success"] and assigned["body"]["value"] == "8", assigned

            request = session.send(
                "disassemble",
                {
                    "memoryReference": frame["instructionPointerReference"],
                    "instructionOffset": 0,
                    "instructionCount": 4,
                    "resolveSymbols": True,
                },
            )
            disassembly = session.response(request)
            assert disassembly["success"] and len(disassembly["body"]["instructions"]) == 4, disassembly

            request = session.send(
                "next",
                {"threadId": stopped["body"]["threadId"], "granularity": "instruction"},
            )
            assert session.response(request)["success"]
            stopped = session.wait(lambda message: message.get("event") == "stopped")

            request = session.send("continue", {"threadId": stopped["body"]["threadId"]})
            assert session.response(request)["success"]
            stopped = session.wait(lambda message: message.get("event") == "stopped")
            request = session.send("stackTrace", {"threadId": stopped["body"]["threadId"]})
            waiting_stack = session.response(request)
            assert waiting_stack["success"] and waiting_stack["body"]["stackFrames"], waiting_stack
            waiting_frame = waiting_stack["body"]["stackFrames"][0]

            request = session.send(
                "evaluate",
                {
                    "expression": "-exec input K",
                    "context": "repl",
                    "frameId": waiting_frame["id"],
                },
            )
            assert session.response(request)["success"]
            finished = session.wait(
                lambda message: message.get("event") in ("exited", "terminated")
            )
            assert finished["event"] in ("exited", "terminated"), finished
        finally:
            session.close()

        terminal_source, terminal_program = write_poll_fixture(Path(directory))
        endpoint = Path(directory) / "dap-terminal.endpoint"
        bridge = TerminalBridge(Path(__file__).resolve().parents[1] / "dcc_host_terminal_bridge.py", endpoint)
        session = DAPSession(adapter)
        try:
            request = session.send(
                "initialize",
                {
                    "adapterID": "cppdbg",
                    "linesStartAt1": True,
                    "columnsStartAt1": True,
                    "pathFormat": "path",
                },
            )
            assert session.response(request)["success"]
            launch = session.send(
                "launch",
                {
                    "name": "dcc-debug-host-terminal-test",
                    "type": "cppdbg",
                    "request": "launch",
                    "program": str(terminal_program),
                    "cwd": str(terminal_source.parent),
                    "MIMode": "gdb",
                    "miDebuggerPath": str(host),
                    "miDebuggerArgs": f'--interpreter=mi --terminal-endpoint-file "{endpoint}"',
                    "targetArchitecture": "x86",
                    "stopAtEntry": False,
                    "externalConsole": False,
                },
            )
            session.wait(lambda message: message.get("event") == "initialized")
            request = session.send("configurationDone")
            assert session.response(request)["success"]
            assert session.response(launch)["success"]
            bridge.wait_for(b"A>")
            request = session.send(
                "setBreakpoints",
                {
                    "source": {"path": str(terminal_source)},
                    "breakpoints": [{"line": 4}],
                    "sourceModified": False,
                },
            )
            breakpoint_response = session.response(request)
            assert breakpoint_response["success"], breakpoint_response
            assert breakpoint_response["body"]["breakpoints"][0]["verified"], breakpoint_response
            assert breakpoint_response["body"]["breakpoints"][0]["line"] == 4, breakpoint_response
            stopped = session.wait(lambda message: message.get("event") == "stopped")
            assert stopped["body"]["reason"] in ("step", "pause"), stopped
            request = session.send("continue", {"threadId": stopped["body"]["threadId"]})
            assert session.response(request)["success"]
            stopped = session.wait(lambda message: message.get("event") == "stopped")
            assert stopped["body"]["reason"] == "breakpoint", stopped
            assert stopped["body"]["line"] == 4, stopped
            request = session.send(
                "setBreakpoints",
                {
                    "source": {"path": str(terminal_source)},
                    "breakpoints": [{"line": 3}],
                    "sourceModified": False,
                },
            )
            replaced = session.response(request)
            assert replaced["success"] and replaced["body"]["breakpoints"][0]["verified"], replaced
            request = session.send(
                "setBreakpoints",
                {
                    "source": {"path": str(terminal_source)},
                    "breakpoints": [],
                    "sourceModified": False,
                },
            )
            assert session.response(request)["success"]
            bridge.send(b"K")
            request = session.send("continue", {"threadId": stopped["body"]["threadId"]})
            assert session.response(request)["success"]
            finished = session.wait(
                lambda message: message.get("event") in ("exited", "terminated")
            )
            assert finished["event"] in ("exited", "terminated"), finished
            bridge.wait_for(b"K")
            dap_stdout = "".join(
                message.get("body", {}).get("output", "")
                for message in session.backlog
                if message.get("event") == "output"
            )
            assert "A>" not in dap_stdout, dap_stdout
        finally:
            session.close()
            bridge.close()
        assert not endpoint.exists(), endpoint

        loop_program = write_loop_fixture(Path(directory))
        session = DAPSession(adapter)
        try:
            request = session.send(
                "initialize",
                {
                    "adapterID": "cppdbg",
                    "linesStartAt1": True,
                    "columnsStartAt1": True,
                    "pathFormat": "path",
                },
            )
            assert session.response(request)["success"]
            launch = session.send(
                "launch",
                {
                    "name": "dcc-debug-host-pause-test",
                    "type": "cppdbg",
                    "request": "launch",
                    "program": str(loop_program),
                    "cwd": str(loop_program.parent),
                    "MIMode": "gdb",
                    "miDebuggerPath": str(host),
                    "miDebuggerArgs": "--interpreter=mi",
                    "targetArchitecture": "x86",
                    "stopAtEntry": False,
                    "externalConsole": False,
                },
            )
            session.wait(lambda message: message.get("event") == "initialized")
            request = session.send("configurationDone")
            assert session.response(request)["success"]
            assert session.response(launch)["success"]
            session.wait(
                lambda message: message.get("event") == "output"
                and message.get("body", {}).get("category") == "stdout"
                and message.get("body", {}).get("output") == "#"
            )
            thread = session.wait(
                lambda message: message.get("event") == "thread"
                and message.get("body", {}).get("reason") == "started"
            )
            request = session.send("pause", {"threadId": thread["body"]["threadId"]})
            pause = session.response(request)
            assert pause["success"], pause
            stopped = session.wait(lambda message: message.get("event") == "stopped")
            assert stopped["body"]["reason"] in ("pause", "step"), stopped
        finally:
            session.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("debug_host", type=Path)
    parser.add_argument("open_debug_ad7", type=Path, nargs="?")
    parser.add_argument("--io-adapter", type=Path)
    options = parser.parse_args()
    debug_host = options.debug_host.resolve()
    if options.io_adapter:
        HOST_ARGUMENTS.extend(["--io-adapter", str(options.io_adapter.resolve())])
    run_test(debug_host)
    if options.open_debug_ad7:
        run_adapter_test(options.open_debug_ad7.resolve(), debug_host)
    print("DCC debugger MI regression passed")