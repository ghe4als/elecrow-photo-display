# Phase 3: Google Drive Private Shared-Folder Sync

## Goal
Sync photos from a private Google Drive shared folder to the SD card `/photos` directory, so the frame can show new pictures without manually copying files.

## Architecture
- Use Google OAuth2 **Desktop app** credentials.
- A host Python script (`scripts/gdrive_auth.py`) performs the one-time browser OAuth flow and saves a `gdrive_refresh_token.json`.
- A host sync script (`scripts/gdrive_sync.py`) uses the refresh token to list the Drive folder, download new images to a local staging directory, convert/process them with the existing `prepare_photos.py` pipeline, and write them to the SD card `/photos` directory.
- Optional future step: device-side direct WiFi download from a pre-processed Drive subfolder.

## Files to add
- `scripts/gdrive_auth.py` — OAuth2 desktop-app flow, refresh-token storage.
- `scripts/gdrive_sync.py` — list, download, and process photos from Drive.
- Update `scripts/prepare_photos.py` so it can be invoked on a staging directory by `gdrive_sync.py`, and use Pillow to apply EXIF orientation before resizing.

## Files to edit
- `.gitignore` — ignore `gdrive_refresh_token.json` and `client_secret_*.json`.
- `README.md` or docs — usage and credential setup steps.

## Verification
1. User creates Google Cloud OAuth2 Desktop credentials and downloads `client_secret_*.json`.
2. Run `scripts/gdrive_auth.py` to open a browser and save `gdrive_refresh_token.json`.
3. Run `scripts/gdrive_sync.py --folder-id <id> --sd-mount /Volumes/SD_CARD` and confirm new photos appear in `/photos`.
4. Build `crowpanel-5` firmware and verify it still cycles through `/photos`.

## Out of scope for Phase 3
- Device-side direct Google Drive download (host-side sync first; device-side later if needed).
- Automatic sync scheduling on the device itself.
