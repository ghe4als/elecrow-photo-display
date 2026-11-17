#pragma once

#include <Arduino.h>
#include <FS.h>

#include "crowpanel/photo_index.h"

namespace crowpanel {

// CrowPanel 5.0" HMI V3.0 TF card is wired to SPI with these pins.
constexpr uint8_t SD_SCK  = 12;
constexpr uint8_t SD_MISO = 13;
constexpr uint8_t SD_MOSI = 11;
constexpr uint8_t SD_CS   = 10;

// Mount the SD card. Safe to call more than once; returns true only when ready.
bool init_sd_card();

// True if the SD card was successfully mounted.
bool sd_ready();

// Access the mounted SD file system. Check sd_ready() first.
fs::FS& get_sd_fs();

// Load every photo filename in `dirname` into a PSRAM-backed PhotoIndex.
// Two passes over the directory: pass 1 records each matching name's length
// and the true on-card total (never capped by `ceiling`); pass 2 copies the
// first load_count bare filenames (no leading slash — the consumer adds it)
// into an exactly-sized PSRAM arena. `reserve_bytes` is subtracted from free
// PSRAM before sizing so allocations made after this call (display buffers)
// are not starved. Returns out.count (0 when the card is empty, the budget is
// exhausted, or a PSRAM allocation fails), or -1 if the directory could not
// be opened.
int load_photo_index(const char* dirname, PhotoIndex& out, size_t reserve_bytes, int ceiling);

// Register an LVGL file-system driver for the 'S:' drive backed by SD.
// Must be called after init_sd_card() and lv_init().
void register_lvgl_sd_fs();

} // namespace crowpanel
