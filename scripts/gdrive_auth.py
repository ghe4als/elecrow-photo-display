#!/usr/bin/env python3
"""One-time Google OAuth2 flow for a Desktop app credential.

Saves a refresh token that can be reused by scripts/gdrive_sync.py to access a
private Google Drive shared folder without repeated browser login.
"""

import json
import os
import sys
from pathlib import Path


def find_client_secret():
    cwd = Path.cwd()
    matches = sorted(cwd.glob("client_secret_*.json"))
    if not matches:
        raise FileNotFoundError(
            "No client_secret_*.json found. Download it from Google Cloud Console "
            "(Credentials > OAuth 2.0 Client IDs > Download JSON) and place it "
            f"in {cwd}"
        )
    return matches[0]


def main():
    try:
        from google_auth_oauthlib.flow import InstalledAppFlow
    except ImportError as exc:
        print("Missing Python package. Install with:")
        print("  pip install google-auth-oauthlib google-api-python-client")
        raise SystemExit(exc)

    client_secret_path = find_client_secret()
    output_path = Path("gdrive_refresh_token.json")

    scopes = ["https://www.googleapis.com/auth/drive.readonly"]
    flow = InstalledAppFlow.from_client_secrets_file(
        str(client_secret_path), scopes
    )
    credentials = flow.run_local_server(port=0)

    refresh_data = {
        "refresh_token": credentials.refresh_token,
        "client_id": credentials.client_id,
        "client_secret": credentials.client_secret,
        "token_uri": credentials.token_uri,
    }

    if not refresh_data["refresh_token"]:
        raise RuntimeError(
            "Google did not return a refresh token. You may have already authorized "
            "this app. Visit https://myaccount.google.com/permissions and remove it, "
            "then run this script again."
        )

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(refresh_data, f, indent=2)

    print(f"Saved refresh token to {output_path}")
    print("Keep this file secret and do not commit it.")


if __name__ == "__main__":
    main()
