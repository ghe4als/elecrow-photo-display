#pragma once

#include <Arduino.h>

namespace crowpanel {

// Maximum number of photos the slideshow can hold at once.
constexpr int MAX_PHOTOS = 128;

// Default interval between slides in milliseconds.
constexpr uint32_t PHOTO_INTERVAL_MS = 10000;

// Initialize the photo viewer on the active LVGL screen.
// Returns true if at least one photo was found on the SD card.
bool init_photo_viewer();

// Advance to the next photo manually or from the timer.
void photo_viewer_next();

// Go back to the previous photo.
void photo_viewer_previous();

// Toggle pause/resume of the auto-advance timer.
void photo_viewer_toggle_pause();

// Return true if the slideshow is currently paused.
bool photo_viewer_is_paused();

// Number of photos discovered on the SD card.
int photo_viewer_count();

void photo_viewer_tick();

} // namespace crowpanel
