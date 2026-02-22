#!/usr/bin/env python3
"""
Deterministic parser fuzz smoke test for HexChat.

This is intentionally lightweight and bounded so it can run in CI:
- exercises IRC message/tag parsing and DCC CTCP handling paths
- exercises CTCP auto-reply expansion paths (auto_insert/check_special_chars)
- fails fast on sanitizer signatures in process output
"""

from __future__ import annotations

import random
import shutil
import socket
import subprocess
import sys
import os
import signal
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


def _rand_ascii(rng: random.Random, length: int) -> bytes:
    return bytes(rng.randint(33, 126) for _ in range(length))


def _rand_bytes(rng: random.Random, length: int) -> bytes:
    return bytes(rng.randint(1, 255) for _ in range(length))


def _safe_send(conn: socket.socket, line: bytes) -> bool:
    try:
        conn.sendall(line + b"\r\n")
        return True
    except OSError:
        return False


def _parser_seed_lines() -> list[bytes]:
    return [
        b":srv 001 fuzz :welcome",
        b":srv 005 fuzz CHANTYPES=#& PREFIX=(ov)@+ :supported",
        b":srv 376 fuzz :end of motd",
        b"@time=2024-12-31T23:59:59.999Z;account=a :n!u@h PRIVMSG #c :hello",
        b"@time=1712345678.123;solanum.chat/identified=1 :n!u@h NOTICE fuzz :\x01PING\x01",
        b":n!u@h PRIVMSG fuzz :\x01DCC CHAT chat 2130706433 12345\x01",
        b':n!u@h PRIVMSG fuzz :\x01DCC SEND "file name.txt" 2130706433 0 123 99\x01',
        b":n!u@h PRIVMSG fuzz :\x01DCC SEND bad 0 0 0 0\x01",
        b":n!u@h PRIVMSG fuzz :\x01DCC RESUME file 0 18446744073709551615 2147483647\x01",
        b":n!u@h PRIVMSG fuzz :\x01DCC ACCEPT file 65535 1\x01",
        b"@aaa=\\:;bbb=\\s\\r\\n\\\\ :n!u@h PRIVMSG #c :tag escaping",
    ]


def _parser_mutated_line(rng: random.Random, i: int) -> bytes:
    taglen = rng.randint(0, 260)
    tags = _rand_ascii(rng, taglen)
    payload = _rand_bytes(rng, rng.randint(0, 1800))
    mode = i % 10

    if mode == 0:
        line = (
            b"@"
            + tags
            + b" :n!u@h PRIVMSG fuzz :\x01DCC SEND x "
            + str(rng.randint(0, 2**32 - 1)).encode()
            + b" "
            + str(rng.randint(0, 70000)).encode()
            + b" "
            + str(rng.randint(0, 2**64 - 1)).encode()
            + b"\x01"
        )
    elif mode == 1:
        line = (
            b"@"
            + tags
            + b" :n!u@h PRIVMSG fuzz :\x01DCC CHAT chat "
            + str(rng.randint(0, 2**32 - 1)).encode()
            + b" "
            + str(rng.randint(0, 70000)).encode()
            + b" "
            + str(rng.randint(0, 2**31 - 1)).encode()
            + b"\x01"
        )
    elif mode == 2:
        line = b"@" + tags + b" :n!u@h NOTICE fuzz :\x01" + payload + b"\x01"
    elif mode == 3:
        line = (
            b"@time=2025-01-01T00:00:00.000Z;"
            + tags
            + b" :srv 005 fuzz CHANTYPES=#& PREFIX=(ov)@+ :supported"
        )
    elif mode == 4:
        line = b":n!u@h PRIVMSG #c :" + payload
    elif mode == 5:
        line = (
            b"@"
            + tags
            + b" :n!u@h PRIVMSG fuzz :\x01DCC RESUME y "
            + str(rng.randint(0, 70000)).encode()
            + b" "
            + str(rng.randint(0, 2**64 - 1)).encode()
            + b" "
            + str(rng.randint(0, 2**31 - 1)).encode()
            + b"\x01"
        )
    elif mode == 6:
        line = (
            b"@"
            + tags
            + b" :n!u@h PRIVMSG fuzz :\x01DCC ACCEPT y "
            + str(rng.randint(0, 70000)).encode()
            + b" "
            + str(rng.randint(0, 2**64 - 1)).encode()
            + b"\x01"
        )
    elif mode == 7:
        line = b":srv 372 fuzz :" + payload
    elif mode == 8:
        line = b"@" + tags + b" :srv NOTICE * :" + payload
    else:
        line = b"@" + tags + b" :n!u@h PRIVMSG fuzz :\x01" + payload[:64] + b"\x01"

    return line[:8190]


