// WMO weather code -> text mapping tests. This file only defines test
// functions; they are registered from the shared Unity main in
// test_starter.cpp (PlatformIO compiles all files under test/ into a
// single program, so only one main()/setUp()/tearDown() may exist).
#include <unity.h>
#include "weather_utils.h"

// Mirrors the table in src/weather_utils.cpp so every mapped WMO code is
// checked against its expected text.
struct ExpectedText {
    int code;
    const char* text;
};

static const ExpectedText kExpected[] = {
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

void test_weather_clear_sky(void) {
    TEST_ASSERT_EQUAL_STRING("Clear sky", wmo_weather_code_to_text(0));
}

void test_weather_moderate_rain(void) {
    TEST_ASSERT_EQUAL_STRING("Moderate rain", wmo_weather_code_to_text(63));
}

void test_weather_unknown_code(void) {
    TEST_ASSERT_EQUAL_STRING("Unknown", wmo_weather_code_to_text(9999));
}

void test_weather_all_known_codes(void) {
    for (const ExpectedText& entry : kExpected) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(entry.text,
                                         wmo_weather_code_to_text(entry.code),
                                         "wrong text for known WMO code");
    }
}

void test_weather_all_codes_non_null(void) {
    // Exhaustive over the whole 0..99 range: every code returns a valid
    // non-NULL string, either its WMO text or "Unknown" for unmapped codes.
    for (int code = 0; code <= 99; ++code) {
        TEST_ASSERT_NOT_NULL(wmo_weather_code_to_text(code));
    }
}

void test_weather_unmapped_codes_unknown(void) {
    // Any code in 0..99 that is not a defined WMO code must say "Unknown".
    for (int code = 0; code <= 99; ++code) {
        bool is_known = false;
        for (const ExpectedText& entry : kExpected) {
            if (entry.code == code) {
                is_known = true;
                break;
            }
        }
        if (!is_known) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("Unknown",
                                             wmo_weather_code_to_text(code),
                                             "unmapped code must be Unknown");
        }
    }
}

void test_weather_negative_unknown(void) {
    TEST_ASSERT_EQUAL_STRING("Unknown", wmo_weather_code_to_text(-1));
}
