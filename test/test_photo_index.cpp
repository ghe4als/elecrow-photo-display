// PhotoIndex helper tests. Like test_weather_utils.cpp this file only defines
// test functions; the single Unity main (and setUp/tearDown) lives in
// test_starter.cpp because PlatformIO links every file under test/ into one
// program.
#include <unity.h>

#include "crowpanel/photo_index.h"

// Deterministic xorshift32 so the shuffle test never depends on timing or
// platform RNG state.
static uint32_t g_rng_state = 0x12345678u;

static uint32_t seeded_rng() {
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 17;
    g_rng_state ^= g_rng_state << 5;
    return g_rng_state;
}

void test_wrap_index_forward(void) {
    TEST_ASSERT_EQUAL_INT(1, crowpanel::wrap_index(0, 5, 1));
}

void test_wrap_index_wraps_past_end(void) {
    TEST_ASSERT_EQUAL_INT(0, crowpanel::wrap_index(4, 5, 1));
}

void test_wrap_index_wraps_before_zero(void) {
    TEST_ASSERT_EQUAL_INT(4, crowpanel::wrap_index(0, 5, -1));
}

void test_wrap_index_empty_returns_minus_one(void) {
    TEST_ASSERT_EQUAL_INT(-1, crowpanel::wrap_index(7, 0, 1));
}

void test_photo_at_returns_names(void) {
    static char arena[] = "alpha.jpg\0bb.jpg\0c.jpg";
    static uint32_t offsets[] = {0, 10, 17};  // byte offsets of each NUL-terminated name
    crowpanel::PhotoIndex idx = {arena, offsets, 3, 3};

    TEST_ASSERT_EQUAL_STRING("alpha.jpg", crowpanel::photo_at(idx, 0));
    TEST_ASSERT_EQUAL_STRING("bb.jpg", crowpanel::photo_at(idx, 1));
    TEST_ASSERT_EQUAL_STRING("c.jpg", crowpanel::photo_at(idx, 2));
}

void test_photo_at_out_of_range_is_null(void) {
    static char arena[] = "alpha.jpg\0bb.jpg\0c.jpg";
    static uint32_t offsets[] = {0, 10, 17};
    crowpanel::PhotoIndex idx = {arena, offsets, 3, 3};

    TEST_ASSERT_NULL(crowpanel::photo_at(idx, 3));
    TEST_ASSERT_NULL(crowpanel::photo_at(idx, -1));
}

void test_shuffle_offsets_is_permutation(void) {
    uint32_t offsets[10];
    for (int i = 0; i < 10; ++i) {
        offsets[i] = static_cast<uint32_t>(i);
    }

    g_rng_state = 0x12345678u;
    crowpanel::shuffle_offsets(offsets, 10, seeded_rng);

    // Ascending insertion sort, then every value 0..9 must appear exactly once.
    for (int i = 1; i < 10; ++i) {
        const uint32_t key = offsets[i];
        int j = i - 1;
        while (j >= 0 && offsets[j] > key) {
            offsets[j + 1] = offsets[j];
            --j;
        }
        offsets[j + 1] = key;
    }
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(i), offsets[i]);
    }
}

// 5000 alternating 10/20-byte names: sum = 75000, matching the plan's scenario.
static uint32_t g_name_lengths[5000];

static void fill_alternating_lengths(void) {
    for (int i = 0; i < 5000; ++i) {
        g_name_lengths[i] = (i % 2 == 0) ? 10u : 20u;
    }
}

void test_compute_load_count_fits_all(void) {
    fill_alternating_lengths();
    // cost(k) = 75000 + 4*5000 = 95000 <= 1000000, and k <= min(5000, 20000).
    TEST_ASSERT_EQUAL_INT(5000, crowpanel::compute_load_count(g_name_lengths, 5000, 1000000, 20000));
}

void test_compute_load_count_clamped_by_ceiling(void) {
    // Needs at least `on_card_count` entries: the prefix walk reads up to
    // min(on_card_count, ceiling) name lengths.
    static uint32_t many_lengths[30000];
    for (int i = 0; i < 30000; ++i) {
        many_lengths[i] = (i % 2 == 0) ? 10u : 20u;
    }
    TEST_ASSERT_EQUAL_INT(20000,
                          crowpanel::compute_load_count(many_lengths, 30000, 64u * 1024u * 1024u, 20000));
}

void test_compute_load_count_empty_card(void) {
    fill_alternating_lengths();
    TEST_ASSERT_EQUAL_INT(0, crowpanel::compute_load_count(g_name_lengths, 0, 1000000, 20000));
}

void test_compute_load_count_truncates_exactly(void) {
    fill_alternating_lengths();
    // cost(k) = 15k (k even) or 15k-5 (k odd), plus 4k:
    //   cost(51) = 964, cost(52) = 988, cost(53) = 1002.
    TEST_ASSERT_EQUAL_INT(52, crowpanel::compute_load_count(g_name_lengths, 5000, 988, 20000));
    TEST_ASSERT_EQUAL_INT(51, crowpanel::compute_load_count(g_name_lengths, 5000, 987, 20000));
    TEST_ASSERT_EQUAL_INT(53, crowpanel::compute_load_count(g_name_lengths, 5000, 1002, 20000));
}
