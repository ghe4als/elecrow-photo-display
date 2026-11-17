#pragma once

#include <cstddef>
#include <cstdint>

namespace crowpanel {

struct PhotoIndex {
    char* name_arena;   // PSRAM-resident on device, heap on host; contiguous NUL-terminated cstrings
    uint32_t* offsets;  // length `count`; offsets[i] = byte offset of filename i in name_arena
    int count;          // number of stored filenames
    int on_card_count;  // total matching files seen during enumeration (>= count when truncated)
};

// Wraps index by delta across [0,count); returns -1 if count == 0.
int wrap_index(int index, int count, int delta);

// Returns pointer to filename i, or nullptr if i is out of range or the index is empty.
const char* photo_at(const PhotoIndex& idx, int index);

// In-place Fisher-Yates shuffle of offsets[0..count-1] using the supplied rng.
void shuffle_offsets(uint32_t* offsets, int count, uint32_t (*rng)());

// Returns the largest k such that sum(name_lengths[0..k-1]) + k*sizeof(uint32_t) <= psram_free
// and k <= min(on_card_count, ceiling). Exact prefix computation, no average-length assumption.
int compute_load_count(const uint32_t* name_lengths, int on_card_count, size_t psram_free, int ceiling);

}  // namespace crowpanel
