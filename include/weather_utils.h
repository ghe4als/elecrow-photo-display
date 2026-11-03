#pragma once

// Pure helper: WMO weather interpretation code -> human-readable text.
// No Arduino/ESP32/LVGL dependencies so the `native` host test env can
// compile and unit-test it. Returns a pointer to a static string literal;
// never returns NULL and never allocates.
const char* wmo_weather_code_to_text(int code);
