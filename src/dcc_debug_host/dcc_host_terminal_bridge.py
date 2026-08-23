#!/usr/bin/env python3
"""Bridge a VS Code integrated terminal to dcc-debug-host target I/O."""

import argparse
import os
import secrets
import select
import signal
import socket
import sys
import tempfile
from pathlib import Path


def publish_endpoint(path, port, token):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = f"127.0.0.1 {port} {token}\n"
    handle, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        os.fchmod(handle, 0o600)
        with os.fdopen(handle, "w", encoding="ascii") as output:
            output.write(descriptor)
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def remove_endpoint(path, token):
    try:
        if path.read_text(encoding="ascii").split()[-1] == token:
            path.unlink()
    except (FileNotFoundError, IndexError, OSError):
        pass


def authenticate(client, token):
    client.settimeout(5)
    received = bytearray()
    while len(received) <= 128 and not received.endswith(b"\n"):
        chunk = client.recv(1)
        if not chunk:
            return False
        received.extend(chunk)
    try:
        return received.rstrip(b"\r\n").decode("ascii", errors="strict") == token
    except UnicodeDecodeError:
        return False


def bridge_posix(client):
    import termios
    import tty

    input_fd = sys.stdin.fileno()
    output_fd = sys.stdout.fileno()
    saved = termios.tcgetattr(input_fd) if os.isatty(input_fd) else None
    if saved is not None:
        tty.setraw(input_fd)
    client.setblocking(False)
    try:
        while True:
            readable, _, _ = select.select((client, input_fd), (), (), 0.1)
            if client in readable:
                data = client.recv(4096)
                if not data:
                    return
                os.write(output_fd, data)
            if input_fd in readable:
                data = os.read(input_fd, 4096)
                if not data:
                    return
                detach = data.find(b"\x1d")
                if detach >= 0:
                    if detach:
                        client.sendall(data[:detach])
                    return
                client.sendall(data)
    finally:
        if saved is not None:
            termios.tcsetattr(input_fd, termios.TCSADRAIN, saved)


def bridge_windows(client):
    import msvcrt
    import time

    extended_keys = {
        "H": b"\x1b[A",
        "P": b"\x1b[B",
        "M": b"\x1b[C",
        "K": b"\x1b[D",
        "R": b"\x1b[2~",
        "S": b"\x1b[3~",
        "I": b"\x1b[5~",
        "Q": b"\x1b[6~",
    }
    client.setblocking(False)
    output_fd = sys.stdout.fileno()
    while True:
        readable, _, _ = select.select((client,), (), (), 0.05)
        if readable:
            data = client.recv(4096)
            if not data:
                return
            os.write(output_fd, data)
        if msvcrt.kbhit():
            character = msvcrt.getwch()
            if character == "\x1d":
                return
            if character in ("\x00", "\xe0"):
                sequence = extended_keys.get(msvcrt.getwch())
                if sequence:
                    client.sendall(sequence)
                continue
            try:
                client.sendall(character.encode("ascii"))
            except UnicodeEncodeError:
                continue
        time.sleep(0.005)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint-file", required=True, type=Path)
    arguments = parser.parse_args()
    endpoint = arguments.endpoint_file.resolve()
    token = secrets.token_hex(16)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    publish_endpoint(endpoint, server.getsockname()[1], token)

    def stop_bridge(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, stop_bridge)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, stop_bridge)

    print("DCC debug terminal starting", flush=True)
    print("DCC debug terminal ready", flush=True)
    try:
        client, _ = server.accept()
        with client:
            if not authenticate(client, token):
                raise RuntimeError("target terminal authentication failed")
            print("DCC target terminal connected (Ctrl+] detaches).", flush=True)
            if os.name == "nt":
                bridge_windows(client)
            else:
                bridge_posix(client)
    except KeyboardInterrupt:
        pass
    except (BrokenPipeError, ConnectionError, OSError) as error:
        print(f"DCC target terminal disconnected: {error}", file=sys.stderr)
    finally:
        server.close()
        remove_endpoint(endpoint, token)


if __name__ == "__main__":
    main()