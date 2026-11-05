#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>
#include <esp32-hal-psram.h>

#include "wifi_config.h"
#include "crowpanel/touch.h"
#include "crowpanel/ui.h"
#include "crowpanel/sdcard.h"
#include "crowpanel/dht20.h"
#include "crowpanel/photo_viewer.h"
#include "crowpanel/display_power.h"
#include "crowpanel/weather.h"
#include "crowpanel/weather_config.h"

/*---------------------------------------------------------------
 * PlatformIO RGB/LVGL application
 * Configure the 800x480 panel, sensor, touch input, and generated UI.
 *--------------------------------------------------------------*/
namespace {
// These values bind LVGL and the application outputs to the 5.0-inch board.
constexpr uint16_t screen_width = 800;
constexpr uint16_t screen_height = 480;
constexpr uint8_t backlight_pin = 2;
constexpr uint8_t led_pin = 38;

class Display : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;

    /** Configure RGB timing, data pins, and the 800x480 panel. */
    Display()
    {
        // Configure the RGB data bus and verified synchronization timings.
        auto bus_config = bus.config();
        bus_config.panel = &panel;
        const int8_t data_pins[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
        memcpy(bus_config.pin_data, data_pins, sizeof(data_pins));
        bus_config.pin_henable = 40;
        bus_config.pin_vsync = 41;
        bus_config.pin_hsync = 39;
        bus_config.pin_pclk = 0;
        bus_config.freq_write = 12000000;
        bus_config.hsync_polarity = 0;
        bus_config.hsync_front_porch = 8;
        bus_config.hsync_pulse_width = 4;
        bus_config.hsync_back_porch = 43;
        bus_config.vsync_polarity = 0;
        bus_config.vsync_front_porch = 8;
        bus_config.vsync_pulse_width = 4;
        bus_config.vsync_back_porch = 12;
        bus_config.pclk_active_neg = 1;
        bus_config.de_idle_high = 0;
        bus_config.pclk_idle_high = 0;
        bus.config(bus_config);

        // Keep the frame-buffer geometry equal to the physical panel size.
        auto panel_config = panel.config();
        panel_config.memory_width = screen_width;
        panel_config.memory_height = screen_height;
        panel_config.panel_width = screen_width;
        panel_config.panel_height = screen_height;
        panel.config(panel_config);
        panel.setBus(&bus);
        setPanel(&panel);
    }
};

Display lcd;

/** Return the millisecond time base consumed by LVGL. */
uint32_t lvgl_tick()
{
    return millis();
}

/** Present the full RGB frame buffer selected by LovyanGFX. */
void display_flush(lv_display_t * display, const lv_area_t *, uint8_t * pixel_map)
{
    // Present the full buffer at VSYNC, then let LVGL render the next frame.
    if (!lcd.bus.presentFrameBuffer(pixel_map)) {
        Serial.println("Display frame switch timeout");
    }
    lv_display_flush_ready(display);
}

/** Supply the latest GT911 point and log each new press. */
void touch_read(lv_indev_t *, lv_indev_data_t * data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    static bool was_pressed = false;
    // Preserve the last coordinates while exposing the current press state.
    const bool pressed = touch_read_point(last_x, last_y);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = last_x;
    data->point.y = last_y;
    // Log only the transition into a press to avoid flooding the serial port.
    if (pressed && !was_pressed) {
        Serial.printf("Touch %d,%d\n", last_x, last_y);
        crowpanel::display_power_reset_activity();
    }
    was_pressed = pressed;
}

/** Apply the generated UI's LED request to GPIO 38. */
void set_led(bool enabled)
{
    digitalWrite(led_pin, enabled ? HIGH : LOW);
}

namespace {

constexpr uint32_t ui_task_stack_words = 16384; // 64 KB (FreeRTOS stack size is in words)
constexpr UBaseType_t ui_task_priority = 1;
TaskHandle_t ui_task_handle = nullptr;

/** Dedicated LVGL/photo-viewer task with a generous stack.
 *
 *  Decoding 800x480 JPEGs inside the Arduino loopTask overflows the default
 *  ~8 KB stack, so all rendering and photo loading runs here instead.
 */
    void ui_task(void*)
{
    crowpanel::init_photo_viewer();
    crowpanel::display_power_init();

    for (;;) {
        lv_timer_handler();
        crowpanel::photo_viewer_tick();
        crowpanel::display_power_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

constexpr uint32_t dht20_poll_interval_ms = 30000;
constexpr uint32_t dht20_max_consecutive_failures = 3;
constexpr uint32_t dht20_task_stack_words = 2048; // 8 KB
constexpr UBaseType_t dht20_task_priority = 1;
TaskHandle_t dht20_task_handle = nullptr;

/** Dedicated DHT20 poller pinned to core 0.
 *
 *  dht20_read() blocks ~80 ms per conversion, so it must never run inside
 *  the LVGL ui_task or an LVGL timer. This task reads every 30 s and
 *  publishes the last-good values (or an offline marker) for the overlay.
 */
void dht20_task(void*)
{
    int temperature = 0;
    int humidity = 0;
    bool previously_ok = true;
    uint32_t consecutive_failures = 0;

    for (;;) {
        crowpanel::IndoorClimate climate;
        if (dht20_read(temperature, humidity)) {
            climate.valid = true;
            climate.temperature_c = temperature;
            climate.humidity_pct = humidity;
            climate.updated_ms = millis();
            crowpanel::photo_viewer_set_indoor_climate(climate);
            Serial.printf("[dht20] %d C, %d %%RH\n", temperature, humidity);
            previously_ok = true;
            consecutive_failures = 0;
        } else {
            ++consecutive_failures;
            climate.valid = false;
            climate.temperature_c = 0;
            climate.humidity_pct = 0;
            climate.updated_ms = millis();
            crowpanel::photo_viewer_set_indoor_climate(climate);
            // Announce only the transition into failure so a missing or
            // flaky sensor cannot flood the serial log.
            if (previously_ok) {
                Serial.println("[dht20] read failed; indoor sensor offline");
            }
            previously_ok = false;
        }

        // A sensor that passed the boot probe but then stops answering means
        // a stuck bus; every further transaction makes the ESP-IDF i2c driver
        // log ESP_ERR_INVALID_STATE. Retire after a few misses: the overlay
        // keeps showing "Indoor sensor offline" with no more bus traffic.
        if (consecutive_failures >= dht20_max_consecutive_failures) {
            Serial.printf("[dht20] offline after %u consecutive read failures; I2C traffic stopped\n",
                          consecutive_failures);
            vTaskSuspend(nullptr);
        }

        vTaskDelay(pdMS_TO_TICKS(dht20_poll_interval_ms));
    }
}

constexpr uint32_t weather_task_stack_words = 8192; // 32 KB (TLS handshake + JSON parse)
constexpr UBaseType_t weather_task_priority = 1;
TaskHandle_t weather_task_handle = nullptr;

/** Dedicated Open-Meteo fetcher pinned to core 0.
 *
 *  weather_fetch_blocking() blocks up to ~10 s on the TLS handshake and
 *  HTTPS GET, so it must never run inside the LVGL ui_task or an LVGL timer.
 *  The task waits for WiFi + NTP, publishes a status enum alongside the
 *  last-good values, and retries failures after 120 s instead of a full
 *  WEATHER_REFRESH_MIN cycle.
 */
void weather_task(void*)
{
    // Last-good snapshot; valid stays false until the first successful fetch.
    static crowpanel::WeatherData g_last_good = {};

    // Publish the last-good values with only the status overwritten, so an
    // error or offline stretch never wipes what the card already shows.
    auto publish_with_status = [&](crowpanel::WeatherStatus status) {
        crowpanel::WeatherData snapshot = g_last_good;
        snapshot.status = status;
        snapshot.last_attempt_ms = millis();
        crowpanel::photo_viewer_set_weather(snapshot);
    };

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            publish_with_status(crowpanel::WeatherStatus::NoWifi);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        if (!crowpanel::display_power_is_ntp_synced()) {
            publish_with_status(crowpanel::WeatherStatus::WaitingForTime);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        crowpanel::WeatherData fetched = g_last_good; // keeps last-good if the fetch fails mid-way
        publish_with_status(crowpanel::WeatherStatus::Fetching);
        weather_fetch_blocking(fetched);
        fetched.last_attempt_ms = millis();

        if (fetched.valid) {
            g_last_good = fetched;
            g_last_good.status = crowpanel::WeatherStatus::Ok;
            crowpanel::photo_viewer_set_weather(g_last_good);
            Serial.printf("[weather] published ok (%d C, %s)\n",
                          g_last_good.temperature_c, g_last_good.condition_text);
            vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MIN * 60000UL));
        } else {
            publish_with_status(crowpanel::WeatherStatus::Error);
            Serial.println("[weather] fetch failed; keeping last-good, retry in 120 s");
            vTaskDelay(pdMS_TO_TICKS(120000));
        }
    }
}

}

constexpr uint32_t wifi_timeout_ms = 15000;
lv_obj_t * ip_label = nullptr;

/** Connect to WiFi using WIFI_SSID/WIFI_PASS and reflect status on screen. */
void connect_wifi()
{
    if (strcmp(WIFI_SSID, "CHANGE_ME") == 0) {
        Serial.println(F("[wifi] WIFI_SSID is default; skipping connect."));
        return;
    }
    Serial.printf("[wifi] connecting to '%s' ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("crowpanel-5");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < wifi_timeout_ms) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected! IP=%s  RSSI=%d dBm\n",
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
        if (ip_label) {
            lv_label_set_text_fmt(ip_label, "IP %s", WiFi.localIP().toString().c_str());
        }
    } else {
        Serial.println(F("[wifi] connect failed - continuing offline"));
        if (ip_label) {
            lv_label_set_text(ip_label, "WiFi offline");
        }
    }
}

}

