#include "crowpanel/sdcard.h"

#include <SPI.h>
#include <SD.h>
#include <lvgl.h>

namespace crowpanel {

namespace {

bool g_sd_ready = false;

bool sd_ready_cb(struct _lv_fs_drv_t*)
{
    return g_sd_ready;
}

void* sd_open_cb(struct _lv_fs_drv_t*, const char* path, lv_fs_mode_t mode)
{
    const char* arduino_mode = (mode & LV_FS_MODE_WR) ? "w" : "r";
    File f = SD.open(path, arduino_mode);
    if (!f) {
        return nullptr;
    }
    File* fp = new File(f);
    if (!fp || !*fp) {
        delete fp;
        return nullptr;
    }
    return fp;
}

lv_fs_res_t sd_close_cb(struct _lv_fs_drv_t*, void* file_p)
{
    File* f = static_cast<File*>(file_p);
    if (f) {
        f->close();
        delete f;
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_read_cb(struct _lv_fs_drv_t*, void* file_p, void* buf,
                       uint32_t btr, uint32_t* br)
{
    File* f = static_cast<File*>(file_p);
    if (!f) {
        return LV_FS_RES_INV_PARAM;
    }
    *br = f->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_seek_cb(struct _lv_fs_drv_t*, void* file_p, uint32_t pos,
                       lv_fs_whence_t whence)
{
    File* f = static_cast<File*>(file_p);
    if (!f) {
        return LV_FS_RES_INV_PARAM;
    }
    SeekMode mode = SeekSet;
    switch (whence) {
        case LV_FS_SEEK_CUR: mode = SeekCur; break;
        case LV_FS_SEEK_END: mode = SeekEnd; break;
        case LV_FS_SEEK_SET: mode = SeekSet; break;
    }
    if (!f->seek(pos, mode)) {
        return LV_FS_RES_FS_ERR;
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_tell_cb(struct _lv_fs_drv_t*, void* file_p, uint32_t* pos_p)
{
    File* f = static_cast<File*>(file_p);
    if (!f || !pos_p) {
        return LV_FS_RES_INV_PARAM;
    }
    *pos_p = f->position();
    return LV_FS_RES_OK;
}

bool path_ends_with(const char* path, const char* ext)
{
    const size_t path_len = strlen(path);
    const size_t ext_len  = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }
    return strcasecmp(path + path_len - ext_len, ext) == 0;
}

bool is_photo_file(const char* path)
{
    return path_ends_with(path, ".jpg") ||
           path_ends_with(path, ".jpeg") ||
           path_ends_with(path, ".png") ||
           path_ends_with(path, ".bmp");
}

} // namespace

bool init_sd_card()
{
    if (g_sd_ready) {
        return true;
    }
    Serial.println("[sd] initializing SPI...");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    delay(100);

    Serial.printf("[sd] SD.begin(CS=%d)...\n", SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("[sd] SD.begin() failed");
        return false;
    }

    const uint8_t card_type = SD.cardType();
    Serial.printf("[sd] cardType=%d\n", card_type);
    if (card_type == CARD_NONE) {
        Serial.println("[sd] no card detected");
        return false;
    }

    Serial.println("[sd] listing root directory:");
    File root = SD.open("/");
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            Serial.printf("[sd]  %s %s\n", f.isDirectory() ? "DIR " : "FILE", f.name());
            f = root.openNextFile();
        }
    } else {
        Serial.println("[sd]  could not open root");
    }

    g_sd_ready = true;
    Serial.printf("[sd] mounted, type=%d, size=%lluMB\n",
                  card_type, SD.cardSize() / (1024 * 1024));
    return true;
}

bool sd_ready()
{
    return g_sd_ready;
}

fs::FS& get_sd_fs()
{
    return SD;
}

int list_photo_files(const char* dirname, String* out_paths, int max_count)
{
    if (!g_sd_ready) {
        return -1;
    }
    File root = SD.open(dirname);
    if (!root || !root.isDirectory()) {
        return -1;
    }
    int count = 0;
    File file = root.openNextFile();
    while (file && count < max_count) {
        if (!file.isDirectory()) {
            const char* name = file.name();
            if (name[0] != '.' && is_photo_file(name)) {
                out_paths[count] = String("/") + name;
                ++count;
            }
        }
        file = root.openNextFile();
    }
    return count;
}

void register_lvgl_sd_fs()
{
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.cache_size = 4096;
    drv.ready_cb   = sd_ready_cb;
    drv.open_cb    = sd_open_cb;
    drv.close_cb   = sd_close_cb;
    drv.read_cb    = sd_read_cb;
    drv.seek_cb    = sd_seek_cb;
    drv.tell_cb    = sd_tell_cb;
    lv_fs_drv_register(&drv);
    Serial.println("[sd] registered LVGL 'S:' fs driver");
}

} // namespace crowpanel
