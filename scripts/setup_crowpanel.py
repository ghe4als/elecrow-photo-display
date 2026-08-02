#!/usr/bin/env python3
"""Vendor the modified LovyanGFX library the CrowPanel 5.0" example depends on.

The Elecrow example ships a patched LovyanGFX whose ESP32-S3 Bus_RGB exposes
`getFrameBuffer()` / `presentFrameBuffer()`. The upstream registry version
(lovyan03/LovyanGFX) lacks those, so we copy the bundled copy from the cloned
example repo into lib/LovyanGFX (gitignored) before building.

Usage:
    python3 scripts/setup_crowpanel.py            # auto-find sibling example repo
    python3 scripts/setup_crowpanel.py --src /path/to/CrowPanel-5.0-HMI-ESP32-Display-800x480
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SRC = ROOT.parent / "CrowPanel-5.0-HMI-ESP32-Display-800x480"
LIB_SRC_REL = "example/V3.0/Arduino/libraries/LovyanGFX"
DEST = ROOT / "lib" / "LovyanGFX"


def find_src(explicit: str | None) -> Path:
    if explicit:
        p = Path(explicit).expanduser()
        if not p.is_dir():
            sys.exit(f"error: --src {p} is not a directory")
        return p
    candidates = [DEFAULT_SRC, ROOT.parent / "CrowPanel-5.0-HMI-ESP32-Display-800x480"]
    seen: set[Path] = set()
    for c in candidates:
        if c in seen:
            continue
        seen.add(c)
        if c.is_dir():
            return c
    sys.exit(
        f"error: could not find the CrowPanel example repo. Expected it at\n"
        f"       {DEFAULT_SRC}\n"
        f"       pass --src /path/to/CrowPanel-5.0-HMI-ESP32-Display-800x480"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", help="path to the cloned CrowPanel example repo")
    ap.add_argument("--force", action="store_true", help="re-copy even if dest exists")
    args = ap.parse_args()

    src = find_src(args.src)
    lib_src = src / LIB_SRC_REL
    if not lib_src.is_dir():
        sys.exit(f"error: expected LovyanGFX at {lib_src} but it is missing")

    if DEST.exists() and not args.force:
        print(f"ok: {DEST} already present (use --force to re-copy)")
        return 0
    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(lib_src, DEST)
    print(f"vendored LovyanGFX: {lib_src} -> {DEST}")
    print("next: pio run -e crowpanel-5")
    return 0


if __name__ == "__main__":
    sys.exit(main())