#include "crowpanel/sdcard.h"

#include <SPI.h>
#include <SD.h>
#include <lvgl.h>

extern "C" {
#include <esp_heap_caps.h>
}

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

    static const uint32_t k_sd_frequencies[] = {4000000, 1000000, 400000};
    bool mounted = false;
    for (uint32_t frequency : k_sd_frequencies) {
        Serial.printf("[sd] SD.begin(CS=%d, %u Hz)...\n", SD_CS, (unsigned)frequency);
        if (SD.begin(SD_CS, SPI, frequency)) {
            mounted = true;
            break;
        }
        Serial.println("[sd] mount attempt failed");
        SD.end();
        delay(250);
    }
    if (!mounted) {
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

int load_photo_index(const char* dirname, PhotoIndex& out, size_t reserve_bytes, int ceiling)
{
    out.name_arena = nullptr;
    out.offsets = nullptr;
    out.count = 0;
    out.on_card_count = 0;

    if (!g_sd_ready) {
        return -1;
    }
    File root = SD.open(dirname);
    if (!root || !root.isDirectory()) {
        return -1;
    }

    // Pass 1: record every matching filename's length (strlen + 1) in a PSRAM
    // scratch array that doubles on demand. on_card_count is the TRUE total,
    // never capped by `ceiling`, so ceiling-truncation stays visible to the
    // caller's log.
    size_t cap = 256;
    uint32_t* name_lengths = static_cast<uint32_t*>(
        heap_caps_malloc(cap * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!name_lengths) {
        return 0;
    }
    int total = 0;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const char* name = file.name();
            if (name[0] != '.' && is_photo_file(name)) {
                if ((size_t)total == cap) {
                    uint32_t* grown = static_cast<uint32_t*>(
                        heap_caps_realloc(name_lengths, cap * 2 * sizeof(uint32_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                    if (!grown) {
                        break;  // scratch cannot grow; keep the lengths gathered so far
                    }
                    name_lengths = grown;
                    cap *= 2;
                }
                name_lengths[total++] = static_cast<uint32_t>(strlen(name) + 1);
            }
        }
        file = root.openNextFile();
    }
    out.on_card_count = total;

    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t budget = psram_free > reserve_bytes ? psram_free - reserve_bytes : 0;
    const int load_count = compute_load_count(name_lengths, out.on_card_count, budget, ceiling);
    if (load_count == 0) {
        heap_caps_free(name_lengths);
        return 0;
    }

    File root2 = SD.open(dirname);
    if (!root2 || !root2.isDirectory()) {
        heap_caps_free(name_lengths);
        return 0;
    }

    size_t arena_bytes = 0;
    for (int i = 0; i < load_count; ++i) {
        arena_bytes += name_lengths[i];
    }
    out.name_arena = static_cast<char*>(
        heap_caps_malloc(arena_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    out.offsets = static_cast<uint32_t*>(
        heap_caps_malloc(load_count * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!out.name_arena || !out.offsets) {
        heap_caps_free(out.name_arena);
        heap_caps_free(out.offsets);
        heap_caps_free(name_lengths);
        out.name_arena = nullptr;
        out.offsets = nullptr;
        out.count = 0;
        return 0;
    }

    // Pass 2: copy the first load_count bare filenames into the arena. It was
    // sized from the exact pass-1 lengths, so the room guard only fires if the
    // card changed between passes.
    int stored = 0;
    uint32_t offset = 0;
    File file2 = root2.openNextFile();
    while (file2 && stored < load_count) {
        if (!file2.isDirectory()) {
            const char* name = file2.name();
            if (name[0] != '.' && is_photo_file(name)) {
                const size_t name_len = strlen(name) + 1;
                if ((size_t)offset + name_len > arena_bytes) {
                    break;
                }
                strcpy(out.name_arena + offset, name);
                out.offsets[stored] = offset;
                offset += name_len;
                ++stored;
            }
        }
        file2 = root2.openNextFile();
    }
    heap_caps_free(name_lengths);
    out.count = stored;
    return out.count;
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
