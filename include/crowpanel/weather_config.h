// Weather location + refresh defaults for the info-screen weather card
// (Open-Meteo, no API key required).
//
// Defaults point at the Vietnam location from the plan so the firmware
// works out of the box. Every value can be overridden at build time
// without editing this file:
//
//   .env (gitignored; injected as -D defines by scripts/load_env.py):
//       WEATHER_LAT=12.8645066
//       WEATHER_LON=107.9744271
//       WEATHER_REFRESH_MIN=90        <- see note below, use build flag instead
//
//   build_flags / CLI:
//       -DWEATHER_LAT='"12.8645066"' -DWEATHER_LON='"107.9744271"'
//       -DWEATHER_REFRESH_MIN=90
//
// WEATHER_LAT / WEATHER_LON are string literals rather than floats because
// scripts/load_env.py always injects .env values as quoted strings, and the
// values drop straight into the Open-Meteo URL query. WEATHER_REFRESH_MIN is
// a plain integer because it feeds FreeRTOS timeout arithmetic; override it
// with a numeric build flag (a .env value would arrive as the string "90"
// and fail the static_assert below). Attribution text is shown on the info
// screen per Open-Meteo's CC BY 4.0 license terms.
//
// Keep this header Arduino-free so any module (including host tests) can
// include it.
#ifndef WEATHER_CONFIG_H
#define WEATHER_CONFIG_H

#ifndef WEATHER_LAT
#define WEATHER_LAT "12.864506604834714"
#endif

#ifndef WEATHER_LON
#define WEATHER_LON "107.97442705894242"
#endif

#ifndef WEATHER_REFRESH_MIN
#define WEATHER_REFRESH_MIN 60
#endif

#ifndef OPEN_METEO_ATTRIBUTION
#define OPEN_METEO_ATTRIBUTION "Weather data by Open-Meteo (CC BY 4.0)"
#endif

// Prebuilt Open-Meteo URL fragment via compile-time literal concatenation:
// "...forecast?latitude=...&longitude=...&..." — append after the '?'.
#define WEATHER_QUERY_LAT_LON "latitude=" WEATHER_LAT "&longitude=" WEATHER_LON

// Fail the build loudly (instead of corrupting the URL at runtime) if lat/lon
// are overridden as unquoted numbers; subscripting only compiles on strings.
static_assert(WEATHER_LAT[0] >= '0' && WEATHER_LAT[0] <= '9',
              "WEATHER_LAT must be a quoted string, e.g. -DWEATHER_LAT='\"12.8\"'");
static_assert((WEATHER_LON[0] >= '0' && WEATHER_LON[0] <= '9') || WEATHER_LON[0] == '-',
              "WEATHER_LON must be a quoted string, e.g. -DWEATHER_LON='\"107.9\"'");
static_assert(WEATHER_REFRESH_MIN > 0,
              "WEATHER_REFRESH_MIN must be a positive integer build flag");

#endif // WEATHER_CONFIG_H