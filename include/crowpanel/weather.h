#pragma once

#include <Arduino.h>

// Open-Meteo weather client for the info-screen weather card.
//
// All weather network code lives in weather.cpp. weather_fetch_blocking()
// blocks for up to ~10 s (TLS handshake + HTTPS GET), so call it only from a
// dedicated FreeRTOS task with an ample stack (16 KB or more recommended) -
// never from ui_task or an LVGL timer. WiFi must already be configured by
// main.cpp; weather_init() does not touch the network.

namespace crowpanel {

// Lifecycle of the weather card's data source, published alongside the
// snapshot so the card can distinguish offline / waiting / fetching / fresh /
// retrying without extra state.
enum class WeatherStatus { NoWifi, WaitingForTime, Fetching, Ok, Error };

// Cached snapshot of the current outdoor weather. One dedicated task is the
// single writer; the UI reads the struct as a last-good snapshot.
struct WeatherData {
    WeatherStatus status;
    bool valid;               // last-good reading exists
    int temperature_c;       // outdoor temperature rounded to whole degrees C
    char condition_text[32]; // WMO code mapped to short human-readable text
    int wmo_code;
    uint32_t last_success_ms; // millis() of last good fetch
    uint32_t last_attempt_ms;
};

} // namespace crowpanel

// One-time setup: validates the compile-time WEATHER_LAT / WEATHER_LON config
// and prebuilds the Open-Meteo request URL. Does not use the network.
// Returns false only when the configured location cannot form a URL.
// Call once from setup() before starting the weather task.
bool weather_init();

// Perform one blocking HTTPS fetch to Open-Meteo and fill `out`.
// Returns true and sets out.valid = true and out.status = Ok on success. On
// any failure (WiFi down, connection error, non-200 status, JSON parse
// error, or missing response fields) returns false and sets out.valid =
// false and out.status = Error. Logs heap watermarks (ESP.getMaxAllocHeap)
// before and after every attempt.
bool weather_fetch_blocking(crowpanel::WeatherData& out);
