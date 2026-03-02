#!/usr/bin/env python3
"""
Regression test for Track A: queued IRC lines must survive short sends.

The test forces one short write for an outbound PONG using LD_PRELOAD and
expects the client to eventually deliver the full line.
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

SHORT_SEND_MARKER = "HEXCHAT_SHORT_SEND_TRIGGERED"
PING_TOKEN = "tracka-short-send-token"
EXPECTED_PONG = ("PONG %s\r\n" % PING_TOKEN).encode("ascii")
SHORT_SEND_PREFIX = "PONG %s" % PING_TOKEN


def _has_sanitizer_finding(output: str) -> bool:
    return any(pattern in output for pattern in SANITIZER_PATTERNS)


def _safe_send(conn: socket.socket, line: bytes) -> bool:
    try:
        conn.sendall(line + b"\r\n")
        return True
    except OSError:
        return False


@dataclass
class ServerResult:
    connected: bool
    ping_sent: bool
    saw_pong_prefix: bool
    saw_full_expected_pong: bool
    error: str | None


class RegressionServer(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ServerResult(False, False, False, False, None)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8.0)
        self.port = self.sock.getsockname()[1]

    def run(self) -> None:
        conn = None
        pending = b""
        captured = bytearray()
        nick = "fuzz"
        welcomed = False

        try:
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                self.result.error = "accept timeout"
                return

            self.result.connected = True
            conn.settimeout(0.05)

            deadline = time.time() + 6.0
            while time.time() < deadline:
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                    pending += data
                    captured.extend(data)
                except OSError:
                    time.sleep(0.01)
                    continue

                while b"\r\n" in pending:
                    line, pending = pending.split(b"\r\n", 1)

                    if line.startswith(b"NICK "):
                        nick = line[5:].decode("utf-8", "replace")

                    if not welcomed and line.startswith(b"USER "):
                        if not _safe_send(conn, (":srv 001 %s :welcome" % nick).encode("utf-8")):
                            return
                        if not _safe_send(conn, (":srv 376 %s :end of motd" % nick).encode("utf-8")):
                            return
                        if not _safe_send(conn, ("PING %s" % PING_TOKEN).encode("ascii")):
                            return

                        self.result.ping_sent = True
                        welcomed = True

            data_all = bytes(captured)
            self.result.saw_pong_prefix = b"PONG " in data_all
            self.result.saw_full_expected_pong = EXPECTED_PONG in data_all
        except OSError as exc:
            self.result.error = str(exc)
        finally:
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass
            self.sock.close()


def _run_client(binary: str, cfgdir: str, port: int, preload_so: str) -> tuple[int, str]:
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

    env = os.environ.copy()
    existing_preload = env.get("LD_PRELOAD")
    if existing_preload:
        env["LD_PRELOAD"] = existing_preload + ":" + preload_so
    else:
        env["LD_PRELOAD"] = preload_so
    env["HEXCHAT_SHORT_SEND_PREFIX"] = SHORT_SEND_PREFIX

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
        env=env,
    )
    try:
        out, _ = proc.communicate(timeout=8)
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
    if len(sys.argv) != 3:
        print("usage: irc_partial_send_queue_poc.py <ditrigon_binary> <short_send_preload_so>", file=sys.stderr)
        return 2

    binary = sys.argv[1]
    preload_so = sys.argv[2]

    if not os.path.isfile(preload_so):
        print("preload library not found: %s" % preload_so, file=sys.stderr)
        return 2
    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-tracka-partial-send-")
    try:
        server = RegressionServer()
        server.start()

        rc, output = _run_client(binary, cfgdir, server.port, preload_so)
        server.join(timeout=2.0)
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    bad = False
    if not server.result.connected:
        print("server did not receive a connection (%s)" % (server.result.error or "unknown"))
        bad = True
    if not server.result.ping_sent:
        print("did not send PING probe")
        bad = True
    if SHORT_SEND_MARKER not in output:
        print("short-send preload did not trigger")
        bad = True
    if not server.result.saw_pong_prefix:
        print("did not observe any outbound PONG bytes")
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True

    if bad:
        print("client output tail:")
        print(output[-4000:])
        print("IRC_PARTIAL_SEND_QUEUE_REGRESSION=INCONCLUSIVE")
        return 1

    if not server.result.saw_full_expected_pong:
        print("IRC_PARTIAL_SEND_QUEUE_REGRESSION=FAIL")
        print("full PONG was not observed after forced short send")
        return 1

    print("IRC_PARTIAL_SEND_QUEUE_REGRESSION=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
