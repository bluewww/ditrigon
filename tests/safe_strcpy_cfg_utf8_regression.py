#!/usr/bin/env python3
"""
Regression test for malformed UTF-8 handling in safe_strcpy().

Trigger path:
- malformed UTF-8 in hexchat.conf
- load_config() -> cfg_get_str() -> safe_strcpy()
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
import tempfile


SANITIZER_PATTERNS = (
    "ERROR: AddressSanitizer",
    "SUMMARY: AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "runtime error:",
)


def _has_sanitizer_finding(output: str) -> bool:
    return any(pattern in output for pattern in SANITIZER_PATTERNS)


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
        print("usage: safe_strcpy_cfg_utf8_regression.py <ditrigon_binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    if not shutil.which(binary) and not binary.startswith("/"):
        print("binary not found: %s" % binary, file=sys.stderr)
        return 2
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found in PATH", file=sys.stderr)
        return 2

    cfgdir = tempfile.mkdtemp(prefix="hexchat-safe-strcpy-cfg-")
    try:
        # Lone lead byte at end of value: old safe_strcpy() overreads here.
        with open(os.path.join(cfgdir, "hexchat.conf"), "wb") as conf:
            conf.write(b"irc_nick1 = \xf0")

        rc, output = _run_client(binary, cfgdir)
    finally:
        shutil.rmtree(cfgdir, ignore_errors=True)

    bad = False
    if rc not in (0, 124):
        print("unexpected client exit code %d" % rc)
        bad = True
    if _has_sanitizer_finding(output):
        print("sanitizer finding")
        print(output[-2000:])
        bad = True

    if bad:
        print("SAFE_STRCPY_CFG_UTF8_REGRESSION=FAIL")
        return 1

    print("SAFE_STRCPY_CFG_UTF8_REGRESSION=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
