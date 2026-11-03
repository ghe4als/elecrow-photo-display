#pragma once

#include <Arduino.h>

// Open-Meteo weather client for the info-screen weather card.
//
// All weather network code lives in weather.cpp. weather_fetch_blocking()
// blocks for up to ~10 s (TLS handshake + HTTPS GET), so call it only from a
// dedicated FreeRTOS task with an ample stack (16 KB or more recommended) -
// never from ui_task or an LVGL timer. WiFi must already be configured by
// main.cpp; weather_init() does not touch the network.

// Cached snapshot of the current outdoor weather. One dedicated task is the
// single writer; the UI reads the struct as a last-good snapshot.
struct WeatherData {
    bool valid;              // true only when the most recent fetch succeeded
    int temperature_c;       // outdoor temperature rounded to whole degrees C
    char condition_text[32]; // WMO code mapped to short human-readable text
    uint32_t updated_ms;     // millis() at the moment the snapshot was written
};

// One-time setup: validates the compile-time WEATHER_LAT / WEATHER_LON config
// and prebuilds the Open-Meteo request URL. Does not use the network.
// Returns false only when the configured location cannot form a URL.
// Call once from setup() before starting the weather task.
bool weather_init();

// Perform one blocking HTTPS fetch to Open-Meteo and fill `out`.
// Returns true and sets out.valid = true on success. On any failure (WiFi
// down, connection error, non-200 status, JSON parse error, or missing
// response fields) returns false and sets out.valid = false. Logs heap
// watermarks (ESP.getMaxAllocHeap) before and after every attempt.
bool weather_fetch_blocking(WeatherData& out);
