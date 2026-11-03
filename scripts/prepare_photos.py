#!/usr/bin/env python3
"""Prepare travel photos for the CrowPanel ESP32 photo frame.

Reads HEIC/JPEG/PNG files from a source folder, resizes them to fit the
800x480 screen, and writes baseline JPEGs to the SD card's /photos folder.
Photos whose output JPEG already exists and is newer than the source are
skipped, keeping incremental re-syncs cheap.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

SCREEN_W = 800
SCREEN_H = 480
SUPPORTED_INPUT = {".heic", ".heif", ".jpg", ".jpeg", ".png"}


def supported(path: Path) -> bool:
    if path.name.startswith("._"):
        return False
    return path.suffix.lower() in SUPPORTED_INPUT


def run_sips_convert(src: Path, dst: Path, fmt: str) -> bool:
    cmd = [
        "sips",
        "-s", "format", fmt,
        "-s", "formatOptions", "best",
        str(src),
        "--out", str(dst),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"sips conversion failed for {src}: {result.stderr}")
        return False
    return True


def read_exif_orientation(src: Path) -> int:
    if Image is None:
        return 1
    try:
        with Image.open(src) as img:
            return img.getexif().get(274, 1)
    except Exception:
        return 1


def crop_to_fill(img, width: int, height: int):
    """Crop and resize so the output exactly fills width x height.

    Preserves the center of the image and trims the longer axis to match
    the target aspect ratio before scaling up/down to fit the screen.
    """
    target_ratio = width / height
    src_ratio = img.width / img.height

    if src_ratio > target_ratio:
        # Source is too wide; crop left/right to match target height ratio.
        new_width = int(img.height * target_ratio)
        left = (img.width - new_width) // 2
        img = img.crop((left, 0, left + new_width, img.height))
    elif src_ratio < target_ratio:
        # Source is too tall; crop top/bottom to match target width ratio.
        new_height = int(img.width / target_ratio)
        top = (img.height - new_height) // 2
        img = img.crop((0, top, img.width, top + new_height))

    if img.size != (width, height):
        img = img.resize((width, height), Image.Resampling.LANCZOS)
    return img


def fit_inside_letterbox(img, width: int, height: int):
    """Shrink to fit inside width x height and center on a black canvas."""
    img.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    x = (width - img.width) // 2
    y = (height - img.height) // 2
    canvas.paste(img, (x, y))
    return canvas


def prepare_with_pillow(src: Path, dst: Path, width: int, height: int, *, fit_inside: bool = False) -> bool:
    if Image is None:
        print("Pillow is required to handle EXIF orientation correctly.")
        print("Install it with: pip install Pillow")
        raise SystemExit(1)
    from PIL import ImageOps

    orientation = read_exif_orientation(src)
    print(f"  EXIF orientation tag: {orientation}")

    try:
        with Image.open(src) as img:
            upright = ImageOps.exif_transpose(img)
            if upright.mode in ("RGBA", "P"):
                upright = upright.convert("RGB")

            if upright.height > upright.width:
                upright = upright.rotate(270, expand=True)
                print("  pre-rotated portrait source to landscape for portrait-mounted frame")

            if fit_inside:
                canvas = fit_inside_letterbox(upright, width, height)
                print("  fit inside screen (letterbox)")
            else:
                canvas = crop_to_fill(upright, width, height)
                print("  cropped to fill screen")

            canvas.save(dst, "JPEG", quality=92, optimize=True)
        return True
    except Exception as e:
        print(f"Pillow processing failed for {src}: {e}")
        return False


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


def prepare_photo(src: Path, dst: Path, width: int, height: int, *, fit_inside: bool = False) -> bool:
    print(f"Preparing {src.name} ...")

    tmp_dir = tempfile.mkdtemp(prefix="crowpanel-photo-")
    try:
        if src.suffix.lower() in {".heic", ".heif"}:
            jpeg_tmp = Path(tmp_dir) / "converted.jpg"
            if not run_sips_convert(src, jpeg_tmp, "jpeg"):
                return False
            src_to_process = jpeg_tmp
        else:
            src_to_process = src

        return prepare_with_pillow(src_to_process, dst, width, height, fit_inside=fit_inside)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


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
        "--fit",
        action="store_true",
        help="Shrink images to fit the screen with black bars (default: crop to fill)",
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
        if dst.exists() and dst.stat().st_mtime > f.stat().st_mtime:
            print(f"Skipping {f.name}: {dst.name} already up to date")
            skipped += 1
            continue
        if prepare_photo(f, dst, args.width, args.height, fit_inside=args.fit):
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
