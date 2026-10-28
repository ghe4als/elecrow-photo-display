#!/usr/bin/env python3
"""Sync photos from a Google Drive shared folder to the SD card /photos dir.

Uses the refresh token created by gdrive_auth.py. Images are downloaded to a
staging directory, then processed by prepare_photos.py so they are resized to
800x480 baseline JPEGs before being copied to the SD card.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def load_credentials():
    token_path = Path("gdrive_refresh_token.json")
    if not token_path.exists():
        print(
            "gdrive_refresh_token.json not found. Run scripts/gdrive_auth.py first."
        )
        sys.exit(1)
    with open(token_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    try:
        from google.oauth2.credentials import Credentials
    except ImportError as exc:
        print("Missing Python package. Install with:")
        print("  pip install google-auth-oauthlib google-api-python-client")
        raise SystemExit(exc)
    return Credentials(
        token=None,
        refresh_token=data["refresh_token"],
        token_uri=data["token_uri"],
        client_id=data["client_id"],
        client_secret=data["client_secret"],
    )


def build_service(creds):
    try:
        from googleapiclient.discovery import build
    except ImportError as exc:
        print("Missing Python package. Install with:")
        print("  pip install google-api-python-client")
        raise SystemExit(exc)
    return build("drive", "v3", credentials=creds, cache_discovery=False)


def list_image_files(service, folder_id):
    query = f"'{folder_id}' in parents and trashed = false and mimeType contains 'image/'"
    files = []
    page_token = None
    while True:
        response = (
            service.files()
            .list(
                q=query,
                spaces="drive",
                fields="nextPageToken, files(id, name, mimeType, modifiedTime)",
                pageToken=page_token,
                pageSize=100,
            )
            .execute()
        )
        files.extend(response.get("files", []))
        page_token = response.get("nextPageToken")
        if not page_token:
            break
    return files


def download_file(service, file_id, dest_path):
    request = service.files().get_media(fileId=file_id)
    with open(dest_path, "wb") as f:
        downloader = None
        try:
            from googleapiclient.http import MediaIoBaseDownload
        except ImportError as exc:
            print("Missing google-api-python-client.")
            raise SystemExit(exc)
        downloader = MediaIoBaseDownload(f, request)
        done = False
        while not done:
            _, done = downloader.next_chunk()


def main():
    parser = argparse.ArgumentParser(
        description="Sync a Google Drive folder of images to an SD card."
    )
    parser.add_argument(
        "--folder-id",
        required=True,
        help="Google Drive folder ID (from the URL after /folders/)",
    )
    parser.add_argument(
        "--sd-mount",
        required=True,
        help="Path to the mounted SD card root (e.g. /Volumes/SD_CARD)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="List what would be downloaded without writing anything.",
    )
    parser.add_argument(
        "--no-eject",
        action="store_true",
        help="Keep the SD card mounted after processing (default: eject).",
    )
    args = parser.parse_args()

    sd_mount = Path(args.sd_mount)
    upload_dir = sd_mount / "upload"
    photos_dir = sd_mount / "photos"

    creds = load_credentials()
    service = build_service(creds)

    files = list_image_files(service, args.folder_id)
    print(f"Found {len(files)} image(s) in Drive folder {args.folder_id}")

    if not files:
        print("Nothing to sync.")
        return

    if args.dry_run:
        for f in files:
            print(f"Would download: {f['name']}")
        return

    upload_dir.mkdir(parents=True, exist_ok=True)
    photos_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="gdrive_sync_") as tmp:
        tmp_path = Path(tmp)
        for f in files:
            dest = tmp_path / f["name"]
            print(f"Downloading {f['name']} ...")
            download_file(service, f["id"], dest)
            shutil.copy2(dest, upload_dir / f["name"])

    prepare_script = Path(__file__).with_name("prepare_photos.py")
    if not prepare_script.exists():
        print(f"prepare_photos.py not found at {prepare_script}; leaving files in upload/")
        print("Sync complete.")
        return

    print("Running prepare_photos.py ...")
    cmd = [
        sys.executable,
        str(prepare_script),
        str(sd_mount),
    ]
    if args.no_eject:
        cmd.append("--no-eject")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("prepare_photos.py failed.")
        sys.exit(result.returncode)

    print("Sync complete.")


if __name__ == "__main__":
    main()
