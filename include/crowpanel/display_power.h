#pragma once

#include <Arduino.h>

namespace crowpanel {

constexpr uint8_t DISPLAY_ON_HOUR = 6;
constexpr uint8_t DISPLAY_OFF_HOUR = 20;
constexpr int32_t TIMEZONE_UTC_OFFSET_HOURS = 7;
constexpr uint32_t AUTO_UNPAUSE_MS = 5 * 60 * 1000;
constexpr uint32_t BACKLIGHT_TIMEOUT_MS = 30 * 60 * 1000;

void display_power_init();
void display_power_tick();
void display_power_reset_activity();
bool display_power_reset_activity_and_check_wake();
void display_power_test_off(uint32_t ms);
bool display_power_is_active();

} // namespace crowpanel
