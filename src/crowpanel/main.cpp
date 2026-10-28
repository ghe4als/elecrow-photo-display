#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

#include "wifi_config.h"
#include "crowpanel/touch.h"
#include "crowpanel/ui.h"
#include "crowpanel/sdcard.h"
#include "crowpanel/photo_viewer.h"
#include "crowpanel/display_power.h"
#include "crowpanel/display.h"

/*---------------------------------------------------------------
 * PlatformIO RGB/LVGL application
 * Configure the 800x480 panel, sensor, touch input, and generated UI.
 *--------------------------------------------------------------*/
namespace {
constexpr uint16_t screen_width = crowpanel::display_width;
constexpr uint16_t screen_height = crowpanel::display_height;
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

        auto panel_config = panel.config();
        panel_config.memory_width = crowpanel::panel_width;
        panel_config.memory_height = crowpanel::panel_height;
        panel_config.panel_width = crowpanel::panel_width;
        panel_config.panel_height = crowpanel::panel_height;
        panel_config.offset_rotation = 1;
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
    Wire.begin(19, 20);
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
    delay(100);

    touch_init();

    // Attach LovyanGFX's two full frames so LVGL can swap them at VSYNC.
    lv_color_t * frame_buffer_0 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(0));
    lv_color_t * frame_buffer_1 = reinterpret_cast<lv_color_t *>(lcd.bus.getFrameBuffer(1));
    lv_display_t * display = lv_display_create(screen_width, screen_height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, frame_buffer_0, frame_buffer_1,
                           crowpanel::panel_width * crowpanel::panel_height * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);

    // Register GT911 as LVGL's pointer source for generated UI widgets.
    lv_indev_t * touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);

    ui_init(set_led);

    connect_wifi();

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

    // The Arduino loopTask is no longer needed; its stack is too small for JPEG decode.
    vTaskDelete(nullptr);
}

void loop()
{
}
