#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include <NDL_directmedia_v2.h>

#define TEST_EVENT_LOAD_COMPLETED 0x16
#define TEST_EVENT_PLAYING 0x1a
#define TEST_EVENT_INT_ERROR 0x12
#define TEST_EVENT_STR_ERROR 0x13

void bench_ndl_detect_test_real_reset(void);
void bench_ndl_detect_test_real_add_event(int type, long long value, const char *text);
int bench_ndl_detect_test_real_play_calls(void);

static int g_original_callback_calls;
static int g_last_original_callback_type;
static long long g_last_original_callback_value;

static void reset_state(void) {
    bench_ndl_detect_test_real_reset();
    g_original_callback_calls = 0;
    g_last_original_callback_type = 0;
    g_last_original_callback_value = 0;
}

static void add_event(int type, long long value, const char *text) {
    bench_ndl_detect_test_real_add_event(type, value, text);
}

static void original_callback(int type, long long num_value, const char *str_value) {
    (void)str_value;
    g_original_callback_calls++;
    g_last_original_callback_type = type;
    g_last_original_callback_value = num_value;
}

static void int_error_blocks_video_play(void) {
    NDL_DIRECTMEDIA_DATA_INFO_T info = {0};

    reset_state();
    add_event(TEST_EVENT_INT_ERROR, 0x259, NULL);

    assert(NDL_DirectMediaLoad(&info, original_callback) == 0);
    assert(g_original_callback_calls == 1);
    assert(g_last_original_callback_type == TEST_EVENT_INT_ERROR);
    assert(g_last_original_callback_value == 0x259);
    assert(NDL_DirectVideoPlay(NULL, 0, 0) == -1);
    assert(bench_ndl_detect_test_real_play_calls() == 0);
}

static void str_error_blocks_video_play(void) {
    NDL_DIRECTMEDIA_DATA_INFO_T info = {0};

    reset_state();
    add_event(TEST_EVENT_STR_ERROR, 0, "load failed");

    assert(NDL_DirectMediaLoad(&info, original_callback) == 0);
    assert(g_original_callback_calls == 1);
    assert(g_last_original_callback_type == TEST_EVENT_STR_ERROR);
    assert(NDL_DirectVideoPlay(NULL, 0, 0) == -1);
    assert(bench_ndl_detect_test_real_play_calls() == 0);
}

static void success_events_pass_video_play_through(void) {
    NDL_DIRECTMEDIA_DATA_INFO_T info = {0};

    reset_state();
    add_event(TEST_EVENT_LOAD_COMPLETED, 0, "true");
    add_event(TEST_EVENT_PLAYING, 0, "true");

    assert(NDL_DirectMediaLoad(&info, original_callback) == 0);
    assert(g_original_callback_calls == 2);
    assert(g_last_original_callback_type == TEST_EVENT_PLAYING);
    assert(NDL_DirectVideoPlay(NULL, 0, 0) == 0);
    assert(bench_ndl_detect_test_real_play_calls() == 1);
}

static void new_load_resets_previous_failure(void) {
    NDL_DIRECTMEDIA_DATA_INFO_T info = {0};

    reset_state();
    add_event(TEST_EVENT_INT_ERROR, 0x259, NULL);
    assert(NDL_DirectMediaLoad(&info, original_callback) == 0);
    assert(NDL_DirectVideoPlay(NULL, 0, 0) == -1);

    reset_state();
    add_event(TEST_EVENT_LOAD_COMPLETED, 0, "true");
    assert(NDL_DirectMediaLoad(&info, original_callback) == 0);
    assert(NDL_DirectVideoPlay(NULL, 0, 0) == 0);
    assert(bench_ndl_detect_test_real_play_calls() == 1);
}

int main(void) {
    int_error_blocks_video_play();
    str_error_blocks_video_play();
    success_events_pass_video_play_through();
    new_load_resets_previous_failure();
    return 0;
}
