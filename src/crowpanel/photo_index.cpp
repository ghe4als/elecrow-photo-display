#include "crowpanel/photo_index.h"

namespace crowpanel {

int wrap_index(int index, int count, int delta) {
    if (count <= 0) {
        return -1;
    }
    // 64-bit sum: index + delta can overflow int for extreme caller values.
    const int64_t sum = static_cast<int64_t>(index) + static_cast<int64_t>(delta);
    int64_t result = sum % count;
    if (result < 0) {
        result += count;
    }
    return static_cast<int>(result);
}

const char* photo_at(const PhotoIndex& idx, int index) {
    if (index < 0 || index >= idx.count) {
        return nullptr;
    }
    if (idx.name_arena == nullptr || idx.offsets == nullptr) {
        return nullptr;
    }
    return idx.name_arena + idx.offsets[index];
}

void shuffle_offsets(uint32_t* offsets, int count, uint32_t (*rng)()) {
    if (offsets == nullptr || rng == nullptr) {
        return;
    }
    for (int i = count - 1; i > 0; --i) {
        const uint32_t j = rng() % static_cast<uint32_t>(i + 1);
        const uint32_t tmp = offsets[i];
        offsets[i] = offsets[j];
        offsets[j] = tmp;
    }
}

int compute_load_count(const uint32_t* name_lengths, int on_card_count, size_t psram_free, int ceiling) {
    if (name_lengths == nullptr || on_card_count <= 0 || ceiling <= 0) {
        return 0;
    }
    const int limit = on_card_count < ceiling ? on_card_count : ceiling;
    size_t used = 0;
    int k = 0;
    while (k < limit) {
        const size_t cost = static_cast<size_t>(name_lengths[k]) + sizeof(uint32_t);
        if (cost > psram_free - used) {
            break;
        }
        used += cost;
        ++k;
    }
    return k;
}

}  // namespace crowpanel
