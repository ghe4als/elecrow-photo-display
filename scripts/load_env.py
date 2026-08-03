#!/usr/bin/env python3
"""PlatformIO extra_script: load variables from a gitignored .env file and
inject them as build flags (e.g. -DWIFI_SSID=\"...\").

Why: keeps secrets out of git, out of the shell environment, and out of
platformio.ini while still making them available to every build of every
env without per-clone shell setup. Edit one file (.env), build normally.

Resolution order for each VAR found in .env:
    platformio.ini  -DVAR=\"${sysenv.VAR}\"   (already in the env's build_flags)
        -> PlatformIO expands ${sysenv.VAR} from os.environ at build time
        -> this script exports .env vars into os.environ before that expansion

Usage in platformio.ini:
    extra_scripts = pre:scripts/load_env.py

The .env file is optional. If it doesn't exist, the script silently does
nothing (the firmware falls back to the CHANGE_ME sentinel in
include/wifi_config.h and skips the WiFi connect).

Env file format: KEY=VALUE lines, # comments, blank lines ignored.
Quotes around values are stripped. No shell expansion is performed.
"""
from __future__ import annotations

import os
import sys

# PlatformIO executes extra_scripts inside an SCons context where the build
# environment is retrieved via Import("env"). Outside PlatformIO (e.g. when
# testing the parser standalone) env is None and we just skip the inject.
Env = None
_import_error = None
try:
    # Import() binds the SCons env into the local namespace as 'env'.
    Import("env")  # type: ignore[name-defined]
    Env = env  # type: ignore[name-defined]
except Exception as e:
    _import_error = repr(e)

# __file__ is undefined when PlatformIO exec()s the script; use the project_dir
# global PlatformIO injects, then fall back to cwd.
_project_dir = globals().get("project_dir")
if _project_dir:
    project_dir = _project_dir
elif Env is not None:
    project_dir = Env.subst("$PROJECT_DIR")
else:
    try:
        project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        project_dir = os.getcwd()

ENV_FILE = os.path.join(project_dir, ".env")


def parse_env(path: str) -> dict[str, str]:
    result: dict[str, str] = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            if key:
                result[key] = value
    return result


def main() -> None:
    sys.stderr.write(f"[load_env] Env={Env!r} import_error={_import_error!r} ENV_FILE={ENV_FILE} exists={os.path.isfile(ENV_FILE)}\n")
    if not os.path.isfile(ENV_FILE):
        # Silent: a fresh clone has no .env, and that's fine. The firmware's
        # CHANGE_ME sentinel handles the "no creds yet" case gracefully.
        return
    secrets = parse_env(ENV_FILE)
    sys.stderr.write(f"[load_env] parsed secrets: {list(secrets.keys())}\n")
    # Export into os.environ so platformio.ini's ${sysenv.VAR} expansions
    # pick them up. Only set keys that aren't already present in the real
    # environment - real env vars win, so CI/secret managers can override.
    for key, value in secrets.items():
        os.environ.setdefault(key, value)
    # Also inject directly as build flags via the PlatformIO env object.
    # ${sysenv.VAR} in platformio.ini is expanded BEFORE pre: scripts run,
    # so the setdefault above doesn't help in-file envs. Direct Append is
    # authoritative and works regardless of expansion timing.
    if Env is not None and hasattr(Env, "Append"):
        extra = []
        for key, value in secrets.items():
            # Escape embedded quotes/backslashes for the C preprocessor.
            escaped = value.replace("\\", "\\\\").replace('"', '\\"')
            extra.append((key, f'\\"{escaped}\\"'))
        sys.stderr.write(f"[load_env] appending CPPDEFINES: {extra}\n")
        if extra:
            Env.Append(CPPDEFINES=extra)
    else:
        sys.stderr.write("[load_env] no Env.Append available\n")


main()