# ESP32 Starter (PlatformIO)

A reusable skeleton for new ESP32 firmware projects. Build, flash, and monitor
in seconds, with two interchangeable frameworks and a host-run unit-test harness.

## Requirements

| Tool | Version | Why |
|---|---|---|
| macOS | 12+ (tested on arm64 / Apple Silicon) | Host OS |
| Python | 3.7+ (3.9+ recommended) | PlatformIO runs on Python |
| `pip` | any recent | Pulled in by the platformio install |
| Homebrew | any recent | Used here to install PlatformIO Core |
| Git | any recent | Cloning the starter |
| VS Code | any recent | Recommended editor (extension available) |
| PlatformIO Core | 6.1+ | CLI build/flash/monitor |
| USB-serial driver | board-specific | Most CP210x / CH340 boards need a driver |
| An ESP32 board | any Espressif ESP32 / ESP32-C3 / ESP32-S3 | Target hardware |

Disk budget: ~3 GB for the espressif32 platform + xtensa toolchain + esptoolpy,
plus ~1.5 GB the first time you build the ESP-IDF environment.

## One-time host setup

### 1. Install PlatformIO Core

Using Homebrew (recommended on macOS):

```sh
brew install platformio
pio --version   # expect: PlatformIO Core, version 6.1.x
```

