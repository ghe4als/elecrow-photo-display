# CrowPanel Photo Display — Progress Log

Date: 2026-11-09 (updated 2026-11-16)
Status: flicker currently not reproducible after a week powered off; weather card resolved

## Goal
Get the Elecrow CrowPanel 5" ESP32-S3 photo frame running reliably with a portrait-oriented info card showing date, time, outdoor weather, and indoor climate. The panel is wall-mounted in portrait mode (power plug at the bottom). Phone photos are pre-rotated to landscape 800x480 by `scripts/prepare_photos.py` so they fill the panel when it is mounted upright.

## What is working

- **Slideshow core**: photos load from SD `/photos`, shuffle, advance every 10 s, touch swipes work, pause works.
- **Portrait info card**: renders upright via an off-screen 480x800 canvas drawn with LVGL, then rotated into the 800x480 framebuffer using `lv_draw_sw_rotate()`. Avoids the earlier `transform_rotation` hang.
- **Weather card content**:
  - Date/time (falls back to placeholder when NTP unavailable).
  - Outdoor weather from Open-Meteo.
  - Indoor climate from DHT20, or placeholder when sensor is absent.
  - Open-Meteo attribution at the bottom.
- **SD card reliability**: multi-frequency retry (4 MHz / 1 MHz / 400 kHz) is committed to `main`.
- **Weather fetch**: `weather.cpp` uses `http.getString()` and is committed to `main`.
- **Build/flash**: `pio run -e crowpanel-5` succeeds and flashes via `/dev/cu.usbserial-110` when the CH340 enumerates.

## What is not working

- **Photo transition flicker**: a visible flicker/flash occurs when the slideshow advances to the next JPEG. The user saw it occasionally go away, but it has returned consistently in recent builds.
- **Live weather/NTP**: the device is offline because the configured AP is not currently reachable. Weather and NTP cannot be validated until the network is available.
- **CH340 serial instability**: the port re-enumerates between `/dev/cu.usbserial-110` and `/dev/tty.usbserial-120` (and sometimes disappears entirely), making serial logging and flashing intermittent.
- **DHT20 sensor**: absent; "DHT20 init failed" log line is expected. User now has a DHT20 module; see hardware notes below.

## Discoveries

1. **Never use LVGL widget rotation for this panel.** `lv_obj_set_style_transform_rotation` on a full-screen image creates a transform layer and reliably hangs/crashes the ESP32-S3. The safe path is to render into an off-screen buffer at the target orientation and then display that buffer unrotated.
2. **`lv_draw_sw_rotate()` works for the info card.** It is LVGL's own software rotator and handles stride correctly. The canvas-based portrait card now renders clean text.
3. **JPEG decode is expensive on every transition.** LVGL's TJPGD decoder decodes JPEGs in MCU tiles during the render pass. Even with `LV_CACHE_DEF_SIZE=4MB`, the first display of a new file-source JPEG decompressed while rendering, which correlates with the observed flicker.
4. **Off-screen canvas decode made it worse.** Drawing the JPEG into a persistent PSRAM canvas via `lv_draw_image()` and then pointing the visible image at that canvas produced *more* flicker, not less. Possible reasons: extra full-screen render pass, canvas/stride mismatch, or memory-bandwidth contention.
5. **Panel clock and fade are not the main fix.** Tried PCLK at 12 MHz and 15 MHz, fade duration 0 ms and 350 ms. None eliminated the flicker.
6. **Direct file-source loading is the least-bad software path.** It keeps the code simple and memory footprint lower, but still flickers.
7. **Raw RGB565 file loading is a backup theory, not the priority.** The current code has a fallback: if a `.raw` file exists alongside the `.jpg`, it reads the raw 768 KB RGB565 framebuffer from SD into PSRAM and points `lv_image` at that buffer. This bypasses JPEG decode entirely, but the user's preference is to first reproduce the flicker-free JPEG state we saw earlier.

