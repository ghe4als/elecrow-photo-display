// Pure-logic helper: WMO weather interpretation code -> short English text.
// Keep hardware deps out of here so the `native` test env can compile it.
#include "weather_utils.h"

namespace {

struct WmoEntry {
    int code;
    const char* text;
};

// WMO weather interpretation codes (WW). Codes not listed (and anything
// outside 0..99) map to "Unknown".
const WmoEntry kWmoTable[] = {
    {0,  "Clear sky"},
    {1,  "Mainly clear"},
    {2,  "Partly cloudy"},
    {3,  "Overcast"},
    {45, "Fog"},
    {48, "Depositing rime fog"},
    {51, "Light drizzle"},
    {53, "Moderate drizzle"},
    {55, "Dense drizzle"},
    {56, "Light freezing drizzle"},
    {57, "Dense freezing drizzle"},
    {61, "Slight rain"},
    {63, "Moderate rain"},
    {65, "Heavy rain"},
    {66, "Light freezing rain"},
    {67, "Heavy freezing rain"},
    {71, "Slight snowfall"},
    {73, "Moderate snowfall"},
    {75, "Heavy snowfall"},
    {77, "Snow grains"},
    {80, "Slight rain showers"},
    {81, "Moderate rain showers"},
    {82, "Violent rain showers"},
    {85, "Slight snow showers"},
    {86, "Heavy snow showers"},
    {95, "Thunderstorm"},
    {96, "Thunderstorm with slight hail"},
    {99, "Thunderstorm with heavy hail"},
};

} // namespace

const char* wmo_weather_code_to_text(int code) {
    for (const WmoEntry& entry : kWmoTable) {
        if (entry.code == code) {
            return entry.text;
        }
    }
    return "Unknown";
}
