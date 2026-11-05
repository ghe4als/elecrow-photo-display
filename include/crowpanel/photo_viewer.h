#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "crowpanel/weather.h"

namespace crowpanel {

constexpr int MAX_PHOTOS = 256;
constexpr uint32_t PHOTO_INTERVAL_MS = 10000;

constexpr int INFO_EVERY_N_PHOTOS = 10;
constexpr uint32_t INFO_HOLD_MS = 10000;

bool init_photo_viewer();
void photo_viewer_next();
void photo_viewer_previous();
void photo_viewer_toggle_pause();
bool photo_viewer_is_paused();
int photo_viewer_count();

void photo_viewer_handle_event(lv_event_t* event);

void photo_viewer_tick();

// WeatherData lives in crowpanel/weather.h; the include above makes
// crowpanel::WeatherData visible here.

struct IndoorClimate {
    bool valid;
    int temperature_c;
    int humidity_pct;
    uint32_t updated_ms;
};

// Thread-safety: each value has one writer task (weather_task / dht20_task)
// and is read from the LVGL ui_task via the getters, which copy out under a
// short critical section so readers never see torn data.
void photo_viewer_set_weather(const WeatherData& data);
void photo_viewer_set_indoor_climate(const IndoorClimate& data);
bool photo_viewer_get_weather(WeatherData& out);
bool photo_viewer_get_indoor_climate(IndoorClimate& out);

void photo_viewer_show_info();

} // namespace crowpanel