Alternative install methods (pipx, the VS Code extension's bundled core) are
documented at <https://docs.platformio.org/en/latest/core/installation.html>.

### 2. (Optional) VS Code integration

```sh
# Put the `code` CLI on PATH (one-time):
echo 'export PATH="$PATH:/Applications/Visual Studio Code.app/Contents/Resources/app/bin"' >> ~/.zprofile
source ~/.zprofile

# Install the PlatformIO IDE extension:
code --install-extension platformio.platformio-ide
```

Open this folder in VS Code and the PlatformIO sidebar will pick up
`platformio.ini` automatically.

### 3. USB-serial driver

Plug your board in and check for a serial port:

```sh
ls /dev/cu.* /dev/tty.usb*
```

If nothing appears you may need a driver:

| Chip on your board | Driver |
|---|---|
| CP2102 / CP2104 (most generic ESP32 devkits) | Silicon Labs CP210x VCP driver |
| CH340 / CH341 (LOLIN/D1 R32, Heltec, etc.) | WCH CH34x VCP driver |
| FT230 / FT232 (some older boards) | FTDI VCP driver |
| Native USB (ESP32-S2/S3/C3 CDC) | no driver needed |

macOS Tahoe (15.x) on Apple Silicon: most CP210x/CH340 chips work with the
vendor driver; verify with `ls /dev/cu.usb*` after install + replug.

## Project layout

```
esp32-starter/
├── platformio.ini        # all build/flash/monitor config (per-chip envs)
├── scripts/
│   ├── detect.py         # auto-detect chip + port + matching env
│   └── flash.py          # detect -> build -> upload -> monitor wrapper
├── include/
│   ├── wifi_config.h     # WIFI_SSID / WIFI_PASS placeholder
│   ├── starter.h         # shared helper API
│   └── unity_config.h   # enables unity for native tests
├── src/
│   ├── main.cpp          # Arduino: blink + serial + WiFi
│   ├── starter.cpp       # pure-logic helper (also linked into tests)
│   ├── idf/main.cpp      # ESP-IDF equivalent (blink via FreeRTOS task)
│   └── CMakeLists.txt    # selects only idf/ when building the IDF env
├── test/
│   └── test_starter.cpp  # Unity tests, run on the host
└── .vscode/settings.json # recommended editor config
```

## Environments

`platformio.ini` defines one env per chip variant plus a host test env:

| Env | Chip | Framework | Use for |
|---|---|---|---|
| `esp32dev-arduino` (default) | original ESP32 | Arduino-ESP32 | DevKitC v1, NodeMCU-32S, WROOM-32 |
| `esp32-s3-arduino` | ESP32-S3 | Arduino-ESP32 | S3 DevKitC-1 (native USB CDC) |
| `esp32dev-idf` | original ESP32 | ESP-IDF | Lower-level / FreeRTOS / production firmware |
| `crowpanel-5` | ESP32-S3 (Elecrow 5" HMI V3.0) | Arduino-ESP32 | 800x480 RGB + GT911 touch + LVGL 9.1.0 |
| `native` | host (your Mac) | GCC + Unity | Host unit tests |

To add a new chip variant: copy an existing `[env:...]` section, change `board`
and `BLINK_PIN`, and add a mapping in `scripts/detect.py` (`CHIP_TO_ENV`).

Common `board` aliases:

```
esp32dev                 generic ESP32 (DevKitC, NodeMCU-32S, WROOM-32)
esp32-s3-devkitc-1       ESP32-S3 DevKitC-1
esp32-c3-devkitm-1       ESP32-C3
esp32-wroom-32
d32_pro                  Lolin D32 Pro
```

## Build, flash, monitor

### Recommended: auto-detect (no need to remember which env)

```sh
python3 scripts/flash.py                 # detect chip, build, upload, monitor
python3 scripts/flash.py --no-monitor    # build + upload only
python3 scripts/flash.py --build-only   # build only (no upload)
python3 scripts/flash.py --wifi MyNet secret   # inject WiFi credentials
python3 scripts/flash.py --port /dev/cu.usbmodem1201   # override port
python3 scripts/detect.py                # just print PORT= / ENV= / CHIP=
```

The scripts probe the connected board with esptool, map the detected chip to
the matching PlatformIO env, then dispatch `pio run -e <env> -t upload -t monitor`
with `--upload-port` / `--monitor-port` set explicitly. If multiple USB-serial
ports are present, pass `--port`. If no TTY is attached (e.g. piped output),
`flash.py` skips the monitor step automatically to avoid a termios crash.

### Manual: explicit env

```sh
pio run -e esp32-s3-arduino                         # build only
pio run -e esp32-s3-arduino -t upload -t monitor    # build + flash + monitor
pio run -e esp32dev-idf -t upload -t monitor        # IDF variant
pio device list                                     # list serial ports
```

VS Code: pick the env from the PlatformIO toolbar dropdown, then use the
✓ Build → → Upload → plug-icon Monitor buttons.

## WiFi

`src/main.cpp` reads `WIFI_SSID` / `WIFI_PASS` macros. Three ways to supply them:

1. Edit `include/wifi_config.h` (don't commit real secrets — see `.gitignore`).
2. Pass via build flags inline:
   ```sh
   pio run -e esp32dev-arduino -- -DWIFI_SSID='"MyNet"' -DWIFI_PASS='"secret"'
   ```
3. Via environment variables (already wired in `platformio.ini`):
   ```sh
   WIFI_SSID="MyNet" WIFI_PASS="secret" pio run -t upload -t monitor
   ```

If left at `CHANGE_ME` the firmware skips the WiFi step and runs offline.

## Unit tests

Pure-logic helpers live in `src/starter.cpp` (no Arduino headers) so the host
compiler can build them. Tests in `test/` use the Unity framework.

```sh
pio test -e native       # host unit tests (no hardware, ~0.5s)
```

> **Always pass `-e native`.** Bare `pio test` targets `default_envs`
> (`esp32dev-arduino`), which is build/flash-only and will fail to link because
> the Arduino framework expects `setup()`/`loop()`. PlatformIO Core 6.1.x has no
> separate `test_default_envs` option, so the `-e native` flag is required.

Add new helpers to `src/starter.cpp` + `include/starter.h` and a matching test
file under `test/`. The `native` env only compiles `starter.cpp` from `src/`
(see `build_src_filter`), so keep hardware dependencies out of helpers you want
to unit-test.

The `esp32dev-arduino` and `esp32dev-idf` environments are build/flash-only for
now. Running tests *on the chip* (`pio test -e esp32dev-arduino`) is possible
but requires test files written in Arduino `setup()`/`loop()` style that stream
results over Serial; scaffold one when you have hardware-dependent code worth
testing on the target.

## CrowPanel 5.0" HMI (Elecrow, V3.0 hardware)

The `crowpanel-5` env targets the Elecrow 5" ESP32-S3 HMI display: 800x480 RGB
TFT (ILI6122/ILI5960) via LovyanGFX, GT911 capacitive touch (reset through a
PCA9557 IO expander — the V3.0-specific timing sequence lives in
`src/crowpanel/touch.cpp`), and an LVGL 9.1.0 UI generated by SquareLine Studio.
A DHT20 sensor on the I2C bus feeds temperature/humidity labels.

Source layout (kept isolated from the starter's blink/WiFi `main.cpp`):

```
boards/esp32-s3-devkitc-1-myboard.json   custom S3 board (PSRAM, QIO OPI, 4MB flash)
boards/huge_app.csv                      3 MB app0 partition for the LVGL images
include/crowpanel/{touch,dht20,ui}.h
src/crowpanel/main.cpp                   display + LVGL + touch + DHT20 setup/loop
src/crowpanel/{touch,dht20,ui,ui_Screen1}.cpp/c
src/crowpanel/ui_img_*.c                 embedded PNGs (~5.7 MB of source)
lib/LovyanGFX/                           vendored, gitignored (see below)
```

One-time setup — vendor the patched LovyanGFX:

The Elecrow example ships a modified LovyanGFX whose `Bus_RGB` exposes
`getFrameBuffer()` / `presentFrameBuffer()`. The upstream registry version lacks
those, so the env depends on the bundled copy from the sibling example repo.
With the example repo cloned at `../CrowPanel-5.0-HMI-ESP32-Display-800x480`:

```sh
python3 scripts/setup_crowpanel.py                 # copies into lib/LovyanGFX
python3 scripts/setup_crowpanel.py --force         # re-copy after upstream change
```

Build, flash, monitor (auto-detect picks the generic S3 env, so name the env
explicitly):

```sh
pio run -e crowpanel-5                             # build (~1.5 min after first run)
pio run -e crowpanel-5 -t upload -t monitor        # flash + serial monitor
```

If `pio device list` shows the board as `/dev/cu.usbmodem*` (native S3 USB-CDC),
that port is used automatically. Hold BOOT, tap RST, release BOOT to enter the
bootloader if esptool can't connect.

The env uses PlatformIO's `pioarduino/platform-espressif32` v55.03.38 (Arduino
core 3.x) pinned in `platformio.ini` — separate from the `esp32-base` platform
line so it does not perturb the other envs. `build_src_filter` excludes the
starter's `main.cpp`/`starter.cpp`/`idf/` so only `src/crowpanel/` is compiled
into this env.

## Cloning into a new project

```sh
# from this repo
cp -r esp32-starter ../my-new-firmware
cd ../my-new-firmware
rm -rf .pio            # clear any cached build artifacts
pio run                # first build downloads toolchains (~minutes)
```

Then edit `platformio.ini` to rename the `[env:...]` sections, drop the env you
won't use, and start coding in `src/main.cpp`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `pio: command not found` | `brew install platformio`, or open a fresh shell to pick up PATH |
| First build is very slow | Normal — it downloads the xtensa toolchain + framework (~700 MB) |
| `A fatal error occurred: Failed to connect to ESP32` | Hold BOOT while pressing EN/RST, then release EN, then BOOT; check `/dev/cu.*` with `pio device list` |
| No serial port appears | Install the USB-serial driver for your board's chip (see table above) |
| `monitor_filters: esp32_exception_decoder` needs `pyelftools` | `pip install pyelftools` in your pio penv, or use the bundled python |
| Native tests fail to compile | Make sure new helpers in `src/` don't include `Arduino.h` or ESP-IDF headers |
| Flash size mismatch on IDF env | Edit `sdkconfig.defaults` or run `pio run -e esp32dev-idf -t menuconfig` |

## Photo sync workflow (Google Drive host sync)

This project includes a host-side Google Drive sync option. It is the simplest
reliable path because phone photos must be resized and converted to baseline
JPEGs before the ESP32 can display them. The conversion happens on your Mac,
not on the device.

Prerequisites:

1. A Google Cloud project with the Google Drive API enabled.
2. An OAuth 2.0 **Desktop app** credential downloaded as
   `client_secret_*.json`.
3. Python packages:
   ```sh
   pip install google-auth-oauthlib google-api-python-client
   ```

One-time authentication:

```sh
python3 scripts/gdrive_auth.py
```

This opens a browser flow and saves `gdrive_refresh_token.json` in the project
root. Keep both credential files secret and do not commit them (they are
already ignored by `.gitignore`).

Day-to-day sync (insert the SD card first):

```sh
python3 scripts/gdrive_sync.py \
  --folder-id 1M1MkkdQ9vKmXTkDvweV3kmGRKdZQtoU_ \
  --sd-mount /Volumes/SDCARD
```

The script downloads new images from the Drive folder to `SDCARD/upload/`, then
runs `prepare_photos.py` to convert them to 800×480 baseline JPEGs in
`SDCARD/photos/`. The SD card is ejected automatically; add `--no-eject` to
keep it mounted.

To preview what would be downloaded without writing anything:

```sh
python3 scripts/gdrive_sync.py \
  --folder-id 1M1MkkdQ9vKmXTkDvweV3kmGRKdZQtoU_ \
  --sd-mount /Volumes/SDCARD \
  --dry-run
```

For fully automatic “upload from anywhere” sync without a local computer, a
future option is a small cloud proxy (for example a Cloudflare Worker) that
reads the Drive folder, prepares the images, and exposes simple HTTPS
endpoints the frame can poll directly.

## Versions this starter was verified with

- PlatformIO Core 6.1.19
- espressif32 platform 7.0.1
- Arduino-ESP32 3.x (resolved by PlatformIO)
- ESP-IDF 6.0.1 (resolved by PlatformIO)
- macOS 26 (Tahoe), Apple Silicon