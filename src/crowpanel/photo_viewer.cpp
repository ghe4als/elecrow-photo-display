#include "crowpanel/photo_viewer.h"

#include <time.h>

#include <lvgl.h>
#include "crowpanel/sdcard.h"
#include "crowpanel/display_power.h"
#include "crowpanel/dht20.h"
#include "weather_utils.h"

extern "C" {
LV_FONT_DECLARE(lv_font_clock_80);
}

namespace crowpanel {

namespace {

constexpr uint16_t SCREEN_W = 800;
constexpr uint16_t SCREEN_H = 480;
constexpr int SCALE_NONE = 256;

constexpr int32_t LOGICAL_W = 480;
constexpr int32_t LOGICAL_H = 800;
constexpr int32_t INFO_CARD_ROTATION = 900;
static_assert(LOGICAL_W == SCREEN_H && LOGICAL_H == SCREEN_W,
              "portrait logical space must mirror the framebuffer");

#if ENABLE_DHT20
constexpr const char* kIndoorOfflineText = "Indoor sensor offline";
#else
constexpr const char* kIndoorOfflineText = "Indoor sensor not installed";
#endif

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
lv_obj_t* g_info_weekday_label = nullptr;
lv_obj_t* g_info_date_label = nullptr;
lv_obj_t* g_info_clock_label = nullptr;
lv_obj_t* g_info_weather_icon = nullptr;
lv_obj_t* g_info_temp_label = nullptr;
lv_obj_t* g_info_condition_label = nullptr;
lv_obj_t* g_info_divider = nullptr;
lv_obj_t* g_info_indoor_label = nullptr;
lv_obj_t* g_info_status_label = nullptr;
lv_obj_t* g_info_attribution_label = nullptr;
WeatherIcon g_weather_icon = WeatherIcon::Unknown;
int g_clock_rendered_minute = -2;

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

void place_rotated(lv_obj_t* obj, int32_t cx, int32_t cy);

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
            place_rotated(g_index_label, 430, 762);
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
        place_rotated(g_index_label, 430, 762);
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

lv_color_t accent_color()
{
    return lv_color_hex(0x58C4DC);
}

lv_color_t body_text_color()
{
    return lv_color_hex(0xF2F5F7);
}

lv_color_t muted_text_color()
{
    return lv_color_hex(0x8A97A6);
}

void place_rotated(lv_obj_t* obj, int32_t cx, int32_t cy)
{
    if (!obj) {
        return;
    }
    lv_obj_update_layout(obj);
    const int32_t w = lv_obj_get_width(obj);
    const int32_t h = lv_obj_get_height(obj);
    const int32_t sx = static_cast<int32_t>(SCREEN_W) - cy;
    const int32_t sy = cx;
    lv_obj_set_pos(obj, sx - w / 2, sy - h / 2);
    lv_obj_set_style_transform_pivot_x(obj, w / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(obj, h / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(obj, INFO_CARD_ROTATION, LV_PART_MAIN);
}

lv_obj_t* make_card_label(const lv_font_t* font, lv_color_t color, const char* text)
{
    lv_obj_t* label = lv_label_create(g_info_overlay);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

void draw_disc(lv_layer_t* layer, int32_t x, int32_t y, int32_t r, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    const lv_area_t area = {x - r, y - r, x + r, y + r};
    lv_draw_rect(layer, &dsc, &area);
}

void draw_bar(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

void draw_cloud(lv_layer_t* layer, int32_t cx, int32_t cy, lv_color_t color)
{
    draw_disc(layer, cx - 18, cy + 1, 13, color);
    draw_disc(layer, cx + 2, cy - 5, 17, color);
    draw_disc(layer, cx + 21, cy + 2, 12, color);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 8;
    const lv_area_t base = {cx - 30, cy + 1, cx + 33, cy + 14};
    lv_draw_rect(layer, &dsc, &base);
}

void draw_sun(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius, bool with_rays)
{
    draw_disc(layer, cx, cy, radius, accent_color());
    if (!with_rays) {
        return;
    }
    static const int32_t dirs[8][2] = {
        {1000, 0}, {707, 707}, {0, 1000}, {-707, 707},
        {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707},
    };
    const int32_t inner = radius + 8;
    const int32_t outer = radius + 22;
    for (int i = 0; i < 8; ++i) {
        draw_bar(layer,
                 cx + dirs[i][0] * inner / 1000,
                 cy + dirs[i][1] * inner / 1000,
                 cx + dirs[i][0] * outer / 1000,
                 cy + dirs[i][1] * outer / 1000,
                 4, accent_color());
    }
}

void weather_icon_draw_cb(lv_event_t* event)
{
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int32_t cx = coords.x1 + 56;
    const int32_t cy = coords.y1 + 56;

    switch (g_weather_icon) {
    case WeatherIcon::Clear:
        draw_sun(layer, cx, cy, 24, true);
        break;
    case WeatherIcon::PartlyCloudy:
        draw_sun(layer, cx - 16, cy - 14, 13, true);
        draw_cloud(layer, cx + 4, cy + 8, body_text_color());
        break;
    case WeatherIcon::Cloudy:
        draw_cloud(layer, cx - 10, cy - 10, muted_text_color());
        draw_cloud(layer, cx + 8, cy + 8, body_text_color());
        break;
    case WeatherIcon::Fog:
        draw_cloud(layer, cx, cy - 10, body_text_color());
        for (int i = 0; i < 3; ++i) {
            const int32_t y = cy + 14 + i * 10;
            draw_bar(layer, cx - 30, y, cx + 30, y, 4, accent_color());
        }
        break;
    case WeatherIcon::Drizzle:
        draw_cloud(layer, cx, cy - 8, body_text_color());
        for (int i = 0; i < 3; ++i) {
            const int32_t x = cx - 20 + i * 20;
            draw_bar(layer, x, cy + 18, x - 4, cy + 27, 3, accent_color());
        }
        break;
    case WeatherIcon::Rain:
        draw_cloud(layer, cx, cy - 8, body_text_color());
        for (int i = 0; i < 4; ++i) {
            const int32_t x = cx - 24 + i * 16;
            draw_bar(layer, x, cy + 18, x - 7, cy + 34, 3, accent_color());
        }
        break;
    case WeatherIcon::Snow:
        draw_cloud(layer, cx, cy - 10, body_text_color());
        for (int i = 0; i < 3; ++i) {
            const int32_t x = cx - 20 + i * 20;
            const int32_t y = cy + 28;
            draw_bar(layer, x - 6, y, x + 6, y, 2, body_text_color());
            draw_bar(layer, x - 5, y - 5, x + 5, y + 5, 2, body_text_color());
            draw_bar(layer, x - 5, y + 5, x + 5, y - 5, 2, body_text_color());
        }
        break;
    case WeatherIcon::Storm: {
        draw_cloud(layer, cx, cy - 10, body_text_color());
        lv_draw_triangle_dsc_t tri;
        lv_draw_triangle_dsc_init(&tri);
        tri.bg_color = accent_color();
        tri.bg_opa = LV_OPA_COVER;
        tri.p[0].x = cx + 8;
        tri.p[0].y = cy + 8;
        tri.p[1].x = cx - 12;
        tri.p[1].y = cy + 36;
        tri.p[2].x = cx + 10;
        tri.p[2].y = cy + 32;
        lv_draw_triangle(layer, &tri);
        break;
    }
    case WeatherIcon::Unknown:
    default: {
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = accent_color();
        arc.width = 4;
        arc.radius = 28;
        arc.center.x = cx;
        arc.center.y = cy;
        arc.start_angle = 0;
        arc.end_angle = 360;
        arc.rounded = 1;
        lv_draw_arc(layer, &arc);

        lv_draw_label_dsc_t label;
        lv_draw_label_dsc_init(&label);
        label.text = "?";
        label.font = &lv_font_montserrat_40;
        label.color = body_text_color();
        label.align = LV_TEXT_ALIGN_CENTER;
        const lv_area_t area = {coords.x1, coords.y1, coords.x1 + 111, coords.y1 + 111};
        lv_draw_label(layer, &label, &area);
        break;
    }
    }
}

bool get_local_time(struct tm& local)
{
    if (!display_power_is_ntp_synced()) {
        return false;
    }
    const time_t now = time(nullptr);
    localtime_r(&now, &local);
    return true;
}

int current_logical_minute()
{
    struct tm local;
    if (!get_local_time(local)) {
        return -1;
    }
    return local.tm_hour * 60 + local.tm_min;
}

void render_clock_label()
{
    if (!g_info_clock_label) {
        return;
    }
    char buf[8];
    struct tm local;
    if (get_local_time(local)) {
        strftime(buf, sizeof(buf), "%H:%M", &local);
        g_clock_rendered_minute = local.tm_hour * 60 + local.tm_min;
    } else {
        strlcpy(buf, "--:--", sizeof(buf));
        g_clock_rendered_minute = -1;
    }
    lv_label_set_text(g_info_clock_label, buf);
    place_rotated(g_info_clock_label, 240, 220);
}

void refresh_clock_if_minute_changed()
{
    if (!g_info_clock_label) {
        return;
    }
    if (current_logical_minute() != g_clock_rendered_minute) {
        render_clock_label();
    }
}

void render_weather_icon(const WeatherData& weather, bool has_weather)
{
    if (!g_info_weather_icon) {
        return;
    }
    const WeatherIcon icon = has_weather ? wmo_weather_code_to_icon(weather.wmo_code)
                                         : WeatherIcon::Unknown;
    if (icon != g_weather_icon) {
        g_weather_icon = icon;
        lv_obj_invalidate(g_info_weather_icon);
    }
}

void render_info_card()
{
    WeatherData weather;
    const bool has_weather = photo_viewer_get_weather(weather);
    render_weather_icon(weather, has_weather);

    struct tm local;
    const bool has_time = get_local_time(local);

    if (g_info_weekday_label) {
        if (has_time) {
            char buf[32];
            strftime(buf, sizeof(buf), "%A", &local);
            for (char* p = buf; *p != '\0'; ++p) {
                if (*p >= 'a' && *p <= 'z') {
                    *p = static_cast<char>(*p - 'a' + 'A');
                }
            }
            lv_label_set_text(g_info_weekday_label, buf);
        } else {
            lv_label_set_text(g_info_weekday_label, "TODAY");
        }
        place_rotated(g_info_weekday_label, 240, 80);
    }

    if (g_info_date_label) {
        if (has_time) {
            char buf[32];
            strftime(buf, sizeof(buf), "%B %d", &local);
            lv_label_set_text(g_info_date_label, buf);
        } else {
            lv_label_set_text(g_info_date_label, "Date unavailable");
        }
        place_rotated(g_info_date_label, 240, 116);
    }

    render_clock_label();

    if (g_info_temp_label) {
        if (has_weather) {
            lv_label_set_text_fmt(g_info_temp_label, "%dC", weather.temperature_c);
        } else {
            lv_label_set_text(g_info_temp_label, "--C");
        }
        place_rotated(g_info_temp_label, 320, 390);
    }

    if (g_info_condition_label) {
        const char* condition = has_weather && weather.condition_text[0] != '\0'
                                    ? weather.condition_text
                                    : "No weather";
        char buf[17];
        snprintf(buf, sizeof(buf), "%.16s", condition);
        lv_label_set_text(g_info_condition_label, buf);
        place_rotated(g_info_condition_label, 240, 490);
    }

    if (g_info_indoor_label) {
        IndoorClimate climate;
        if (photo_viewer_get_indoor_climate(climate)) {
            lv_label_set_text_fmt(g_info_indoor_label, "Indoor %dC  %d%%",
                                  climate.temperature_c, climate.humidity_pct);
        } else {
            lv_label_set_text(g_info_indoor_label, kIndoorOfflineText);
        }
        place_rotated(g_info_indoor_label, 240, 610);
    }

    if (g_info_status_label) {
        char buf[64];
        const uint32_t age_min = has_weather ? (millis() - weather.last_success_ms) / 60000u : 0;
        switch (weather.status) {
        case WeatherStatus::NoWifi:
            strlcpy(buf, "No WiFi", sizeof(buf));
            break;
        case WeatherStatus::WaitingForTime:
            strlcpy(buf, "Waiting for time sync", sizeof(buf));
            break;
        case WeatherStatus::Fetching:
            strlcpy(buf, has_weather ? "Updating..." : "Fetching weather...", sizeof(buf));
            break;
        case WeatherStatus::Ok:
            snprintf(buf, sizeof(buf), "Updated %um ago", static_cast<unsigned>(age_min));
            break;
        case WeatherStatus::Error:
            if (has_weather) {
                snprintf(buf, sizeof(buf), "Updated %um ago (retrying)",
                         static_cast<unsigned>(age_min));
            } else {
                strlcpy(buf, "Weather unavailable", sizeof(buf));
            }
            break;
        default:
            strlcpy(buf, "Weather unavailable", sizeof(buf));
            break;
        }
        lv_label_set_text(g_info_status_label, buf);
        place_rotated(g_info_status_label, 240, 700);
    }

    if (g_info_attribution_label) {
        place_rotated(g_info_attribution_label, 240, 750);
    }
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
    place_rotated(g_index_label, 430, 762);

    g_pause_label = lv_label_create(g_screen);
    lv_label_set_text(g_pause_label, LV_SYMBOL_PAUSE " PAUSED");
    lv_obj_set_style_text_color(g_pause_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_pause_label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_pause_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_pause_label, 8, LV_PART_MAIN);
    place_rotated(g_pause_label, 240, 40);
    lv_obj_add_flag(g_pause_label, LV_OBJ_FLAG_HIDDEN);

    g_info_overlay = lv_obj_create(g_screen);
    lv_obj_set_size(g_info_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(g_info_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_info_overlay, lv_color_hex(0x0B1020), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(g_info_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(g_info_overlay, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_info_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_info_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_info_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_info_overlay, LV_OBJ_FLAG_HIDDEN);

    g_info_weekday_label = make_card_label(&lv_font_montserrat_24, accent_color(), "TODAY");
    place_rotated(g_info_weekday_label, 240, 80);

    g_info_date_label = make_card_label(&lv_font_montserrat_24, body_text_color(), "Date unavailable");
    place_rotated(g_info_date_label, 240, 116);

    g_info_clock_label = make_card_label(&lv_font_montserrat_40, body_text_color(), "--:--");
    place_rotated(g_info_clock_label, 240, 220);

    g_info_weather_icon = lv_obj_create(g_info_overlay);
    lv_obj_set_size(g_info_weather_icon, 112, 112);
    lv_obj_set_style_radius(g_info_weather_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_info_weather_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_info_weather_icon, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_info_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_flag(g_info_weather_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_info_weather_icon, weather_icon_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    place_rotated(g_info_weather_icon, 150, 400);

    g_info_temp_label = make_card_label(&lv_font_montserrat_40, body_text_color(), "--C");
    place_rotated(g_info_temp_label, 320, 390);

    g_info_condition_label = make_card_label(&lv_font_montserrat_24, muted_text_color(), "Weather unavailable");
    place_rotated(g_info_condition_label, 240, 490);

    g_info_divider = lv_obj_create(g_info_overlay);
    lv_obj_set_size(g_info_divider, 380, 1);
    lv_obj_set_style_radius(g_info_divider, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_info_divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_info_divider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_info_divider, accent_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_info_divider, LV_OPA_40, LV_PART_MAIN);
    place_rotated(g_info_divider, 240, 560);

    g_info_indoor_label = make_card_label(&lv_font_montserrat_24, body_text_color(), kIndoorOfflineText);
    place_rotated(g_info_indoor_label, 240, 610);

    g_info_status_label = make_card_label(&lv_font_montserrat_14, muted_text_color(), "No WiFi");
    place_rotated(g_info_status_label, 240, 700);

    g_info_attribution_label = make_card_label(&lv_font_montserrat_14, muted_text_color(),
                                               "Weather data by Open-Meteo.com (CC BY 4.0)");
    place_rotated(g_info_attribution_label, 240, 750);

    lv_obj_delete(g_info_weather_icon); g_info_weather_icon = nullptr;
    lv_obj_delete(g_info_divider); g_info_divider = nullptr;
    lv_obj_delete(g_info_indoor_label); g_info_indoor_label = nullptr;
    lv_obj_delete(g_info_status_label); g_info_status_label = nullptr;
    lv_obj_delete(g_info_attribution_label); g_info_attribution_label = nullptr;

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
        } else {
            refresh_clock_if_minute_changed();
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
