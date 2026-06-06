#include <assert.h>
#include <stddef.h>

#include <NDL_directmedia_v2.h>

typedef struct TestEvent {
    int type;
    long long value;
    const char *text;
} TestEvent;

static TestEvent g_events[4];
static size_t g_event_count;
static int g_real_play_calls;

void bench_ndl_detect_test_real_reset(void) {
    g_event_count = 0;
    g_real_play_calls = 0;
}

void bench_ndl_detect_test_real_add_event(int type, long long value, const char *text) {
    assert(g_event_count < sizeof(g_events) / sizeof(g_events[0]));
    g_events[g_event_count++] = (TestEvent){
        .type = type,
        .value = value,
        .text = text,
    };
}

int bench_ndl_detect_test_real_play_calls(void) {
    return g_real_play_calls;
}

int NDL_DirectMediaLoad(NDL_DIRECTMEDIA_DATA_INFO_T *data, NDLMediaLoadCallback callback) {
    (void)data;
    for (size_t i = 0; i < g_event_count; i++) {
        callback(g_events[i].type, g_events[i].value, g_events[i].text);
    }
    return 0;
}

int NDL_DirectVideoPlay(void *buffer, unsigned int size, long long pts) {
    (void)buffer;
    (void)size;
    (void)pts;
    g_real_play_calls++;
    return 0;
}
