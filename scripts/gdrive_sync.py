#!/usr/bin/env python3
"""Sync photos from a Google Drive shared folder to the SD card /photos dir.

Uses the refresh token created by gdrive_auth.py. Images are downloaded to a
staging directory, then processed by prepare_photos.py so they are resized to
800x480 baseline JPEGs before being copied to the SD card.

A manifest at <sd-mount>/sync_state.json records the last-synced state (id,
name, modifiedTime) of every Drive file. Unchanged files are skipped, and
files no longer present in the Drive folder are removed from upload/ and
photos/ so the SD card mirrors the Drive folder.
"""

import argparse
import hashlib
import http.client
import json
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
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
                fields=(
                    "nextPageToken, files(id, name, mimeType, modifiedTime, "
                    "md5Checksum, size)"
                ),
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


DOWNLOAD_CHUNK_SIZE = 10 * 1024 * 1024  # bytes fetched per HTTP round-trip
DOWNLOAD_TIMEOUT_SECS = 300  # socket timeout for the media download requests
MAX_CHUNK_RETRIES = 3  # consecutive failures tolerated before giving up
RETRY_BACKOFF_SECS = 5  # wait between chunk retries, scaled by failure count
PROGRESS_STEP_PCT = 10  # log a Downloaded N% line every N percent


def _set_http_timeout(request):
    """Give the media request a socket timeout long enough for slow links.

    build() wraps httplib2 in AuthorizedHttp, which exposes `timeout` as a
    property forwarded to the wrapped Http object, so setting it there is
    enough. If the HTTP client rejects the attribute, chunk-level retries in
    download_file() backstop stalled reads instead.
    """
    http = getattr(request, "http", None)
    if http is None:
        return
    try:
        http.timeout = DOWNLOAD_TIMEOUT_SECS
    except (AttributeError, TypeError, ValueError):
        pass


def _transient_network_errors():
    """Errors raised by next_chunk() that a chunk retry can recover from.

    MediaIoBaseDownload advances its Range offset only after a chunk is fully
    received, so any of these leaves the file and progress at the last chunk
    boundary and a retry refetches from there.
    """
    import httplib2

    return (
        TimeoutError,  # socket read timeout (socket.timeout on Python 3.10+)
        socket.timeout,
        ssl.SSLError,
        OSError,  # ECONNRESET, EPIPE, "Network is down", ...
        httplib2.ServerNotFoundError,
        http.client.HTTPException,  # IncompleteRead / dropped connection
    )


def download_file(service, file_id, dest_path, label=None):
    """Download one Drive file to dest_path with chunked, retrying reads.

    On permanent failure the partial file is deleted so nothing half-written
    can reach upload/ or the manifest, and the exception is re-raised.
    """
    name = label or file_id
    request = service.files().get_media(fileId=file_id)
    _set_http_timeout(request)
    try:
        from googleapiclient.http import MediaIoBaseDownload
    except ImportError as exc:
        print("Missing google-api-python-client.")
        raise SystemExit(exc)

    retry_errors = _transient_network_errors()
    try:
        with open(dest_path, "wb") as f:
            downloader = MediaIoBaseDownload(f, request, chunksize=DOWNLOAD_CHUNK_SIZE)
            done = False
            failures = 0
            last_logged = 0
            while not done:
                try:
                    status, done = downloader.next_chunk()
                except retry_errors as exc:
                    failures += 1
                    if failures >= MAX_CHUNK_RETRIES:
                        print(
                            f"Download of {name} failed after "
                            f"{MAX_CHUNK_RETRIES} consecutive "
                            f"{type(exc).__name__}s: {exc}"
                        )
                        raise
                    print(
                        f"Retrying chunk for {name} (attempt {failures + 1}/"
                        f"{MAX_CHUNK_RETRIES}) after {type(exc).__name__}: {exc}"
                    )
                    time.sleep(RETRY_BACKOFF_SECS * failures)
                    continue
                # A received chunk (even mid-file) resets the retry window.
                failures = 0
                pct = 100 if done else int(status.progress() * 100)
                if pct > last_logged and (done or pct - last_logged >= PROGRESS_STEP_PCT):
                    print(f"Downloaded {pct}%")
                    last_logged = pct
    except Exception:
        try:
            Path(dest_path).unlink()
        except OSError:
            pass
        raise


