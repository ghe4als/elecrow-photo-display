#include <unity.h>
#include "starter.h"

void setUp(void) {}
void tearDown(void) {}

static void test_add_basic(void) {
    TEST_ASSERT_EQUAL_INT32(0, starter::add(0, 0));
    TEST_ASSERT_EQUAL_INT32(5, starter::add(2, 3));
    TEST_ASSERT_EQUAL_INT32(-1, starter::add(2, -3));
}

static void test_add_overflow(void) {
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, starter::add(INT32_MAX, 0));
}

static void test_mapClamp_midpoint(void) {
    // 0..1023 -> 0..255, midpoint 512. Integer math truncates: 512*255/1023 = 127.
    TEST_ASSERT_EQUAL_INT32(127, starter::mapClamp(512, 0, 1023, 0, 255));
    // exact endpoint values must hit the output endpoints exactly:
    TEST_ASSERT_EQUAL_INT32(0,   starter::mapClamp(0,   0, 1023, 0, 255));
    TEST_ASSERT_EQUAL_INT32(255, starter::mapClamp(1023,0, 1023, 0, 255));
}

static void test_mapClamp_clamps_below(void) {
    TEST_ASSERT_EQUAL_INT32(0, starter::mapClamp(-100, 0, 1023, 0, 255));
}

static void test_mapClamp_clamps_above(void) {
    TEST_ASSERT_EQUAL_INT32(255, starter::mapClamp(5000, 0, 1023, 0, 255));
}

static void test_mapClamp_inverted_range(void) {
    // output reversed: 0..1023 -> 255..0
    TEST_ASSERT_EQUAL_INT32(255, starter::mapClamp(0, 0, 1023, 255, 0));
    TEST_ASSERT_EQUAL_INT32(0,   starter::mapClamp(1023, 0, 1023, 255, 0));
}

static void test_mapClamp_degenerate(void) {
    // inMin==inMax must not divide by zero
    TEST_ASSERT_EQUAL_INT32(42, starter::mapClamp(7, 5, 5, 42, 99));
}

// Weather tests live in test_weather_utils.cpp; PlatformIO compiles every
// file under test/ into one program, so this file owns the single Unity main
// and registers both sets of test functions here.
extern void test_weather_clear_sky(void);
extern void test_weather_moderate_rain(void);
extern void test_weather_unknown_code(void);
extern void test_weather_all_known_codes(void);
extern void test_weather_all_codes_non_null(void);
extern void test_weather_unmapped_codes_unknown(void);
extern void test_weather_negative_unknown(void);
extern void test_wrap_index_forward(void);
extern void test_wrap_index_wraps_past_end(void);
extern void test_wrap_index_wraps_before_zero(void);
extern void test_wrap_index_empty_returns_minus_one(void);
extern void test_photo_at_returns_names(void);
extern void test_photo_at_out_of_range_is_null(void);
extern void test_shuffle_offsets_is_permutation(void);
extern void test_compute_load_count_fits_all(void);
extern void test_compute_load_count_clamped_by_ceiling(void);
extern void test_compute_load_count_empty_card(void);
extern void test_compute_load_count_truncates_exactly(void);

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_add_basic);
    RUN_TEST(test_add_overflow);
    RUN_TEST(test_mapClamp_midpoint);
    RUN_TEST(test_mapClamp_clamps_below);
    RUN_TEST(test_mapClamp_clamps_above);
    RUN_TEST(test_mapClamp_inverted_range);
    RUN_TEST(test_mapClamp_degenerate);
    RUN_TEST(test_weather_clear_sky);
    RUN_TEST(test_weather_moderate_rain);
    RUN_TEST(test_weather_unknown_code);
    RUN_TEST(test_weather_all_known_codes);
    RUN_TEST(test_weather_all_codes_non_null);
    RUN_TEST(test_weather_unmapped_codes_unknown);
    RUN_TEST(test_weather_negative_unknown);
    RUN_TEST(test_wrap_index_forward);
    RUN_TEST(test_wrap_index_wraps_past_end);
    RUN_TEST(test_wrap_index_wraps_before_zero);
    RUN_TEST(test_wrap_index_empty_returns_minus_one);
    RUN_TEST(test_photo_at_returns_names);
    RUN_TEST(test_photo_at_out_of_range_is_null);
    RUN_TEST(test_shuffle_offsets_is_permutation);
    RUN_TEST(test_compute_load_count_fits_all);
    RUN_TEST(test_compute_load_count_clamped_by_ceiling);
    RUN_TEST(test_compute_load_count_empty_card);
    RUN_TEST(test_compute_load_count_truncates_exactly);
    return UNITY_END();
}