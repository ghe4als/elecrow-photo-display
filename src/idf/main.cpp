// ESP32 Starter - ESP-IDF framework
// ----------------------------------------------------------------------------
// Equivalent blink+log example using the official ESP-IDF APIs (FreeRTOS task).
// Build with:  pio run -e esp32dev-idf
// ----------------------------------------------------------------------------

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

static const char* TAG = "starter";
static const gpio_num_t LED_GPIO = GPIO_NUM_2;

extern "C" void app_main(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "booting (ESP-IDF)");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    int level = 0;
    while (true) {
        gpio_set_level(LED_GPIO, level);
        ESP_LOGI(TAG, "LED=%d", level);
        level = !level;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}