def _ctcp_line(rng: random.Random, i: int) -> bytes:
    nick = ("n%d" % rng.randint(0, 9999)).encode()
    payload = _rand_bytes(rng, rng.randint(0, 512))
    if i % 2 == 0:
        return b":" + nick + b"!u@h PRIVMSG fuzz :\x01PING " + payload + b"\x01"
    return b":" + nick + b"!u@h PRIVMSG fuzz :\x01TIME " + payload + b"\x01"


@dataclass
class ServerResult:
    connected: bool
    error: str | None


class FuzzServer(threading.Thread):
    def __init__(self, seed: int, mode: str):
        super().__init__(daemon=True)
        self.seed = seed
        self.mode = mode
        self.result = ServerResult(False, None)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8.0)
        self.port = self.sock.getsockname()[1]

    def run(self) -> None:
        rng = random.Random(self.seed)
        try:
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                self.result.error = "accept timeout"
                return

            self.result.connected = True
            conn.settimeout(0.03)

            # Drain a bit of startup chatter from the client.
            for _ in range(10):
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                    if len(data) >= 3 and data[0] == 0x16 and data[1] == 0x03 and data[2] <= 0x04:
                        self.result.error = "client attempted TLS handshake"
                        return
                except OSError:
                    pass
                time.sleep(0.01)

            if self.mode == "parser":
                for line in _parser_seed_lines():
                    if not _safe_send(conn, line):
                        return
                for i in range(700):
                    if not _safe_send(conn, _parser_mutated_line(rng, i)):
                        return
            else:
                if not _safe_send(conn, b":srv 001 fuzz :welcome"):
                    return
                if not _safe_send(conn, b":srv 376 fuzz :end of motd"):
                    return
                for i in range(900):
                    if not _safe_send(conn, _ctcp_line(rng, i)):
                        return
        except OSError as exc:
            self.result.error = str(exc)
        finally:
            try:
                conn.close()  # type: ignore[name-defined]
            except Exception:
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
        # Use negative port syntax so /server treats this as insecure TCP.
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


def _has_sanitizer_finding(output: str) -> bool:
    for pattern in SANITIZER_PATTERNS:
        if pattern in output:
            return True
    return False


def _run_campaign(binary: str, cfgdir: str, mode: str, seeds: list[int]) -> int:
    failures = 0

    for idx, seed in enumerate(seeds, start=1):
        print("[%s seed %d/%d]" % (mode, idx, len(seeds)), flush=True)
        server = FuzzServer(seed, mode)
        server.start()

        rc, output = _run_client(binary, cfgdir, server.port)
        server.join(timeout=2.0)

        bad = False
        if not server.result.connected:
            print("  server did not receive a connection (%s)" % (server.result.error or "unknown"))
            bad = True
        if rc not in (0, 124):
            print("  unexpected client exit code %d" % rc)
            bad = True
        if _has_sanitizer_finding(output):
            print("  sanitizer finding")
            print(output[-2000:])
            bad = True

        if bad:
            failures += 1
        else:
            print("  no sanitizer finding")

    return failures


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_parser_smoke.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-fuzzcfg-")
    try:
        failures = 0
        failures += _run_campaign(binary, cfgdir, "parser", [1, 2, 3])
        failures += _run_campaign(binary, cfgdir, "ctcp", [11, 12])
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    if failures:
        print("FUZZ_SMOKE=FAIL failures=%d" % failures)
        return 1

    print("FUZZ_SMOKE=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
