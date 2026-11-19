#include "crowpanel/photo_viewer.h"

#include <time.h>
#include <string.h>

#include <Arduino.h>
#include <SD.h>
#include <lvgl.h>
#include "src/draw/sw/lv_draw_sw.h"
#include "crowpanel/sdcard.h"
#include "crowpanel/display_power.h"

extern "C" {
#include <esp_heap_caps.h>
#include <esp_cache.h>
}

namespace crowpanel {

namespace {

constexpr uint16_t SCREEN_W = 800;
constexpr uint16_t SCREEN_H = 480;
constexpr int SCALE_NONE = 256;

constexpr int16_t SWIPE_THRESHOLD_PX = 60;
constexpr int16_t SWIPE_MAX_VERTICAL_DRIFT_PX = 80;
constexpr uint32_t TAP_MAX_DURATION_MS = 400;
constexpr uint32_t SWIPE_MAX_DURATION_MS = 500;

constexpr uint32_t FADE_DURATION_MS = 0;
constexpr uint32_t FADE_TICK_MS = 16;

PhotoIndex g_index = {};
int g_photo_count = 0;
int g_current_index = 0;
bool g_paused = false;
uint32_t g_last_advance_ms = 0;

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_image = nullptr;
lv_obj_t* g_index_label = nullptr;
lv_obj_t* g_pause_label = nullptr;

lv_color_t* g_photo_buf = nullptr;
lv_image_dsc_t g_photo_dsc = {};

int g_photos_since_info = 0;
bool g_info_showing = false;
uint32_t g_info_show_start_ms = 0;

lv_obj_t* g_info_overlay = nullptr;
lv_obj_t* g_info_image = nullptr;

constexpr uint16_t CARD_W = 480;
constexpr uint16_t CARD_H = 800;
lv_color_t* g_card_portrait_buf = nullptr;
lv_color_t* g_card_frame_buf = nullptr;
lv_image_dsc_t g_card_image_dsc = {};

WeatherData g_cached_weather = {};
IndoorClimate g_cached_climate = {};
portMUX_TYPE g_info_mux = portMUX_INITIALIZER_UNLOCKED;

enum class TouchState { IDLE, PRESSED };
TouchState g_touch_state = TouchState::IDLE;
int16_t g_touch_start_x = 0;
int16_t g_touch_start_y = 0;
uint32_t g_touch_start_ms = 0;

enum class FadeState { VISIBLE, FADING_OUT, LOADING, FADING_IN };
FadeState g_fade_state = FadeState::VISIBLE;
int g_pending_index = -1;
uint32_t g_fade_start_ms = 0;
lv_opa_t g_fade_opa = LV_OPA_COVER;

void apply_opacity()
{
    if (!g_image) {
        return;
    }
    lv_obj_set_style_opa(g_image, g_fade_opa, LV_PART_MAIN);
    lv_obj_set_style_image_opa(g_image, g_fade_opa, LV_PART_MAIN);
}

bool load_raw_photo(const char* raw_path)
{
    if (!g_photo_buf) {
        return false;
    }
    File f = SD.open(raw_path, "r");
    if (!f) {
        Serial.printf("[photo] no raw file: %s\n", raw_path);
        return false;
    }
    const size_t expected = SCREEN_W * SCREEN_H * sizeof(lv_color_t);
    if (f.size() != static_cast<long>(expected)) {
        Serial.printf("[photo] raw size mismatch: %ld != %u\n", f.size(), (unsigned)expected);
        f.close();
        return false;
    }
    const size_t read = f.read(reinterpret_cast<uint8_t*>(g_photo_buf), expected);
    f.close();
    if (read != expected) {
        Serial.printf("[photo] raw read failed: %u != %u\n", (unsigned)read, (unsigned)expected);
        return false;
    }
    esp_cache_msync(g_photo_buf, expected, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    lv_image_set_scale(g_image, SCALE_NONE);
    lv_image_set_rotation(g_image, 0);
    lv_image_set_src(g_image, &g_photo_dsc);
    return true;
}

void load_photo_now(int index)
{
    if (g_photo_count == 0 || index < 0 || index >= g_photo_count) {
        return;
    }
    g_current_index = index;

    const String name = photo_at(g_index, index);
    const String path = String("S:/photos/") + name;
    Serial.printf("[photo] loading %s\n", path.c_str());

    const String raw_path = String("/photos/") + name.substring(0, name.lastIndexOf('.')) + ".raw";
    if (load_raw_photo(raw_path.c_str())) {
        Serial.println("[photo] loaded raw RGB565");
        if (g_index_label) {
            lv_label_set_text_fmt(g_index_label, "%d / %d", index + 1, g_photo_count);
        }
        return;
    }

    lv_image_header_t header;
    lv_result_t info = lv_image_decoder_get_info(path.c_str(), &header);
    if (info != LV_RESULT_OK) {
        Serial.printf("[photo] decoder_get_info failed for %s\n", path.c_str());
        if (g_index_label) {
            lv_label_set_text_fmt(g_index_label, "%d / %d (bad file)", index + 1, g_photo_count);
        }
        return;
    }

    Serial.printf("[photo] header %dx%d cf=%d\n", header.w, header.h, header.cf);
    lv_image_set_src(g_image, path.c_str());
    lv_image_set_scale(g_image, SCALE_NONE);
    lv_image_set_rotation(g_image, 0);

    if (header.w > 0 && header.h > 0) {
        const int32_t scale_x = (int32_t)SCREEN_W * SCALE_NONE / header.w;
        const int32_t scale_y = (int32_t)SCREEN_H * SCALE_NONE / header.h;
        const int32_t scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale != SCALE_NONE) {
            lv_image_set_scale(g_image, (uint32_t)scale);
        }
        Serial.printf("[photo] header %dx%d scale %d\n", header.w, header.h, scale);
    }

    if (g_index_label) {
        lv_label_set_text_fmt(g_index_label, "%d / %d", index + 1, g_photo_count);
    }
}

void load_photo(int index)
{
    if (g_photo_count == 0 || index < 0 || index >= g_photo_count) {
        return;
    }
    load_photo_now(index);
}

void update_fade()
{
    if (g_info_showing) {
        return;
    }
    if (g_fade_state == FadeState::VISIBLE) {
        return;
    }
    if (g_fade_state == FadeState::FADING_OUT || g_fade_state == FadeState::FADING_IN) {
        const uint32_t now = millis();
        const uint32_t elapsed = now - g_fade_start_ms;
        const lv_opa_t target = (g_fade_state == FadeState::FADING_OUT) ? LV_OPA_TRANSP : LV_OPA_COVER;
        const lv_opa_t start = (g_fade_state == FadeState::FADING_OUT) ? LV_OPA_COVER : LV_OPA_TRANSP;
        if (elapsed >= FADE_DURATION_MS) {
            g_fade_opa = target;
        } else {
            g_fade_opa = static_cast<lv_opa_t>(start + (target - start) * elapsed / FADE_DURATION_MS);
        }
        apply_opacity();

        if (elapsed >= FADE_DURATION_MS) {
            if (g_fade_state == FadeState::FADING_OUT) {
                g_fade_state = FadeState::LOADING;
            } else {
                g_fade_state = FadeState::VISIBLE;
            }
        }
    }

    if (g_fade_state == FadeState::LOADING) {
        if (g_pending_index >= 0 && g_pending_index < g_photo_count) {
            load_photo_now(g_pending_index);
        }
        g_pending_index = -1;
        g_fade_state = FadeState::FADING_IN;
        g_fade_start_ms = millis();
    }
}

void update_pause_overlay()
{
    if (!g_pause_label) {
        return;
    }
    if (g_paused) {
        lv_obj_clear_flag(g_pause_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_pause_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void draw_card_label(lv_layer_t* layer, const char* text, int32_t y, const lv_font_t* font)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.color = lv_color_white();
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.opa = LV_OPA_COVER;

    const int32_t band = lv_font_get_line_height(font) + 8;
    lv_area_t area = {0, y - band / 2, CARD_W - 1, y + band / 2};
    layer->_clip_area = (lv_area_t){0, 0, CARD_W - 1, CARD_H - 1};

    lv_draw_label(layer, &dsc, &area);
}

void draw_card_line(lv_layer_t* layer, int32_t y, int32_t width)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(0x58C4DC);
    dsc.width = 2;
    dsc.opa = LV_OPA_50;
    dsc.round_start = 0;
    dsc.round_end = 0;
    dsc.p1.x = (CARD_W - width) / 2;
    dsc.p1.y = y;
    dsc.p2.x = CARD_W - dsc.p1.x;
    dsc.p2.y = y;
    layer->_clip_area = (lv_area_t){0, 0, CARD_W - 1, CARD_H - 1};

    lv_draw_line(layer, &dsc);
}

void render_info_card()
{
    if (!g_card_portrait_buf || !g_card_frame_buf) {
        return;
    }

    lv_obj_t* canvas = lv_canvas_create(nullptr);
    lv_canvas_set_buffer(canvas, g_card_portrait_buf, CARD_W, CARD_H, LV_COLOR_FORMAT_RGB565);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color = lv_color_hex(0x0B1020);
    bg_dsc.bg_opa = LV_OPA_COVER;
    bg_dsc.radius = 0;
    lv_area_t full = {0, 0, CARD_W - 1, CARD_H - 1};
    layer._clip_area = full;
    lv_draw_rect(&layer, &bg_dsc, &full);

    const bool has_time = display_power_is_ntp_synced();

    char date_buf[48];
    if (has_time) {
        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        strftime(date_buf, sizeof(date_buf), "%A, %B %d", &local);
    } else {
        strlcpy(date_buf, "Saturday, November 09", sizeof(date_buf));
    }
    draw_card_label(&layer, date_buf, 40, &lv_font_montserrat_24);

    char clock_buf[16];
    if (has_time) {
        const time_t now = time(nullptr);
        struct tm local;
        localtime_r(&now, &local);
        strftime(clock_buf, sizeof(clock_buf), "%H:%M", &local);
    } else {
        strlcpy(clock_buf, "21:47", sizeof(clock_buf));
    }
    draw_card_label(&layer, clock_buf, 110, &lv_font_montserrat_40);

    WeatherData weather;
    char temp_buf[16];
    char cond_buf[32];
    if (photo_viewer_get_weather(weather)) {
        snprintf(temp_buf, sizeof(temp_buf), "%d C", weather.temperature_c);
        strlcpy(cond_buf, weather.condition_text, sizeof(cond_buf));
    } else {
        strlcpy(temp_buf, "27 C", sizeof(temp_buf));
        strlcpy(cond_buf, "Clear sky", sizeof(cond_buf));
    }
    draw_card_label(&layer, temp_buf, 220, &lv_font_montserrat_40);
    draw_card_label(&layer, cond_buf, 260, &lv_font_montserrat_24);

    draw_card_line(&layer, 320, 360);

    IndoorClimate climate;
    char climate_buf[48];
    if (photo_viewer_get_indoor_climate(climate)) {
        snprintf(climate_buf, sizeof(climate_buf), "Indoor: %dC, %d%%", climate.temperature_c, climate.humidity_pct);
    } else {
        strlcpy(climate_buf, "Indoor sensor offline", sizeof(climate_buf));
    }
    draw_card_label(&layer, climate_buf, 380, &lv_font_montserrat_24);

    draw_card_label(&layer, "Weather data by Open-Meteo.com (CC BY 4.0)", 770, &lv_font_montserrat_14);

    lv_canvas_finish_layer(canvas, &layer);

    const lv_draw_buf_t* canvas_draw_buf = lv_canvas_get_draw_buf(canvas);
    const uint32_t src_stride = canvas_draw_buf ? canvas_draw_buf->header.stride : CARD_W * sizeof(lv_color_t);
    Serial.printf("[photo] canvas stride=%u\n", (unsigned)src_stride);

    const size_t portrait_bytes = CARD_W * CARD_H * sizeof(lv_color_t);
    esp_cache_msync(g_card_portrait_buf, portrait_bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    lv_draw_sw_rotate(canvas_draw_buf ? canvas_draw_buf->data : reinterpret_cast<const uint8_t*>(g_card_portrait_buf),
                       g_card_frame_buf,
                       CARD_W, CARD_H,
                       src_stride,
                       SCREEN_W * sizeof(lv_color_t),
                       LV_DISPLAY_ROTATION_90,
                       LV_COLOR_FORMAT_RGB565);

    const size_t frame_bytes = SCREEN_W * SCREEN_H * sizeof(lv_color_t);
    esp_cache_msync(g_card_frame_buf, frame_bytes,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    lv_obj_delete(canvas);

    lv_obj_invalidate(g_info_image);
}

void show_info_overlay()
{
    if (!g_info_overlay || g_info_showing) {
        return;
    }
    render_info_card();
    lv_obj_move_foreground(g_info_overlay);
    lv_obj_clear_flag(g_info_overlay, LV_OBJ_FLAG_HIDDEN);
    g_info_showing = true;
    g_info_show_start_ms = millis();
    Serial.println("[info] overlay shown");
}

void hide_info_overlay()
{
    if (!g_info_showing) {
        return;
    }
    g_info_showing = false;
    if (g_info_overlay) {
        lv_obj_add_flag(g_info_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    g_photos_since_info = 0;
    g_last_advance_ms = millis();
    Serial.println("[info] overlay dismissed");
}

void shuffle_photos()
{
    if (g_photo_count <= 1) {
        return;
    }
    shuffle_offsets(g_index.offsets, g_index.count, []() -> uint32_t { return esp_random(); });
}

void advance_photo(int delta)
{
    if (g_photo_count == 0) {
        return;
    }
    g_photos_since_info++;
    if (g_photos_since_info >= INFO_EVERY_N_PHOTOS) {
        show_info_overlay();
    }
    int next = g_current_index + delta;
    if (next >= g_photo_count) {
        next = 0;
    } else if (next < 0) {
        next = g_photo_count - 1;
    }
    load_photo(next);
}

void on_press(const lv_indev_t* indev)
{
    if (g_info_showing) {
        return;
    }
    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    if (abs(vect.x) > 2 || abs(vect.y) > 2) {
        return;
    }
    lv_point_t pos;
    lv_indev_get_point(indev, &pos);
    g_touch_start_x = pos.x;
    g_touch_start_y = pos.y;
    g_touch_start_ms = millis();
    g_touch_state = TouchState::PRESSED;
}

void on_release(const lv_indev_t* indev)
{
    if (g_info_showing) {
        g_touch_state = TouchState::IDLE;
        return;
    }
    if (g_touch_state != TouchState::PRESSED) {
        return;
    }
    g_touch_state = TouchState::IDLE;

    lv_point_t pos;
    lv_indev_get_point(indev, &pos);
    const int16_t dx = pos.x - g_touch_start_x;
    const int16_t dy = pos.y - g_touch_start_y;
    const uint32_t elapsed = millis() - g_touch_start_ms;

    if (elapsed <= SWIPE_MAX_DURATION_MS && abs(dx) >= SWIPE_THRESHOLD_PX && abs(dy) <= SWIPE_MAX_VERTICAL_DRIFT_PX) {
        if (dx > 0) {
            Serial.println("[touch] swipe right -> previous");
            photo_viewer_previous();
        } else {
            Serial.println("[touch] swipe left -> next");
            photo_viewer_next();
        }
        return;
    }

    if (elapsed <= TAP_MAX_DURATION_MS && abs(dx) < SWIPE_THRESHOLD_PX && abs(dy) < SWIPE_THRESHOLD_PX) {
        Serial.println("[touch] tap -> toggle pause");
        photo_viewer_toggle_pause();
        update_pause_overlay();
        return;
    }

    if (elapsed > TAP_MAX_DURATION_MS && abs(dx) < SWIPE_THRESHOLD_PX && abs(dy) < SWIPE_THRESHOLD_PX) {
        Serial.println("[touch] long press -> test backlight off");
        display_power_test_off(5000);
        return;
    }
}

} // namespace

bool init_photo_viewer()
{
    uint32_t t0 = millis();
    g_photo_count = load_photo_index("/photos", g_index, /*reserve_bytes=*/ 3UL * SCREEN_W * SCREEN_H * sizeof(lv_color_t), MAX_PHOTOS_CEILING);
    Serial.printf("[photo] loaded %d photo(s) in %lu ms\n", g_photo_count, (unsigned long)(millis() - t0));

    if (g_index.count > 0 && g_index.count < g_index.on_card_count) {
      Serial.printf("[photo] truncated: loaded %d of %d (PSRAM ceiling reached)\n", g_index.count, g_index.on_card_count);
    }
    if (g_index.count == 0 && g_index.on_card_count > 0) {
      Serial.println("[photo] PSRAM allocation failed: cannot load any filenames");
    }

    if (g_photo_count > 0) {
        shuffle_photos();
        Serial.println("[photo] shuffled photo order");
    }

    g_screen = lv_obj_create(nullptr);
    lv_obj_set_size(g_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), LV_PART_MAIN);
    lv_screen_load(g_screen);

    if (g_photo_count <= 0) {
        lv_obj_t* label = lv_label_create(g_screen);
        lv_label_set_text(label, "No photos in /photos");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(label);
        return false;
    }

    g_image = lv_image_create(g_screen);
    lv_obj_set_size(g_image, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(g_image, 0, 0);
    lv_image_set_inner_align(g_image, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_antialias(g_image, true);

    g_index_label = lv_label_create(g_screen);
    lv_obj_set_style_text_color(g_index_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_index_label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_index_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_index_label, 4, LV_PART_MAIN);
    lv_obj_align(g_index_label, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    g_pause_label = lv_label_create(g_screen);
    lv_label_set_text(g_pause_label, LV_SYMBOL_PAUSE " PAUSED");
    lv_obj_set_style_text_color(g_pause_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_pause_label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_pause_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_pause_label, 8, LV_PART_MAIN);
    lv_obj_align(g_pause_label, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_add_flag(g_pause_label, LV_OBJ_FLAG_HIDDEN);

    g_info_overlay = lv_obj_create(g_screen);
    lv_obj_set_size(g_info_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(g_info_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_info_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_info_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_info_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_info_overlay, LV_OBJ_FLAG_HIDDEN);

    const size_t photo_bytes = SCREEN_W * SCREEN_H * sizeof(lv_color_t);
    const size_t portrait_bytes = CARD_W * CARD_H * sizeof(lv_color_t);
    const size_t frame_bytes = SCREEN_W * SCREEN_H * sizeof(lv_color_t);
    g_photo_buf = static_cast<lv_color_t*>(heap_caps_malloc(photo_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_card_portrait_buf = static_cast<lv_color_t*>(heap_caps_malloc(portrait_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_card_frame_buf = static_cast<lv_color_t*>(heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_photo_buf || !g_card_portrait_buf || !g_card_frame_buf) {
        Serial.println("[photo] failed to allocate PSRAM buffers");
        return false;
    }
    Serial.printf("[photo] PSRAM buffers: photo %u, card %u + %u bytes\n",
                  (unsigned)photo_bytes, (unsigned)portrait_bytes, (unsigned)frame_bytes);

    lv_memset(g_photo_buf, 0, photo_bytes);
    g_photo_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_photo_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_photo_dsc.header.w = SCREEN_W;
    g_photo_dsc.header.h = SCREEN_H;
    g_photo_dsc.header.stride = SCREEN_W * sizeof(lv_color_t);
    g_photo_dsc.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    g_photo_dsc.data_size = photo_bytes;
    g_photo_dsc.data = reinterpret_cast<const uint8_t*>(g_photo_buf);

    lv_memset(g_card_frame_buf, 0, frame_bytes);
    g_card_image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_card_image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    g_card_image_dsc.header.w = SCREEN_W;
    g_card_image_dsc.header.h = SCREEN_H;
    g_card_image_dsc.header.stride = SCREEN_W * sizeof(lv_color_t);
    g_card_image_dsc.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    g_card_image_dsc.data_size = frame_bytes;
    g_card_image_dsc.data = reinterpret_cast<const uint8_t*>(g_card_frame_buf);

    g_info_image = lv_image_create(g_info_overlay);
    lv_obj_set_size(g_info_image, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(g_info_image, 0, 0);
    lv_image_set_src(g_info_image, &g_card_image_dsc);

    g_current_index = -1;
    g_last_advance_ms = millis();
    g_touch_state = TouchState::IDLE;
    load_photo(0);

    lv_obj_add_event_cb(g_screen, photo_viewer_handle_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_screen, photo_viewer_handle_event, LV_EVENT_RELEASED, nullptr);
    return true;
}

void photo_viewer_next()
{
    advance_photo(1);
    g_last_advance_ms = millis();
}

void photo_viewer_previous()
{
    advance_photo(-1);
    g_last_advance_ms = millis();
}

void photo_viewer_toggle_pause()
{
    g_paused = !g_paused;
    Serial.printf("[photo] %s\n", g_paused ? "paused" : "resumed");
}

bool photo_viewer_is_paused()
{
    return g_paused;
}

int photo_viewer_count()
{
    return g_photo_count;
}

void photo_viewer_handle_event(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        on_press(lv_indev_active());
    } else if (code == LV_EVENT_RELEASED) {
        on_release(lv_indev_active());
    }
}

void photo_viewer_tick()
{
    update_fade();

    if (g_info_showing) {
        if (millis() - g_info_show_start_ms >= INFO_HOLD_MS) {
            hide_info_overlay();
        }
        return;
    }

    if (g_paused || g_photo_count == 0) {
        return;
    }
    const uint32_t now = millis();
    if (now - g_last_advance_ms >= PHOTO_INTERVAL_MS) {
        g_last_advance_ms = now;
        advance_photo(1);
    }
}

void photo_viewer_set_weather(const WeatherData& data)
{
    portENTER_CRITICAL(&g_info_mux);
    g_cached_weather = data;
    portEXIT_CRITICAL(&g_info_mux);
}

void photo_viewer_set_indoor_climate(const IndoorClimate& data)
{
    portENTER_CRITICAL(&g_info_mux);
    g_cached_climate = data;
    portEXIT_CRITICAL(&g_info_mux);
}

bool photo_viewer_get_weather(WeatherData& out)
{
    portENTER_CRITICAL(&g_info_mux);
    out = g_cached_weather;
    portEXIT_CRITICAL(&g_info_mux);
    return out.valid;
}

bool photo_viewer_get_indoor_climate(IndoorClimate& out)
{
    portENTER_CRITICAL(&g_info_mux);
    out = g_cached_climate;
    portEXIT_CRITICAL(&g_info_mux);
    return out.valid;
}

void photo_viewer_show_info()
{
    show_info_overlay();
}

} // namespace crowpanel
