#!/usr/bin/env python3
"""
Regression test for active DCC SEND listener hijack with non-numeric peer hosts.

Trigger setup:
- queue active DCC SEND to nick "victim"
- register victim in userlist with a hostname (not IPv4 literal)
- connect to advertised listening port from unrelated local attacker

Expected secure behavior:
- unauthorized peer must not receive transfer bytes
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
class AttackerResult:
    connected: bool
    received: bytes
    error: str | None


class AttackerClient(threading.Thread):
    def __init__(self, port: int) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.result = AttackerResult(False, b"", None)

    def run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        try:
            sock.connect(("127.0.0.1", self.port))
            self.result.connected = True
            try:
                self.result.received = sock.recv(4096)
            except OSError:
                self.result.received = b""
        except OSError as exc:
            self.result.error = str(exc)
        finally:
            sock.close()


@dataclass
class ServerResult:
    connected: bool
    error: str | None
    client_nick: str | None
    dcc_port: int | None
    attacker_result: AttackerResult | None


class RegressionServer(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True)
        self.result = ServerResult(False, None, None, None, None)

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
                        if not _safe_send(conn, (":%s!self@127.0.0.1 JOIN :#race" % nick).encode()):
                            return
                        if not _safe_send(conn, b":victim!id@victim.example PRIVMSG #race :hello"):
                            return
                        if not _safe_send(conn, b":victim!id@victim.example JOIN :#race"):
                            return
                        sent_welcome = True

                    if b"PRIVMSG victim :" not in line or b"\x01DCC SEND " not in line:
                        continue

                    parts = line.split(b"\x01")
                    if len(parts) < 2:
                        continue
                    tokens = parts[1].split()
                    if len(tokens) < 6 or tokens[0] != b"DCC" or tokens[1] != b"SEND":
                        continue
                    try:
                        dcc_port = int(tokens[4])
                    except ValueError:
                        continue
                    if dcc_port <= 0 or dcc_port > 65535:
                        continue

                    self.result.dcc_port = dcc_port

                    attacker = AttackerClient(dcc_port)
                    attacker.start()
                    attacker.join(timeout=2.0)
                    self.result.attacker_result = attacker.result
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
        print("usage: dcc_send_listener_hostname_hijack_security_poc.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-dcc-send-listener-hostname-hijack-")
    try:
        payload_path = os.path.join(cfgdir, "payload.bin")
        with open(payload_path, "wb") as payload:
            payload.write(b"host-hijack-proof\n")

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
    if server.result.dcc_port is None:
        print("did not capture outbound DCC SEND port")
        bad = True
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True

    attacker = server.result.attacker_result
    if attacker is None:
        print("attacker probe did not run")
        bad = True
    elif len(attacker.received) > 0:
        print("unauthorized peer received transfer bytes")
        bad = True

    if bad:
        print("DCC_SEND_LISTENER_HOSTNAME_HIJACK_REGRESSION=FAIL")
        return 1

    print("DCC_SEND_LISTENER_HOSTNAME_HIJACK_REGRESSION=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
