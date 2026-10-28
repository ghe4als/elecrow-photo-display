#include "crowpanel/display_power.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <lvgl.h>

#include "crowpanel/photo_viewer.h"

namespace crowpanel {

namespace {

constexpr uint8_t backlight_pin = 2;
constexpr uint32_t ntp_sync_interval_ms = 60 * 60 * 1000;
constexpr uint32_t ntp_initial_timeout_ms = 15000;
constexpr uint32_t test_pulse_ms = 5000;

enum class State { ACTIVE, DIMMED, OFF };

State g_state = State::ACTIVE;
uint32_t g_last_activity_ms = 0;
uint32_t g_pause_started_ms = 0;
uint32_t g_last_ntp_sync_ms = 0;
bool g_ntp_synced = false;
bool g_test_off_active = false;
uint32_t g_test_off_until_ms = 0;

void set_backlight(bool on)
{
    digitalWrite(backlight_pin, on ? HIGH : LOW);
}

bool is_within_display_hours()
{
    if (!g_ntp_synced) {
        return true; // Keep display on until time is known.
    }
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    return local.tm_hour >= DISPLAY_ON_HOUR && local.tm_hour < DISPLAY_OFF_HOUR;
}

} // namespace

void display_power_init()
{
    pinMode(backlight_pin, OUTPUT);
    set_backlight(true);

    if (WiFi.status() == WL_CONNECTED) {
        const int32_t offset_sec = TIMEZONE_UTC_OFFSET_HOURS * 3600;
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_set_time_sync_notification_cb([](struct timeval *) {
            g_ntp_synced = true;
            Serial.println("[ntp] synced");
        });
        setenv("TZ", "ICT-7", 1);
        tzset();
        esp_sntp_init();
        g_last_ntp_sync_ms = millis();
        Serial.println("[ntp] starting sync ...");
    }
}

void display_power_tick()
{
    const uint32_t now = millis();

    if (g_ntp_synced && now - g_last_ntp_sync_ms >= ntp_sync_interval_ms) {
        g_ntp_synced = false;
        sntp_restart();
        g_last_ntp_sync_ms = now;
    }

    if (photo_viewer_is_paused() && g_pause_started_ms == 0) {
        g_pause_started_ms = now;
    } else if (!photo_viewer_is_paused()) {
        g_pause_started_ms = 0;
    }

    if (photo_viewer_is_paused() && g_pause_started_ms > 0
        && now - g_pause_started_ms >= AUTO_UNPAUSE_MS) {
        Serial.println("[power] auto-unpausing after timeout");
        photo_viewer_toggle_pause();
        g_pause_started_ms = 0;
    }

    if (g_test_off_active) {
        if (now >= g_test_off_until_ms) {
            g_test_off_active = false;
            g_state = State::ACTIVE;
            set_backlight(true);
            Serial.println("[power] test off ended");
        }
        return;
    }

    const bool in_hours = is_within_display_hours();
    const bool idle_timed_out = now - g_last_activity_ms >= BACKLIGHT_TIMEOUT_MS;

    if (!in_hours) {
        if (g_state != State::OFF) {
            g_state = State::OFF;
            set_backlight(false);
            Serial.println("[power] night mode off");
        }
        return;
    }

    if (g_state == State::OFF && in_hours) {
        g_state = State::ACTIVE;
        set_backlight(true);
        g_last_activity_ms = now;
        Serial.println("[power] morning on");
        return;
    }

    if (g_state == State::ACTIVE && idle_timed_out) {
        g_state = State::OFF;
        set_backlight(false);
        Serial.println("[power] idle timeout off");
        return;
    }

    if (g_state == State::OFF && !idle_timed_out) {
        g_state = State::ACTIVE;
        set_backlight(true);
        Serial.println("[power] wake from idle");
    }
}

void display_power_reset_activity()
{
    g_last_activity_ms = millis();
}

bool display_power_reset_activity_and_check_wake()
{
    const uint32_t now = millis();
    const bool was_off = (g_state == State::OFF || g_state == State::DIMMED) && !g_test_off_active;
    g_last_activity_ms = now;
    if (was_off) {
        g_state = State::ACTIVE;
        set_backlight(true);
        Serial.println("[power] touch wake");
    }
    return was_off;
}

void display_power_test_off(uint32_t ms)
{
    g_test_off_active = true;
    g_test_off_until_ms = millis() + ms;
    set_backlight(false);
    Serial.printf("[power] test off for %u ms\n", ms);
}

bool display_power_is_active()
{
    return g_state != State::OFF || g_test_off_active;
}

} // namespace crowpanel
