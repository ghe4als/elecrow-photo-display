#include "crowpanel/photo_viewer.h"

#include <lvgl.h>
#include "crowpanel/sdcard.h"

namespace crowpanel {

namespace {

constexpr uint16_t SCREEN_W = 800;
constexpr uint16_t SCREEN_H = 480;
constexpr int SCALE_NONE = 256;

String g_photos[MAX_PHOTOS];
int g_photo_count = 0;
int g_current_index = 0;
bool g_paused = false;
uint32_t g_last_advance_ms = 0;

lv_obj_t* g_image = nullptr;
lv_obj_t* g_index_label = nullptr;

void load_photo(int index)
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
        Serial.printf("[photo] scale %d\n", scale);
    } else {
        lv_image_set_scale(g_image, SCALE_NONE);
    }

    if (g_index_label) {
        lv_label_set_text_fmt(g_index_label, "%d / %d", index + 1, g_photo_count);
    }
}

void advance_photo(int delta)
{
    if (g_photo_count == 0) {
        return;
    }
    int next = g_current_index + delta;
    if (next >= g_photo_count) {
        next = 0;
    } else if (next < 0) {
        next = g_photo_count - 1;
    }
    load_photo(next);
}

} // namespace

bool init_photo_viewer()
{
    g_photo_count = list_photo_files("/photos", g_photos, MAX_PHOTOS);
    Serial.printf("[photo] found %d photo(s) in /photos\n", g_photo_count);

    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_size(screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_screen_load(screen);

    if (g_photo_count <= 0) {
        lv_obj_t* label = lv_label_create(screen);
        lv_label_set_text(label, "No photos in /photos");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(label);
        return false;
    }

    g_image = lv_image_create(screen);
    lv_obj_set_size(g_image, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(g_image, 0, 0);
    lv_image_set_inner_align(g_image, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_antialias(g_image, true);

    g_index_label = lv_label_create(screen);
    lv_obj_set_style_text_color(g_index_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_index_label, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_index_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_index_label, 4, LV_PART_MAIN);
    lv_obj_align(g_index_label, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    g_current_index = -1;
    g_last_advance_ms = millis();
    load_photo(0);
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

void photo_viewer_tick()
{
    if (g_paused || g_photo_count == 0) {
        return;
    }
    const uint32_t now = millis();
    if (now - g_last_advance_ms >= PHOTO_INTERVAL_MS) {
        g_last_advance_ms = now;
        advance_photo(1);
    }
}

} // namespace crowpanel