MANIFEST_NAME = "sync_state.json"


def _md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def _is_unchanged(f, entry, upload_dir):
    """Return True when the local copy already matches Drive content.

    A file is unchanged when the Drive md5Checksum matches the manifest's
    stored checksum and the local upload/ file exists with the matching size.
    If Drive does not expose a checksum (rare), fall back to the previous
    modifiedTime comparison plus local file existence.
    """
    if entry is None:
        return False
    drive_md5 = f.get("md5Checksum")
    drive_size = f.get("size")
    local_path = upload_dir / f["name"]
    if not local_path.is_file():
        return False
    if drive_md5 and entry.get("md5Checksum") == drive_md5:
        local_size = str(local_path.stat().st_size)
        if drive_size is None or local_size == str(drive_size):
            return True
    if entry.get("modifiedTime") == f["modifiedTime"]:
        return True
    return False


def load_manifest(manifest_path):
    """Load {file_id: {name, modifiedTime, md5Checksum, size}}; missing/corrupt counts as empty."""
    if not manifest_path.exists():
        return {}
    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        files = data.get("files")
        if not isinstance(files, dict):
            raise ValueError("manifest 'files' entry is not an object")
        return files
    except (json.JSONDecodeError, OSError, ValueError):
        print(f"Warning: {manifest_path} is corrupt; treating as empty (full sync).")
        return {}


def save_manifest(manifest_path, files_by_id):
    """Atomically write the manifest as small, human-readable JSON."""
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = manifest_path.with_suffix(".json.tmp")
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump({"files": files_by_id}, f, indent=2, sort_keys=True)
        f.write("\n")
    tmp_path.replace(manifest_path)


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
    manifest_path = sd_mount / MANIFEST_NAME

    creds = load_credentials()
    service = build_service(creds)

    files = list_image_files(service, args.folder_id)
    print(f"Found {len(files)} image(s) in Drive folder {args.folder_id}")

    manifest = load_manifest(manifest_path)
    listing = {f["id"]: f for f in files}

    to_download = []
    unchanged = []
    for f in files:
        entry = manifest.get(f["id"])
        if _is_unchanged(f, entry, upload_dir):
            unchanged.append(f)
        else:
            to_download.append(f)
    removed_ids = [fid for fid in manifest if fid not in listing]

    if not files and not manifest:
        print("Nothing to sync.")
        return

    if args.dry_run:
        for f in to_download:
            print(f"Would download: {f['name']}")
        for f in unchanged:
            print(f"Would skip unchanged: {f['name']}")
        for fid in removed_ids:
            print(f"Would remove (deleted from Drive): {manifest[fid].get('name', fid)}")
        if not to_download and not removed_ids:
            print("All files up to date.")
        return

    upload_dir.mkdir(parents=True, exist_ok=True)
    photos_dir.mkdir(parents=True, exist_ok=True)

    for f in unchanged:
        print(f"skipping unchanged: {f['name']}")

    with tempfile.TemporaryDirectory(prefix="gdrive_sync_") as tmp:
        tmp_path = Path(tmp)
        for f in to_download:
            dest = tmp_path / f["name"]
            print(f"Downloading {f['name']} ...")
            download_file(service, f["id"], dest, label=f["name"])
            shutil.copy2(dest, upload_dir / f["name"])
            manifest[f["id"]] = {
                "name": f["name"],
                "modifiedTime": f["modifiedTime"],
                "md5Checksum": _md5(dest),
                "size": str(dest.stat().st_size),
            }
            save_manifest(manifest_path, manifest)

    for fid in removed_ids:
        entry = manifest.pop(fid)
        name = entry.get("name", "")
        targets = (
            [upload_dir / name, photos_dir / (Path(name).stem + ".jpg")]
            if name
            else []
        )
        for target in targets:
            if target.exists():
                print(f"Removing {target.name} (deleted from Drive)")
                target.unlink()
        save_manifest(manifest_path, manifest)

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
