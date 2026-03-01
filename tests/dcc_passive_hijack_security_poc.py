#!/usr/bin/env python3
"""
Regression test for passive DCC hijack by ID-only matching.

Flow:
- queue a passive send to nick "victim"
- observe the generated passive DCC id (pasvid)
- spoof a third-step DCC SEND from a different nick using that same id
- assert the client does NOT connect to the spoofed endpoint
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
class ListenerResult:
    connected: bool
    error: str | None


class SpoofTargetListener(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ListenerResult(False, None)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8.0)
        self.port = self.sock.getsockname()[1]

    def run(self) -> None:
        conn = None
        try:
            conn, _ = self.sock.accept()
            self.result.connected = True
        except OSError as exc:
            self.result.error = str(exc)
        finally:
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass
            self.sock.close()


@dataclass
class ServerResult:
    connected: bool
    error: str | None
    client_nick: str | None
    captured_pasvid: int | None
    spoof_sent: bool


class RegressionServer(threading.Thread):
    def __init__(self, spoof_port: int) -> None:
        super().__init__(daemon=True)
        self.spoof_port = spoof_port
        self.result = ServerResult(False, None, None, None, False)

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

            deadline = time.time() + 6.0
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

                    if b"PRIVMSG victim :" in line and b"\x01DCC SEND " in line:
                        parts = line.split(b"\x01")
                        if len(parts) >= 2:
                            tokens = parts[1].split()
                            if len(tokens) >= 7:
                                try:
                                    pasvid = int(tokens[-1])
                                except ValueError:
                                    pasvid = None
                                if pasvid is not None:
                                    self.result.captured_pasvid = pasvid
                                    nick = self.result.client_nick or "fuzz"
                                    spoof = (
                                        ":attacker!u@h PRIVMSG %s :\x01DCC SEND fake 2130706433 %d 1 %d\x01"
                                        % (nick, self.spoof_port, pasvid)
                                    )
                                    if _safe_send(conn, spoof.encode()):
                                        self.result.spoof_sent = True
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
        startup.write("TIMER 1 DCC PSEND victim %s\n" % payload_path)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: dcc_passive_hijack_security_poc.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-dcc-passive-hijack-")
    listener = SpoofTargetListener()
    listener.start()

    try:
        payload_path = os.path.join(cfgdir, "payload.bin")
        with open(payload_path, "wb") as payload:
            payload.write(b"proof\n")

        _write_startup(cfgdir, payload_path)

        server = RegressionServer(listener.port)
        server.start()

        rc, output = _run_client(binary, cfgdir, server.port)
        server.join(timeout=2.0)
        listener.join(timeout=2.0)
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    bad = False
    if not server.result.connected:
        print("server did not receive a connection (%s)" % (server.result.error or "unknown"))
        bad = True
    if server.result.captured_pasvid is None:
        print("did not capture outbound passive DCC id")
        bad = True
    if not server.result.spoof_sent:
        print("did not inject spoofed passive DCC response")
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True
    if listener.result.connected:
        print("client connected to spoof target; passive DCC sender binding bypassed")
        bad = True

    if bad:
        print("DCC_PASSIVE_HIJACK_REGRESSION=FAIL")
        return 1

    print("DCC_PASSIVE_HIJACK_REGRESSION=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