/**
 * @brief Initialize hardware, LVGL callbacks, and generated UI.
 *
 * Called once after reset; the serial log identifies each major subsystem.
 */
void setup()
{
    Serial.begin(115200);
    // Single I2C bus bring-up: GT911 touch (via PCA9557) and the DHT20 share
    // pins 19/20, so Wire.begin must run exactly once, before any device is
    // probed. Re-initializing later would leave the ESP-IDF i2c driver in an
    // invalid state.
    Wire.begin(19, 20);
    delay(50); // let the I2C peripheral settle before the first DHT20 probe

    // Probe the DHT20 once. When it is missing (or the bus is broken) the
    // polling task is never created, so the driver cannot spam
    // ESP_ERR_INVALID_STATE every 30 s.
    const bool dht20_ready = dht20_init();
    if (!dht20_ready) {
        Serial.println("[main] DHT20 init failed; indoor sensor offline");
    }
    pinMode(led_pin, OUTPUT);
    set_led(false);

    if (!lcd.begin()) {
        Serial.println("LovyanGFX lcd.begin() failed");
        return;
    }
    Serial.println("LovyanGFX lcd.begin() OK");
    delay(200);
    pinMode(backlight_pin, OUTPUT);
    digitalWrite(backlight_pin, HIGH);

    lv_init();
    lv_tick_set_cb(lvgl_tick);

    // PSRAM-backed LVGL pool: rotated info-card elements allocate transform
    // layers larger than the built-in 64 KB pool; small allocations keep
    // coming from the internal pool first.
    if (psramFound()) {
        constexpr size_t k_lvgl_psram_pool_size = 1024 * 1024;
        void* pool = ps_malloc(k_lvgl_psram_pool_size);
        if (pool) {
            lv_mem_add_pool(pool, k_lvgl_psram_pool_size);
            Serial.printf("[lvgl] psram pool %u bytes (psram total %u)\n",
                          (unsigned)k_lvgl_psram_pool_size, (unsigned)ESP.getPsramSize());
        } else {
            Serial.println("[lvgl] psram pool alloc failed; internal pool only");
        }
    }

    delay(100);

    touch_init();

    // Attach LovyanGFX's two full frames so LVGL can swap them at VSYNC.
    lv_color_t * frame_buffer_0 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(0));
    lv_color_t * frame_buffer_1 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(1));
    lv_display_t * display = lv_display_create(screen_width, screen_height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, frame_buffer_0, frame_buffer_1,
                           screen_width * screen_height * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    // Register GT911 as LVGL's pointer source for generated UI widgets.
    lv_indev_t * touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);

    ui_init(set_led);

    connect_wifi();

    // Prebuild the Open-Meteo request URL; the fetch task only needs WiFi + NTP.
    if (!weather_init()) {
        Serial.println("[main] weather_init failed; weather card will show offline");
    }

    // Mount the SD card and register its LVGL file-system driver.
    if (crowpanel::init_sd_card()) {
        crowpanel::register_lvgl_sd_fs();
    } else {
        Serial.println("[main] SD card not available; slideshow disabled");
    }

    // Hand rendering/photo decoding to a dedicated task with a larger stack.
    BaseType_t created = xTaskCreatePinnedToCore(
        ui_task, "uiTask", ui_task_stack_words, nullptr,
        ui_task_priority, &ui_task_handle, 1);
    if (created != pdPASS) {
        Serial.println("[main] failed to create UI task");
        return;
    }
    Serial.println("[main] UI task started");

    // Poll the DHT20 on core 0 so its ~80 ms reads never stall LVGL on core 1.
    // Spawn it only after the boot probe succeeded; otherwise the task would
    // keep hitting the dead bus every 30 s and flood the log.
    if (dht20_ready) {
        BaseType_t dht20_created = xTaskCreatePinnedToCore(
            dht20_task, "dht20Task", dht20_task_stack_words, nullptr,
            dht20_task_priority, &dht20_task_handle, 0);
        if (dht20_created != pdPASS) {
            Serial.println("[main] failed to create DHT20 task");
        } else {
            Serial.println("[main] DHT20 task started");
        }
    }

    // Fetch weather on core 0; the ~10 s HTTPS calls must never stall LVGL.
    BaseType_t weather_created = xTaskCreatePinnedToCore(
        weather_task, "weatherTask", weather_task_stack_words, nullptr,
        weather_task_priority, &weather_task_handle, 0);
    if (weather_created != pdPASS) {
        Serial.println("[main] failed to create weather task");
    } else {
        Serial.println("[main] weather task started");
    }

    // The Arduino loopTask is no longer needed; its stack is too small for JPEG decode.
    vTaskDelete(nullptr);
}

void loop()
{
}
