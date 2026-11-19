# CrowPanel 5" Photo Display — Progress and Issue Summary

Last updated: 2026-11-06

## 1. Goal

Get the CrowPanel 5" working reliably:

- Boot and show a **portrait info card first** with date, time, and weather.
- Then resume the photo slideshow without freezing.
- Weather must actually display.
- The absent DHT20 sensor must be safely disabled.

## 2. Hardware / Environment

- **Board:** ESP32-S3 CrowPanel 5", 800×480 RGB display.
- **Detected hardware:** ESP32-S3 v0.2, 8 MB PSRAM, 4 MB flash.
- **Touch:** GT911 at `0x5D`.
- **Serial:** CH340, normally `/dev/cu.usbserial-110`, sometimes re-enumerates as `/dev/cu.usbserial-120`.
- **Storage:** SD card, 15,193 MB, mounted over SPI.
- **Worktree:** `/Users/zero/code/wt/info-screen-rework`
- **Current HEAD:** `3bdb8cb` (`chore(sensor): shelf DHT20 behind ENABLE_DHT20=0 ...`)
- **Build tool:** `/Users/zero/code/crowpanel-photo-display/.venv/bin/pio` — the system `pio` uses Python 3.15.0, which pioarduino rejects.

## 3. Main Problem

### Card rendering hangs the UI

When the portrait info card is rotated 90° and rendered, the device hangs inside the first `lv_timer_handler()` call.

Observed sequence:

```text
[info] overlay shown
[loop] 0 pre-lv
[touch] read 0 begin
[touch] read 0 end
```

Then nothing. No `[flush]`, no `[loop] 0 post-lv`, no heartbeat.

With `INFO_CARD_ROTATION = 0`, the loop continues and photos work, but the card is not portrait.

This proves the hang is tied to the **rotated card rendering path**, not to:

- RGB frame switching
- Touch I2C
- NTP
- Weather fetching
- SD mounting
- The main UI loop itself

## 4. Root-Cause Investigation

### What was tested

| Test | Result |
|---|---|
| Card rotation = 0 | Works; loop and photos run |
| Weekday label only, rotated | Works; flushes and heartbeats |
| 4 rotated labels: weekday, date, clock, temp | Works |
| 4 labels + short condition `"Wx"` | Works |
| 4 labels + long `"Weather unavailable"` | Hangs |
| 4 labels + condition + indoor + divider | Hangs |
| Full card with shortened texts | Still hangs |
| One rotated 480×800 parent containing all labels | Hangs |
| LVGL PSRAM pool increased from 1 MB to 4 MB | Still hangs |

### Ruled out as sole causes

- Weather icon draw callback
- Rotated transparent icon container
- 1 px divider
- Custom 80 px clock font
- Gradient background
- Off-screen attribution label
- Card elements being children of `g_screen` vs `g_info_overlay`
- Touch, NTP, weather task, SD task
- The 1 MB LVGL PSRAM pool being too small

### Current hypothesis

The LVGL software renderer is hitting a **transformed-layer size/aggregate limit** when many or large rotated objects are present.

Evidence:

- 4 rotated labels work.
- Adding a long rotated condition label pushes it over the edge.
- Shortening that label fixes it.
- Adding the remaining card elements makes it hang again.
- Replacing all per-label transforms with one large 480×800 rotated parent also hangs.
- Increasing the LVGL heap pool does not help.

This is still a hypothesis, not fully proven. The exact threshold has not been determined.

## 5. What Has Been Fixed

### Weather fetch — fixed and verified

Original code used:

```cpp
deserializeJson(doc, http.getStream())
```

It returned `InvalidInput` with a `-110` timeout. Changed to:

```cpp
String body = http.getString();
```

Verified on device:

```text
[weather] body 369 bytes
[weather] fetch ok
[weather] result: 30 C, Clear sky
[weather] published ok
```

The weather task now publishes valid data.

### DHT20 shelf-out — committed

Commit `3bdb8cb`:

- `ENABLE_DHT20` defaults to `1`.
- `platformio.ini` sets `-D ENABLE_DHT20=0`.
- DHT20 probe/task creation is guarded in `main.cpp`.
- Info card shows an offline indoor message.
- Build green, 26/26 native tests.

### SD mount retry — implemented, uncommitted

`src/crowpanel/sdcard.cpp` now retries:

```text
4,000,000 Hz
1,000,000 Hz
400,000 Hz
```

The card still mounts inconsistently. Sometimes it mounts at 4 MHz, sometimes 1 MHz, sometimes not at all.

### Info card redesign — committed, but incomplete

The portrait card, clock, vector icons, and layout are committed. The rendering path is what hangs.

## 6. Current Code State

### `src/crowpanel/photo_viewer.cpp`

Reverted to HEAD `3bdb8cb`, then patched with a **minimal card**:

