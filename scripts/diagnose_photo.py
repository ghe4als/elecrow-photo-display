#!/usr/bin/env python3
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


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


def inspect(path: Path, label: str):
    try:
        from PIL import Image
        with Image.open(path) as img:
            exif = img.getexif()
            orient = exif.get(274, 1)
            print(f"{label}: size={img.size} mode={img.mode} orientation={orient}")
    except Exception as e:
        print(f"{label}: could not inspect ({e})")


def diagnose(src: Path):
    src = Path(src)
    if not src.exists():
        print(f"File not found: {src}")
        sys.exit(1)

    inspect(src, "source")

    with tempfile.TemporaryDirectory(prefix="photo_diag_") as tmp:
        tmp_path = Path(tmp)
        if src.suffix.lower() in {".heic", ".heif"}:
            jpeg_tmp = tmp_path / "sips.jpg"
            if not run_sips_convert(src, jpeg_tmp, "jpeg"):
                sys.exit(1)
            inspect(jpeg_tmp, "after sips to jpeg")
            process_src = jpeg_tmp
        else:
            process_src = src

        sys.path.insert(0, str(Path(__file__).parent))
        from prepare_photos import prepare_with_pillow

        out = tmp_path / "final.jpg"
        if prepare_with_pillow(process_src, out, 800, 480):
            inspect(out, "final 800x480")
            preview = Path("/tmp/crowpanel_diag_preview.jpg")
            shutil.copy2(out, preview)
            print(f"Final saved for inspection: {preview}")
        else:
            print("prepare_with_pillow failed")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path to photo>")
        sys.exit(1)
    diagnose(sys.argv[1])
