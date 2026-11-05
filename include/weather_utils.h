#pragma once

// Pure helper: WMO weather interpretation code -> human-readable text and
// icon category. No Arduino/ESP32/LVGL dependencies so the `native` host test
// env can compile and unit-test it. wmo_weather_code_to_text() returns a
// pointer to a static string literal; never returns NULL and never
// allocates.
const char* wmo_weather_code_to_text(int code);

// Icon category for the info-card weather glyph, drawn with LVGL primitives
// (no bitmap assets). Unknown covers any code outside the WMO table.
enum class WeatherIcon { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Storm, Unknown };

WeatherIcon wmo_weather_code_to_icon(int code);
