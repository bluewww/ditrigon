#!/usr/bin/env python3
"""
Regression test for cross-nick DCC RESUME spoofing.

Trigger setup:
- queue active DCC SEND to nick "victim"
- capture offered filename and port from outbound CTCP
- inject DCC RESUME from nick "attacker" with that port

Expected secure behavior:
- spoofed RESUME must not produce a DCC ACCEPT response
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
    client_nick: str | None
    offered_file: str | None
    offered_port: int | None
    spoof_sent: bool
    observed_accept: bool


class RegressionServer(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ServerResult(False, None, None, None, None, False, False)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8.0)
        self.port = self.sock.getsockname()[1]

    def run(self) -> None:
        conn = None
        pending = b""
        sent_welcome = False

        try:
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                self.result.error = "accept timeout"
                return

            self.result.connected = True
            conn.settimeout(0.05)

            deadline = time.time() + 6.5
            while time.time() < deadline:
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

                    if line.startswith(b"NICK ") and self.result.client_nick is None:
                        self.result.client_nick = line[5:].decode("utf-8", "replace")

                    if not sent_welcome and line.startswith(b"USER "):
                        nick = self.result.client_nick or "fuzz"
                        if not _safe_send(conn, (":srv 001 %s :welcome" % nick).encode()):
                            return
                        if not _safe_send(conn, (":srv 376 %s :end of motd" % nick).encode()):
                            return
                        sent_welcome = True

                    if b"PRIVMSG victim :" in line and b"\x01DCC SEND " in line and not self.result.spoof_sent:
                        parts = line.split(b"\x01")
                        if len(parts) < 2:
                            continue
                        tokens = parts[1].split()
                        if len(tokens) < 6 or tokens[0] != b"DCC" or tokens[1] != b"SEND":
                            continue

                        offered_file = tokens[2].decode("utf-8", "replace")
                        try:
                            offered_port = int(tokens[4])
                        except ValueError:
                            continue
                        if offered_port <= 0 or offered_port > 65535:
                            continue

                        self.result.offered_file = offered_file
                        self.result.offered_port = offered_port

                        nick = self.result.client_nick or "fuzz"
                        spoof = ":attacker!u@198.51.100.88 PRIVMSG %s :\x01DCC RESUME %s %d 3\x01" % (
                            nick,
                            offered_file,
                            offered_port,
                        )
                        if _safe_send(conn, spoof.encode()):
                            self.result.spoof_sent = True
                        continue

                    if b"PRIVMSG victim :" in line and b"\x01DCC ACCEPT " in line and self.result.spoof_sent:
                        parts = line.split(b"\x01")
                        if len(parts) < 2:
                            continue
                        tokens = parts[1].split()
                        if len(tokens) < 5 or tokens[0] != b"DCC" or tokens[1] != b"ACCEPT":
                            continue
                        if self.result.offered_file is None or self.result.offered_port is None:
                            continue

                        try:
                            accept_port = int(tokens[3])
                            accept_pos = int(tokens[4])
                        except ValueError:
                            continue

                        accept_file = tokens[2].decode("utf-8", "replace")
                        if accept_file == self.result.offered_file and accept_port == self.result.offered_port and accept_pos == 3:
                            self.result.observed_accept = True
                            return
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


def _write_startup(cfgdir: str, payload_path: str) -> None:
    with open(os.path.join(cfgdir, "startup.txt"), "w", encoding="utf-8") as startup:
        startup.write("TIMER 1 DCC SEND victim %s\n" % payload_path)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: dcc_resume_nick_spoof_security_poc.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-dcc-resume-nick-spoof-")
    try:
        payload_path = os.path.join(cfgdir, "payload.bin")
        with open(payload_path, "wb") as payload:
            payload.write(b"resume-spoof-proof\n")

        _write_startup(cfgdir, payload_path)

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
    if server.result.offered_file is None or server.result.offered_port is None:
        print("did not capture outbound DCC SEND offer")
        bad = True
    if not server.result.spoof_sent:
        print("did not inject spoofed DCC RESUME")
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True
    if server.result.observed_accept:
        print("client emitted DCC ACCEPT in response to spoofed RESUME")
        bad = True

    if bad:
        print("DCC_RESUME_NICK_SPOOF_REGRESSION=FAIL")
        return 1

    print("DCC_RESUME_NICK_SPOOF_REGRESSION=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
