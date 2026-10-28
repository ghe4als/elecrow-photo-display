#!/usr/bin/env python3
"""Prepare travel photos for the CrowPanel ESP32 photo frame.

Reads HEIC/JPEG/PNG files from a source folder, resizes them to fit the
800x480 screen, and writes baseline JPEGs to the SD card's /photos folder.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCREEN_W = 800
SCREEN_H = 480
SUPPORTED_INPUT = {".heic", ".heif", ".jpg", ".jpeg", ".png"}


def supported(path: Path) -> bool:
    if path.name.startswith("._"):
        return False
    return path.suffix.lower() in SUPPORTED_INPUT


def run_sips(src: Path, dst: Path, width: int, height: int) -> bool:
    # Convert to a plain sRGB JPEG first (forces baseline, strips HEIC container).
    tmp_jpg = dst.with_suffix(".tmp.jpg")
    cmd_convert = [
        "sips",
        "-s", "format", "jpeg",
        "-s", "formatOptions", "best",
        "-s", "profile", "/System/Library/ColorSync/Profiles/sRGB Profile.icc",
        src,
        "--out", tmp_jpg,
    ]
    result = subprocess.run(cmd_convert, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"sips conversion failed for {src}: {result.stderr}")
        return False

    # Resize to fit inside the target box and pad to exact size with black bars.
    cmd_resize = [
        "sips",
        "-Z", str(max(width, height)),
        "-p", str(height), str(width),
        "--padColor", "000000",
        tmp_jpg,
        "--out", dst,
    ]
    result = subprocess.run(cmd_resize, capture_output=True, text=True)
    tmp_jpg.unlink(missing_ok=True)
    if result.returncode != 0:
        print(f"sips resize failed for {src}: {result.stderr}")
        return False
    return True


def eject_volume(mount_point: Path) -> None:
    try:
        result = subprocess.run(
            ["diskutil", "info", str(mount_point)],
            capture_output=True,
            text=True,
            check=False,
        )
        match = re.search(r"Device Identifier:\s*(disk\d+)", result.stdout)
        if not match:
            print(f"[eject] could not determine disk for {mount_point}")
            return
        disk = match.group(1)
        subprocess.run(["diskutil", "eject", disk], check=True)
        print(f"[eject] ejected {disk} ({mount_point})")
    except subprocess.CalledProcessError as e:
        print(f"[eject] failed to eject {mount_point}: {e}")
    except FileNotFoundError:
        print("[eject] diskutil not found (not macOS?)")


def prepare_photo(src: Path, dst: Path, width: int, height: int) -> bool:
    print(f"Preparing {src.name} ...")

    tmp_dir = tempfile.mkdtemp(prefix="crowpanel-photo-")
    tmp_path = Path(tmp_dir) / f"tmp{src.suffix}"

    # Convert to JPEG first if HEIC/HEIF.
    if src.suffix.lower() in {".heic", ".heif"}:
        jpeg_tmp = Path(tmp_dir) / "converted.jpg"
        if not run_sips(src, jpeg_tmp, width, height):
            shutil.rmtree(tmp_dir)
            return False
        tmp_path = jpeg_tmp
    else:
        # For input JPEG/PNG, copy then resize below.
        tmp_path = src

    # Final resize to fit exactly with aspect preservation and padding.
    if not run_sips(tmp_path, dst, width, height):
        shutil.rmtree(tmp_dir)
        return False

    shutil.rmtree(tmp_dir)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Resize and copy photos to the CrowPanel SD card."
    )
    parser.add_argument(
        "--sd",
        type=Path,
        default=Path("/Volumes/SDCARD"),
        help="Path to the mounted SD card (default: /Volumes/SDCARD)",
    )
    parser.add_argument(
        "sd_positional",
        nargs="?",
        type=Path,
        default=None,
        help="Path to the mounted SD card (positional alternative to --sd)",
    )
    parser.add_argument(
        "--upload-dir",
        type=Path,
        default=None,
        help="Source folder for raw photos (default: SD/upload)",
    )
    parser.add_argument(
        "--photos-dir",
        type=Path,
        default=None,
        help="Destination folder for prepared JPEGs (default: SD/photos)",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=SCREEN_W,
        help="Target width",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=SCREEN_H,
        help="Target height",
    )
    parser.add_argument(
        "--no-eject",
        action="store_true",
        help="Do not eject the SD card after processing",
    )
    args = parser.parse_args()

    sd = args.sd_positional or args.sd

    upload_dir = args.upload_dir or sd / "upload"
    photos_dir = args.photos_dir or sd / "photos"

    if not upload_dir.is_dir():
        print(f"Source folder not found: {upload_dir}")
        return 1

    photos_dir.mkdir(parents=True, exist_ok=True)

    files = sorted(f for f in upload_dir.iterdir() if f.is_file())
    prepared = 0
    skipped = 0

    for f in files:
        if not supported(f):
            print(f"Skipping {f.name}: unsupported format")
            skipped += 1
            continue

        dst = photos_dir / (f.stem + ".jpg")
        if prepare_photo(f, dst, args.width, args.height):
            prepared += 1
        else:
            print(f"Failed to prepare {f.name}")
            skipped += 1

    print(f"Done: {prepared} prepared, {skipped} skipped.")
    print(f"Output: {photos_dir}")
    print(f"Source: {upload_dir}")

    if not args.no_eject:
        eject_volume(sd)

    return 0


if __name__ == "__main__":
    sys.exit(main())
