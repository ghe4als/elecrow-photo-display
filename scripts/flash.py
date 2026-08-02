#!/usr/bin/env python3
"""Auto-detect the connected ESP32 board, then build/upload/monitor using the
matching PlatformIO environment. Replaces having to remember which `-e <env>`
matches which board.

Usage:
    scripts/flash.py                 # build + upload + monitor (auto-detect)
    scripts/flash.py --no-monitor     # build + upload only
    scripts/flash.py --build-only     # build only, no upload
    scripts/flash.py --port /dev/X    # override auto-detection
    scripts/flash.py --wifi SSID PASS # inject WIFI_SSID/WIFI_PASS env vars
    scripts/flash.py -- --verbose     # extra args after -- are passed to pio

Exit code: nonzero if any step fails.
"""
from __future__ import annotations

import argparse
import configparser
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
DETECT = SCRIPT_DIR / "detect.py"


def env_exists_in_ini(env: str) -> bool:
    """True if [env:<env>] is defined in platformio.ini."""
    cp = configparser.ConfigParser(inline_comment_prefixes=(";",))
    cp.read(PROJECT_DIR / "platformio.ini")
    return f"env:{env}" in cp.sections()


def run(cmd: list[str], env: dict[str, str] | None = None) -> int:
    print(f"+ {' '.join(cmd)}", file=sys.stderr)
    return subprocess.call(cmd, cwd=PROJECT_DIR, env=env)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (auto-detect if omitted)")
    ap.add_argument("--build-only", action="store_true", help="skip upload+monitor")
    ap.add_argument("--no-monitor", action="store_true", help="skip monitor after upload")
    ap.add_argument("--wifi", nargs=2, metavar=("SSID", "PASS"),
                    help="inject WIFI_SSID / WIFI_PASS env vars")
    extra, passthrough = ap.parse_known_args()

    # 1. detect chip + port + env
    detect_cmd = [sys.executable, str(DETECT)]
    if extra.port:
        detect_cmd += ["--port", extra.port]
    detect_cmd += ["--json"]
    try:
        out = subprocess.check_output(detect_cmd, text=True)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(e.stderr or "detection failed\n")
        return e.returncode
    import json
    info = json.loads(out)
    port, env, chip = info["PORT"], info["ENV"], info["CHIP"]
    print(f"[detect] {chip} on {port} -> env {env}", file=sys.stderr)

    # 2. sanity check the env exists
    if not env_exists_in_ini(env):
        print(f"error: env `{env}` is not defined in platformio.ini", file=sys.stderr)
        print(f"       chip was {chip}; add an [env:{env}] section or edit CHIP_TO_ENV",
              file=sys.stderr)
        return 1

    # 3. build/upload/monitor
    want_monitor = not extra.build_only and not extra.no_monitor
    if want_monitor and not sys.stdout.isatty():
        print("warning: stdout is not a TTY; skipping monitor to avoid a "
              "termios crash. Run this command from a real terminal to monitor, "
              "or re-run with --no-monitor to silence this.",
              file=sys.stderr)
        want_monitor = False

    cmd = ["pio", "run", "-e", env]
    if not extra.build_only:
        cmd += ["-t", "upload", "--upload-port", port]
        if want_monitor:
            cmd += ["-t", "monitor", "--monitor-port", port]
    cmd += passthrough  # extra args after `--` forwarded to pio

    env_export = os.environ.copy()
    if extra.wifi:
        env_export["WIFI_SSID"] = extra.wifi[0]
        env_export["WIFI_PASS"] = extra.wifi[1]
    return run(cmd, env=env_export)


if __name__ == "__main__":
    sys.exit(main())