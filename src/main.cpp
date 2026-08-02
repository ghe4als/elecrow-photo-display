// ESP32 Starter - Arduino framework
// ----------------------------------------------------------------------------
// Combines: onboard LED blink, serial logging, and a WiFi station connection
// example. Build with:  pio run -e esp32dev-arduino
// Upload + monitor:     pio run -t upload -t monitor  (or pio device monitor)
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include "wifi_config.h"

// LED pin. The framework defines LED_BUILTIN for many variants, but on some
// (e.g. ESP32-S3 DevKitC-1) it points at an addressable RGB NeoPixel on GPIO48
// which doesn't blink cleanly with plain digitalWrite. Override per-env with
// -DBLINK_PIN=<gpio> in platformio.ini; otherwise fall back to the framework
// value, or GPIO2 if even that is undefined.
#ifdef BLINK_PIN
static const uint8_t LED_PIN = BLINK_PIN;
#elif defined(LED_BUILTIN)
static const uint8_t LED_PIN = LED_BUILTIN;
#else
static const uint8_t LED_PIN = 2; // GPIO2 on most original ESP32 dev boards
#endif

static const uint32_t BLINK_INTERVAL_MS = 500;
static const uint32_t WIFI_TIMEOUT_MS   = 15000;

static uint32_t lastBlink = 0;
static bool ledState = false;

static void blinkTask();
static void connectWifi();

void setup() {
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);
    delay(100);

    Serial.println();
    Serial.println(F("[starter] booting..."));
    Serial.printf("[starter] CPU freq: %u MHz | Free heap: %u bytes\n",
                  (unsigned)ESP.getCpuFreqMHz(), (unsigned)ESP.getFreeHeap());

    connectWifi();
}

void loop() {
    blinkTask();
}

static void blinkTask() {
    uint32_t now = millis();
    if (now - lastBlink >= BLINK_INTERVAL_MS) {
        lastBlink = now;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
        Serial.printf("[blink] LED=%u t=%lu ms\n", ledState ? 1u : 0u, (unsigned long)now);
    }
}

static void connectWifi() {
    const char* ssid = WIFI_SSID;
    const char* pass = WIFI_PASS;

    if (strcmp(ssid, "CHANGE_ME") == 0) {
        Serial.println(F("[wifi] WIFI_SSID is default; skipping connect. "
                         "Set WIFI_SSID/WIFI_PASS env vars or edit include/wifi_config.h."));
        return;
    }

    Serial.printf("[wifi] connecting to SSID '%s' ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("esp32-starter");
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected! IP=%s  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    } else {
        Serial.println(F("[wifi] connect failed - continuing offline"));
    }
}