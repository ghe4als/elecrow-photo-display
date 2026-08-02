#!/usr/bin/env python3
"""Detect the connected ESP32 board and print its serial port + matching
PlatformIO environment. Used by flash.py and standalone for CI / scripting.

Output (machine-parseable, one KEY=VALUE per line):
    PORT=/dev/cu.usbmodem1201
    ENV=esp32-s3-arduino
    CHIP=ESP32-S3

Exit codes:
    0   exactly one ESP32 board detected
    1   no ESP32 board found
    2   multiple candidate ports found (ambiguous - pass --port)
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# Map esptool's "Detecting chip type... <X>" string to a PlatformIO env name.
# Add new chip variants here as you expand the starter.
CHIP_TO_ENV = {
    "ESP32":     "esp32dev-arduino",
    "ESP32-S2":  "esp32-s2-arduino",   # placeholder; add the env to platformio.ini to enable
    "ESP32-S3":  "esp32-s3-arduino",
    "ESP32-C3":  "esp32-c3-arduino",   # placeholder; add the env to enable
    "ESP32-C6":  "esp32-c6-arduino",   # placeholder; add the env to enable
}

# USB VID:PID prefixes that are almost certainly Espressif USB-serial/JTAG or
# CP210x/CH340 USB-UART bridges on an ESP32 board. Used only as a hint to narrow
# down which /dev/cu.* port to probe - esptool does the authoritative detection.
ESPRESSIF_USB_HINTS = ("303A:", "10C4:", "1A86:", "0403:", "067B:")

# PlatformIO's bundled python (the one esptoolpy expects to run under).
PIO_PY = "/opt/homebrew/Cellar/platformio/6.1.19_2/libexec/bin/python"
ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")


def list_candidate_ports() -> list[str]:
    """Return /dev/cu.* ports that look like USB-serial adapters."""
    try:
        out = subprocess.check_output(["pio", "device", "list", "--json-output"],
                                      text=True, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Fallback: glob the filesystem
        return sorted(str(p) for p in Path("/dev").glob("cu.usb*"))

    ports = []
    try:
        for entry in json.loads(out):
            port = entry.get("port", "")
            hwid = entry.get("hwid", "") or ""
            if port.startswith("/dev/cu.") and any(h in hwid for h in ESPRESSIF_USB_HINTS):
                ports.append(port)
    except json.JSONDecodeError:
        pass
    if not ports:
        # last-ditch glob
        ports = sorted(str(p) for p in Path("/dev").glob("cu.usb*"))
    return sorted(set(ports))


def detect_chip(port: str) -> str | None:
    """Run esptool and return the chip name (e.g. 'ESP32-S3') or None."""
    if not Path(ESPTOOL).exists():
        print(f"error: esptool not found at {ESPTOOL}", file=sys.stderr)
        print("       run `pio pkg install -g -t tool-esptoolpy` first", file=sys.stderr)
        return None
    try:
        proc = subprocess.run(
            [PIO_PY, ESPTOOL, "--port", port, "chip_id"],
            capture_output=True, text=True, timeout=20,
        )
    except subprocess.TimeoutExpired:
        return None
    m = re.search(r"Detecting chip type\.\.\.\s*(\S+[^\n]*)", proc.stdout)
    if not m:
        return None
    chip = m.group(1).strip()
    # normalize "ESP32-S3 (QFN56) (revision v0.2)" -> "ESP32-S3"
    return chip.split()[0] if chip else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="serial port to probe (auto-detect if omitted)")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of KEY=VALUE")
    args = ap.parse_args()

    port = args.port
    if port is None:
        ports = list_candidate_ports()
        if not ports:
            print("error: no USB-serial ports found. Plug in a board and check:",
                  file=sys.stderr)
            print("       pio device list", file=sys.stderr)
            return 1
        if len(ports) > 1:
            print("error: multiple candidate ports found:", file=sys.stderr)
            for p in ports:
                print(f"       {p}", file=sys.stderr)
            print("pass --port to disambiguate", file=sys.stderr)
            return 2
        port = ports[0]

    chip = detect_chip(port)
    if chip is None:
        print(f"error: could not detect chip on {port}", file=sys.stderr)
        print("       hold BOOT, tap EN/RST, release EN, release BOOT, retry",
              file=sys.stderr)
        return 1

    env = CHIP_TO_ENV.get(chip)
    if env is None:
        print(f"error: detected {chip} but no env mapping in CHIP_TO_ENV",
              file=sys.stderr)
        print("       edit scripts/detect.py to add it", file=sys.stderr)
        return 1

    result = {"PORT": port, "ENV": env, "CHIP": chip}
    if args.json:
        print(json.dumps(result))
    else:
        for k, v in result.items():
            print(f"{k}={v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())