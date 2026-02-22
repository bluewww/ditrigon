#!/usr/bin/env python3
"""
Regression test for servlist nick overflow in servlist_connect().

Trigger:
- crafted servlist.conf has an oversized I= nick
- connect by network name via URL, which reaches:
    strcpy(serv->nick, net->nick)
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
    error: str | None
    nick_len: int | None


class RegressionServer(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ServerResult(False, None, None)

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

            pending = b""
            deadline = time.time() + 2.0
            while time.time() < deadline and self.result.nick_len is None:
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                    pending += data
                except OSError:
                    time.sleep(0.01)
                    continue

                while b"\r\n" in pending:
                    line, pending = pending.split(b"\r\n", 1)
                    if line.startswith(b"NICK "):
                        self.result.nick_len = len(line[5:])
                        break

            _safe_send(conn, b":srv 001 fuzz :welcome")
            _safe_send(conn, b":srv 376 fuzz :end of motd")
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


def _run_client(binary: str, cfgdir: str) -> tuple[int, str]:
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
        "irc://fuzznet",
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


def _write_servlist(cfgdir: str, port: int) -> None:
    long_nick = "A" * 800
    servlist = (
        "v=1.0.3\n"
        "N=fuzznet\n"
        "I=%s\n"
        "F=0\n"
        "D=0\n"
        "S=127.0.0.1/%d\n"
    ) % (long_nick, port)

    with open(os.path.join(cfgdir, "servlist.conf"), "w", encoding="utf-8") as conf:
        conf.write(servlist)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: servlist_nick_overflow_regression.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-servlist-overflow-")
    try:
        server = RegressionServer()
        _write_servlist(cfgdir, server.port)
        server.start()
        rc, output = _run_client(binary, cfgdir)
        server.join(timeout=2.0)
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    bad = False
    if not server.result.connected:
        print("server did not receive a connection (%s)" % (server.result.error or "unknown"))
        bad = True
    if server.result.nick_len is None:
        print("did not capture client NICK command")
        bad = True
    elif server.result.nick_len > 63:
        print("oversized NICK sent (%d bytes)" % server.result.nick_len)
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True

    if bad:
        print("SERVLIST_NICK_OVERFLOW_REGRESSION=FAIL")
        return 1

    print("SERVLIST_NICK_OVERFLOW_REGRESSION=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
