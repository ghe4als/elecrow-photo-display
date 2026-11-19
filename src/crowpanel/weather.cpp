// Open-Meteo current-weather client for the info-screen weather card.
//
// All blocking weather network I/O lives here. weather_fetch_blocking() takes
// up to ~10 s (TLS handshake + HTTPS GET), so it must only run on a dedicated
// FreeRTOS task, never on ui_task or inside an LVGL timer. The WEATHER_REFRESH_MIN
// cadence is enforced by the caller's task, not by this module.
#include "crowpanel/weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include "crowpanel/weather_config.h"
#include "weather_utils.h"

// Builtin CA cert bundle embedded in libmbedtls.a by the pinned pioarduino
// 55.03.38 / arduino-esp32 3.x core. That core has no useBuiltinCACertBundle();
// the raw bundle symbols (assembly labels, so extern "C") are declared here
// and handed to setCACertBundle(). x509_crt_bundle_length is a read-only word
// holding the byte count.
extern "C" {
extern const uint8_t x509_crt_bundle[];
extern const unsigned int x509_crt_bundle_length;
}

namespace {

constexpr uint16_t kHttpTimeoutMs = 10000; // requirement: timeout <= 10 s
constexpr uint16_t kUrlBufferSize = 160;    // host + lat/lon query fits easily

// Prebuilt by weather_init() (which runs in setup(), before any task starts);
// read-only afterwards, so the fetch task needs no locking.
char g_url[kUrlBufferSize] = {0};

// WEATHER_LAT / WEATHER_LON are validated by static_asserts in
// weather_config.h, so a well-formed URL is guaranteed here.
bool build_url()
{
    const int written = snprintf(
        g_url, sizeof(g_url),
        "https://api.open-meteo.com/v1/forecast?%s&current=temperature_2m,weather_code",
        WEATHER_QUERY_LAT_LON);
    return written > 0 && static_cast<size_t>(written) < sizeof(g_url);
}

bool fetch_and_parse(crowpanel::WeatherData& out)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[weather] WiFi not connected; skipping fetch");
        return false;
    }

    WiFiClientSecure client;
    client.setCACertBundle(x509_crt_bundle, x509_crt_bundle_length);

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, g_url)) {
        Serial.printf("[weather] HTTP begin failed for %s\n", g_url);
        return false;
    }

    const int status = http.GET();
    if (status <= 0) {
        Serial.printf("[weather] GET failed: %s\n",
                      http.errorToString(status).c_str());
        http.end();
        return false;
    }
    if (status != HTTP_CODE_OK) {
        Serial.printf("[weather] HTTP status %d\n", status);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    Serial.printf("[weather] body %u bytes: %.100s\n", (unsigned)body.length(), body.c_str());
    if (body.length() == 0) {
        Serial.println("[weather] empty response body");
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[weather] JSON parse error: %s\n", err.c_str());
        return false;
    }

    const JsonVariantConst current = doc["current"];
    const JsonVariantConst temperature = current["temperature_2m"];
    const JsonVariantConst weather_code = current["weather_code"];
    if (current.isNull() || temperature.isNull() || weather_code.isNull()) {
        Serial.println("[weather] response missing current weather fields");
        return false;
    }

    out.temperature_c = static_cast<int>(lroundf(temperature.as<float>()));
    const int code = weather_code.as<int>();
    out.wmo_code = code;
    strlcpy(out.condition_text, wmo_weather_code_to_text(code),
            sizeof(out.condition_text));
    out.last_success_ms = millis();
    out.valid = true;
    return true;
}

} // namespace

bool weather_init()
{
    if (!build_url()) {
        Serial.println("[weather] failed to build request URL; check WEATHER_LAT/WEATHER_LON");
        return false;
    }
    Serial.printf("[weather] init: %s (refresh %d min)\n", g_url, (int)WEATHER_REFRESH_MIN);
    return true;
}

bool weather_fetch_blocking(crowpanel::WeatherData& out)
{
    out.valid = false;
    out.status = crowpanel::WeatherStatus::Error;
    out.temperature_c = 0;
    out.condition_text[0] = '\0';
    out.wmo_code = -1;
    out.last_success_ms = 0;
    out.last_attempt_ms = 0;

    // Log the heap watermark around every attempt (success or failure) so the
    // TLS handshake's memory impact stays visible on the serial console.
    const uint32_t heap_before = ESP.getMaxAllocHeap();
    Serial.printf("[weather] fetch begin (max alloc heap %u)\n", (unsigned)heap_before);

    const bool ok = fetch_and_parse(out);

    const uint32_t heap_after = ESP.getMaxAllocHeap();
    Serial.printf("[weather] fetch %s (max alloc heap %u -> %u)\n",
                  ok ? "ok" : "failed", (unsigned)heap_before, (unsigned)heap_after);
    if (ok) {
        out.status = crowpanel::WeatherStatus::Ok;
        Serial.printf("[weather] result: %d C, %s\n", out.temperature_c, out.condition_text);
    }
    return ok;
}