## Attempted fixes

| Approach | Result |
|----------|--------|
| Label-based info overlay | Worked but wrong orientation for portrait mount. |
| `transform_rotation` on image widget | Hang/crash — abandoned. |
| Canvas + manual glyph renderer for card | Crashed/locked up on weather card. |
| Canvas + `lv_draw_sw_rotate()` for card | **Weather card works, clean text.** |
| Persistent off-screen canvas for photo decode | Worse flicker. |
| Disable fade animation (hard cut) | Slightly reduced at times, not resolved. |
| PCLK 15 MHz | No clear improvement; reverted to 12 MHz. |
| PCLK 12 MHz + fade 350 ms | Still flickered. |
| `LV_CACHE_DEF_SIZE=4MB` | Still flickered. |
| Raw RGB565 file loader in firmware | Code added; backup plan if JPEG flicker cannot be reproduced as fixed. |

## Current code state

- `src/crowpanel/photo_viewer.cpp`: portrait info card using canvas + `lv_draw_sw_rotate`; raw RGB565 photo loader with JPEG fallback; hard-cut photo transitions; `INFO_EVERY_N_PHOTOS = 10`; realistic offline placeholders.
- `src/crowpanel/main.cpp`: PCLK restored to 12 MHz.
- `scripts/prepare_photos.py`: now writes both `.jpg` and `.raw` (768 KB RGB565 binary) for each photo.
- `platformio.ini`: `LV_CACHE_DEF_SIZE=4MB` added plus the fonts needed for the card.
- `.env`: WiFi credentials configured.

## Next step (for tomorrow)

Priority is to reproduce the flicker-free state the user saw, not to explore the raw-file theory first.

1. Review the exact commits and build artifacts from the session to identify which combination produced no flicker. The user reported no flicker after the first off-screen-decode + placeholder/INFO=2 + 15 MHz + hard-cut build, so bisect toward that configuration while keeping the working portrait card.
2. Key variables to bisect:
   - PCLK: 12 MHz vs 15 MHz
   - Fade duration: 0 ms (hard cut) vs 350 ms
   - Photo source: direct file vs off-screen canvas decode
   - Scale/rotation calls on `g_image`
   - `LV_CACHE_DEF_SIZE` enabled vs disabled
3. Flash one variable at a time and verify on hardware.
4. If the flicker-free state cannot be reproduced with JPEGs, then consider the `.raw` RGB565 path as a fallback.

## Notes for resuming

- The weather card should be tested again once the travel router is available and NTP syncs. The placeholder path is verified; the live-data path is not.
- The `.raw` loader is already in the code as a fallback but is not the preferred next step. It reads the file synchronously with `SD.open()` and `File.read()` directly into PSRAM, then calls `esp_cache_msync()` and `lv_image_set_src()` on a static `lv_image_dsc_t`.
- Commit `aa398fe` is the last known mostly-stable baseline (before the portrait-card rework). Diff against it if needed.
- Recent commits on `main` include the portrait card and the raw loader; they are not yet verified flicker-free.

## Hardware notes

- **CrowPanel 5" V3.0 I2C expansion header**: provides `SDA` (GPIO19), `SCL` (GPIO20), **3.3 V**, and **GND**. It does **not** provide 5 V.
- **DHT20 power**: the DHT20 IC itself works at 3.3 V to 5 V, and the firmware uses I2C address `0x38`. If the breakout module is labeled "5 V only," you can still use it by powering VCC from an external 5 V source, but you **must** connect the module's GND to the CrowPanel's GND so the I2C signals share a common reference.
- **I2C logic level**: the ESP32-S3 I/O pins are 3.3 V. If the DHT20 module outputs 5 V logic on SDA/SCL, you need a level shifter. Most small DHT20 modules have an onboard regulator and 3.3 V I2C logic, so connecting SDA/SCL directly is usually safe — verify with the module's datasheet if unsure.
