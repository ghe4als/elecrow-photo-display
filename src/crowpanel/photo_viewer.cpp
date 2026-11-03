#include "crowpanel/photo_viewer.h"

#include <time.h>

#include <lvgl.h>
#include "crowpanel/sdcard.h"
#include "crowpanel/display_power.h"

namespace crowpanel {

namespace {

constexpr uint16_t SCREEN_W = 800;
constexpr uint16_t SCREEN_H = 480;
constexpr int SCALE_NONE = 256;

constexpr int16_t SWIPE_THRESHOLD_PX = 60;
constexpr int16_t SWIPE_MAX_VERTICAL_DRIFT_PX = 80;
constexpr uint32_t TAP_MAX_DURATION_MS = 400;
constexpr uint32_t SWIPE_MAX_DURATION_MS = 500;

constexpr uint32_t FADE_DURATION_MS = 350;
constexpr uint32_t FADE_TICK_MS = 16;

String g_photos[MAX_PHOTOS];
int g_photo_count = 0;
int g_current_index = 0;
bool g_paused = false;
uint32_t g_last_advance_ms = 0;

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_image = nullptr;
lv_obj_t* g_index_label = nullptr;
lv_obj_t* g_pause_label = nullptr;

int g_photos_since_info = 0;
bool g_info_showing = false;
uint32_t g_info_show_start_ms = 0;

lv_obj_t* g_info_overlay = nullptr;
lv_obj_t* g_info_date_label = nullptr;
lv_obj_t* g_info_weather_label = nullptr;
lv_obj_t* g_info_climate_label = nullptr;
lv_obj_t* g_info_attribution_label = nullptr;

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

void load_photo_now(int index)
{
    if (g_photo_count == 0 || index < 0 || index >= g_photo_count) {
        return;
    }
    g_current_index = index;

    const String path = String("S:/photos") + g_photos[index];
    Serial.printf("[photo] loading %s\n", path.c_str());

    lv_image_header_t header;
    lv_result_t info = lv_image_decoder_get_info(path.c_str(), &header);
    if (info != LV_RESULT_OK) {
        Serial.printf("[photo] decoder_get_info failed for %s\n", path.c_str());
        lv_image_set_scale(g_image, SCALE_NONE);
        apply_opacity();
        if (g_index_label) {
            lv_label_set_text_fmt(g_index_label, "%d / %d (bad file)", index + 1, g_photo_count);
        }
        return;
    }

    Serial.printf("[photo] header %dx%d cf=%d\n", header.w, header.h, header.cf);
    lv_image_set_src(g_image, path.c_str());

    if (header.w > 0 && header.h > 0) {
        const int32_t scale_x = (int32_t)SCREEN_W * SCALE_NONE / header.w;
        const int32_t scale_y = (int32_t)SCREEN_H * SCALE_NONE / header.h;
        const int32_t scale = (scale_x < scale_y) ? scale_x : scale_y;
        lv_image_set_scale(g_image, (uint32_t)scale);
        lv_image_set_rotation(g_image, 0);
        Serial.printf("[photo] header %dx%d scale %d\n", header.w, header.h, scale);
    } else {
        lv_image_set_scale(g_image, SCALE_NONE);
        lv_image_set_rotation(g_image, 0);
    }

    apply_opacity();

    if (g_index_label) {
        lv_label_set_text_fmt(g_index_label, "%d / %d", index + 1, g_photo_count);
    }
}

void load_photo(int index)
{
    if (g_photo_count == 0 || index < 0 || index >= g_photo_count) {
        return;
    }
    if (!g_image) {
        load_photo_now(index);
        return;
    }
    g_pending_index = index;
    g_fade_state = FadeState::FADING_OUT;
    g_fade_start_ms = millis();
    g_fade_opa = LV_OPA_COVER;
    apply_opacity();
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

void render_info_labels()
{
    if (g_info_date_label) {
        if (display_power_is_ntp_synced()) {
            char buf[48];
            const time_t now = time(nullptr);
            struct tm local;
            localtime_r(&now, &local);
            strftime(buf, sizeof(buf), "%A, %B %d", &local);
            lv_label_set_text(g_info_date_label, buf);
        } else {
            lv_label_set_text(g_info_date_label, "Date unavailable");
        }
    }

    if (g_info_weather_label) {
        WeatherData weather;
        if (photo_viewer_get_weather(weather)) {
            lv_label_set_text_fmt(g_info_weather_label, "Outside: %dC, %s", weather.temperature_c, weather.condition_text);
        } else {
            lv_label_set_text(g_info_weather_label, "Weather unavailable");
        }
    }

    if (g_info_climate_label) {
        IndoorClimate climate;
        if (photo_viewer_get_indoor_climate(climate)) {
            lv_label_set_text_fmt(g_info_climate_label, "Indoor: %dC, %d%% humidity", climate.temperature_c, climate.humidity_pct);
        } else {
            lv_label_set_text(g_info_climate_label, "Indoor sensor offline");
        }
    }
}

void show_info_overlay()
{
    if (!g_info_overlay || g_info_showing) {
        return;
    }
    render_info_labels();
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
    randomSeed(esp_random());
    for (int i = g_photo_count - 1; i > 0; --i) {
        const int j = random(i + 1);
        const String tmp = g_photos[i];
        g_photos[i] = g_photos[j];
        g_photos[j] = tmp;
    }
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
    g_photo_count = list_photo_files("/photos", g_photos, MAX_PHOTOS);
    Serial.printf("[photo] found %d photo(s) in /photos\n", g_photo_count);

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
    lv_obj_set_style_bg_opa(g_info_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_info_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_info_overlay, LV_OBJ_FLAG_HIDDEN);

    g_info_date_label = lv_label_create(g_info_overlay);
    lv_obj_set_style_text_color(g_info_date_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_info_date_label, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_label_set_text(g_info_date_label, "Date unavailable");
    lv_obj_align(g_info_date_label, LV_ALIGN_TOP_MID, 0, 48);

    g_info_weather_label = lv_label_create(g_info_overlay);
    lv_obj_set_style_text_color(g_info_weather_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_info_weather_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(g_info_weather_label, "Weather unavailable");
    lv_obj_align(g_info_weather_label, LV_ALIGN_TOP_MID, 0, 160);

    g_info_climate_label = lv_label_create(g_info_overlay);
    lv_obj_set_style_text_color(g_info_climate_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_info_climate_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(g_info_climate_label, "Indoor sensor offline");
    lv_obj_align(g_info_climate_label, LV_ALIGN_TOP_MID, 0, 200);

    g_info_attribution_label = lv_label_create(g_info_overlay);
    lv_obj_set_style_text_color(g_info_attribution_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_info_attribution_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(g_info_attribution_label, "Weather data by Open-Meteo.com (CC BY 4.0)");
    lv_obj_align(g_info_attribution_label, LV_ALIGN_BOTTOM_MID, 0, -24);

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