- Weekday label
- Date label
- Clock label
- Temperature label
- Condition label
- Clock font changed from `lv_font_clock_80` to `lv_font_montserrat_40`
- Condition fallback changed to `No weather`
- Condition text truncated to 16 characters
- Weather icon, divider, indoor label, status label, and attribution label are **created and then deleted** after creation
- The card therefore renders only 5 rotated labels

This minimal-card firmware **compiled successfully** but has **not been flashed yet**.

### `src/crowpanel/main.cpp`

Still contains temporary diagnostics:

- `[loop]` heartbeat and phase logs
- `[flush]` logs
- `[touch]` logs
- LVGL PSRAM pool set to **4 MB** instead of 1 MB

These need cleanup before final commit.

### `src/crowpanel/sdcard.cpp`

Contains the 3-frequency mount retry. Uncommitted.

### `src/crowpanel/weather.cpp`

Contains the `getString()` fix and diagnostic body logging. Uncommitted.

### `include/crowpanel/photo_viewer.h`

Current constants:

- `MAX_PHOTOS = 256`
- `PHOTO_INTERVAL_MS = 10000`
- `INFO_EVERY_N_PHOTOS = 10`
- `INFO_HOLD_MS = 10000`

## 7. Build / Upload Status

### Last build

Succeeded:

```text
Total image size: 2,827,815 bytes
Flash: 89.9% (2,827,559 / 3,145,728)
RAM:   36.5% (119,744 / 327,680)
```

The minimal-card binary exists at:

```text
.pio/build/crowpanel-5/firmware.bin
```

### Last upload

Failed at the upload step:

```text
Environment crowpanel-5  FAILED  00:00:17.933
```

The port is currently visible again:

```text
/dev/cu.usbserial-110  USB VID:PID=1A86:7523
```

This looks like the CH340 re-enumeration / power / cable issue, not a code failure.

### Flash headroom

The app partition is nearly full. Only about **318 KB** remains, so further code growth is a risk.

## 8. Current Working / Broken Matrix

| Area | Status |
|---|---|
| Build | Working |
| Weather fetch | Working |
| Weather display | Not yet verified in the current minimal card |
| DHT20 disable | Working / committed |
| SD mount | Flaky |
| Photo scan | Restored in source; not yet verified after revert |
| Photo slideshow | Worked with card rotation = 0 |
| Portrait card | Hangs when fully populated; minimal 5-label version untested |
| Touch | Reads successfully before the card hang |
| NTP | Syncs around 6 seconds |
| CH340 serial | Intermittently drops / re-enumerates |

## 9. Current Risks

- The minimal card may still hang because it still uses 5 rotated labels and the HEAD gradient background.
- The full intended card cannot use the current per-label rotation approach.
- SD startup can block for more than 25 seconds and sometimes fails to mount.
- CH340 instability makes flashing and capture unreliable.
- The app is close to the flash limit.
- Weather conditions longer than 16 characters are truncated.
- Temporary diagnostic logs and the 4 MB pool remain in the tree.
- No commit has been made since `3bdb8cb`.

## 10. Recommended Next Steps

1. **Retry the upload** on the current port and capture the minimal-card boot log.
2. If the minimal card works:
   - Re-enable/verify the photo scan.
   - Verify info card → slideshow transition.
   - Remove diagnostic logs and decide whether to keep the 4 MB pool.
   - Commit the minimal working card as an interim fix.
3. For the full card design, choose one:
   - **Pre-render the card** into a 480×800 buffer, rotate the pixels in software to 800×480, and display it as an unrotated image.
   - Reduce the card to short rotated lines and drop/split long elements.
   - Investigate the LVGL transform-layer limit further.
4. Separately fix SD reliability and CH340 power/cable issues.

## 11. Build / Flash Commands

```sh
cd /Users/zero/code/wt/info-screen-rework

/Users/zero/code/crowpanel-photo-display/.venv/bin/pio run -e crowpanel-5

/Users/zero/code/crowpanel-photo-display/.venv/bin/pio run -e crowpanel-5 \
  -t upload --upload-port /dev/cu.usbserial-110

/Users/zero/code/crowpanel-photo-display/.venv/bin/pio test -e native
```

## 12. Repository State

Modified but uncommitted:

```text
M src/crowpanel/main.cpp
M src/crowpanel/photo_viewer.cpp
M src/crowpanel/sdcard.cpp
M src/crowpanel/weather.cpp
```

Recent commits:

```text
3bdb8cb chore(sensor): shelf DHT20 behind ENABLE_DHT20=0 until hardware is fitted
736407d docs(readme): describe portrait info card and weather statuses
5154b80 feat(card): rebuild info overlay in portrait with clock and vector icons
1f03eb9
2bcf88d
be1297d
266b855
e748e74
```
