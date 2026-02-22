#!/usr/bin/env python3
"""
Regression test for malformed UTF-8 handling in process_data_init().

The trigger is injected through startup.txt as a command line ending with a
lone UTF-8 lead byte (0xF0). This path reaches handle_command() directly
without server-side UTF-8 fixup.
"""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass


SANITIZER_PATTERNS = (
    "ERROR: AddressSanitizer",
    "SUMMARY: AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "runtime error:",
)


def _send_line(conn: socket.socket, line: bytes) -> bool:
    try:
        conn.sendall(line + b"\r\n")
        return True
    except OSError:
        return False


def _has_sanitizer_finding(output: str) -> bool:
    return any(pattern in output for pattern in SANITIZER_PATTERNS)


@dataclass
class ServerResult:
    connected: bool
    error: str | None


class RegressionServer(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ServerResult(False, None)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8.0)
        self.port = self.sock.getsockname()[1]

    def run(self) -> None:
        conn = None
        try:
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                self.result.error = "accept timeout"
                return

            self.result.connected = True
            conn.settimeout(0.03)

            # Drain a little startup chatter from the client.
            for _ in range(10):
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                except OSError:
                    pass
                time.sleep(0.01)

            if not _send_line(conn, b":srv 001 fuzz :welcome"):
                return
            if not _send_line(conn, b":srv 376 fuzz :end of motd"):
                return

            # Keep connection briefly alive while startup commands run.
            time.sleep(0.2)
        except OSError as exc:
            self.result.error = str(exc)
        finally:
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass
            self.sock.close()


def _run_client(binary: str, cfgdir: str, port: int) -> tuple[int, str]:
    xvfb_run = shutil.which("xvfb-run")
    if not xvfb_run:
        return 2, "xvfb-run not found"

    cmd = [
        xvfb_run,
        "-a",
        binary,
        "-d",
        cfgdir,
        "-n",
        "-a",
        "irc://127.0.0.1:-%d" % port,
    ]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        out, _ = proc.communicate(timeout=6)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=1.0)
        except (OSError, ProcessLookupError, subprocess.TimeoutExpired):
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except (OSError, ProcessLookupError):
                pass

        try:
            out, _ = proc.communicate(timeout=1.0)
        except subprocess.TimeoutExpired:
            out = b""
        rc = 124

    return rc if rc is not None else 1, out.decode("utf-8", "replace")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: parser_utf8_truncation_regression.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-utf8-regress-")
    try:
        # Trigger bytes: process_data_init() sees "X <0xF0>" in startup parser path.
        with open(os.path.join(cfgdir, "startup.txt"), "wb") as startup:
            startup.write(b"X \xf0\n")

        server = RegressionServer()
        server.start()
        rc, output = _run_client(binary, cfgdir, server.port)
        server.join(timeout=2.0)
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    bad = False
    if not server.result.connected:
        print("server did not receive a connection (%s)" % (server.result.error or "unknown"))
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True

    if bad:
        print("UTF8_TRUNCATION_REGRESSION=FAIL")
        return 1

    print("UTF8_TRUNCATION_REGRESSION=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
