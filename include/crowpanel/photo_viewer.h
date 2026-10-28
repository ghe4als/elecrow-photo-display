#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace crowpanel {

constexpr int MAX_PHOTOS = 128;
constexpr uint32_t PHOTO_INTERVAL_MS = 10000;

bool init_photo_viewer();
void photo_viewer_next();
void photo_viewer_previous();
void photo_viewer_toggle_pause();
bool photo_viewer_is_paused();
int photo_viewer_count();

void photo_viewer_handle_event(lv_event_t* event);

void photo_viewer_tick();

} // namespace crowpanel
