# Phase 1: SD-Card Slideshow Build Plan

## Goal
Make the CrowPanel 5" HMI display JPEGs from a `/photos` folder on the SD card in a looping slideshow, without cloud dependencies.

## Scope
- SD card mount via SPI on pins `MOSI=11`, `MISO=13`, `SCK=12`, `CS=10`.
- Custom LVGL file-system driver registered for letter `S:` backed by Arduino `SD.h`.
- LVGL image decoder enabled for JPEG/PNG/BMP via build flags.
- Full-screen photo viewer with aspect-fit scaling and 10-second auto-advance.
- Replace the current SquareLine-generated demo screen with the photo viewer in `main.cpp`.
- Build and type-check with `pio run -e crowpanel-5`.

## Files to add
- `include/crowpanel/sdcard.h`
- `src/crowpanel/sdcard.cpp`
- `include/crowpanel/photo_viewer.h`
- `src/crowpanel/photo_viewer.cpp`

## Files to edit
- `src/crowpanel/main.cpp` — init SD + photo viewer instead of `ui_init()`
- `platformio.ini` — add `-D LV_USE_TJPGD=1`, `-D LV_USE_LODEPNG=1`, `-D LV_USE_BMP=1`

## Verification
1. `pio run -e crowpanel-5` exits 0.
2. Serial monitor shows SD card mounted and file count.
3. Device cycles through `/photos/*.jpg` with correct aspect ratio.

## Out of scope for Phase 1
- Touch navigation
- Google Drive sync
- Filename overlay / status UI
- Pre-processing / image resize helper
