#pragma once

#include <Arduino.h>
#include <FS.h>

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

// Scan a directory on the SD card for JPEG/PNG/BMP files.
// out_paths must have room for max_count entries. Returns the number of
// matching files written, or -1 if the directory could not be opened.
int list_photo_files(const char* dirname, String* out_paths, int max_count);

// Register an LVGL file-system driver for the 'S:' drive backed by SD.
// Must be called after init_sd_card() and lv_init().
void register_lvgl_sd_fs();

} // namespace crowpanel
