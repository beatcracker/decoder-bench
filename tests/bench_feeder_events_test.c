#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_feeder.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ss4s.h"

static const unsigned char h264_fixture[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x2a, 0xac, 0x2b, 0x40, 0x3c, 0x01, 0x13, 0xf2, 0xe0,
    0x2d, 0x41, 0x81, 0x81, 0xa9, 0x40, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x75, 0x30, 0x23, 0xc7, 0x0a,
    0xa8, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80, 0x00, 0x00, 0x00, 0x01, 0x65, 0x80,
};

typedef struct EventPumpState {
    BenchEventPumpResult result;
    int calls;
} EventPumpState;

static int feed_calls = 0;

SS4S_VideoFeedResult __wrap_SS4S_PlayerVideoFeed(SS4S_Player *player, const unsigned char *data, size_t size,
                                                 SS4S_VideoFeedFlags flags) {
    assert(player != NULL);
    assert(data != NULL);
    assert(size > 0);
    assert((flags & SS4S_VIDEO_FEED_DATA_FRAME_START) != 0);
    assert((flags & SS4S_VIDEO_FEED_DATA_FRAME_END) != 0);
    feed_calls++;
    return SS4S_VIDEO_FEED_OK;
}

bool __wrap_SS4S_PlayerGetVideoLatency(SS4S_Player *player, int avg_interval_us, int *latency_us) {
    assert(player != NULL);
    assert(avg_interval_us == 0);
    assert(latency_us != NULL);
    return false;
}

static BenchEventPumpResult pump_event(void *ctx) {
    EventPumpState *state = ctx;
    assert(state != NULL);
    state->calls++;
    return state->result;
}

static void write_fixture(int fd) {
    size_t written = 0;
    while (written < sizeof(h264_fixture)) {
        ssize_t count = write(fd, h264_fixture + written, sizeof(h264_fixture) - written);
        assert(count > 0);
        written += (size_t)count;
    }
}

static BenchSource *open_fixture(const char *path) {
    BenchSourceOptions options = {
        .source_buffer_mib = BENCH_SOURCE_BUFFER_MIN_MIB,
        .target_frames = 1,
    };
    BenchSource *source = NULL;
    assert(bench_source_open(path, &options, NULL, &source) == BENCH_SOURCE_OK);
    assert(source != NULL);
    return source;
}

static int run_with_result(const char *path, BenchEventPumpResult pump_result, BenchStopReason *stop_reason,
                           int *records_written) {
    BenchSource *source = open_fixture(path);
    FrameRecord record = {0};
    BenchSourceError source_error = BENCH_SOURCE_ERROR_NONE;
    volatile sig_atomic_t interrupted = 0;
    EventPumpState pump_state = {
        .result = pump_result,
        .calls = 0,
    };

    int rc = bench_feed_loop((SS4S_Player *)(uintptr_t)1, source, 60, 1, &record, 1, records_written, pump_event,
                             &pump_state, &interrupted, stop_reason, &source_error);
    assert(pump_state.calls == 1);
    assert(source_error == BENCH_SOURCE_ERROR_NONE);
    bench_source_close(source);
    return rc;
}

int main(void) {
    char path[] = "/tmp/decoder-bench-feeder-events-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    write_fixture(fd);
    assert(close(fd) == 0);

    BenchStopReason stop_reason = BENCH_STOP_NONE;
    int records_written = 0;
    feed_calls = 0;
    assert(run_with_result(path, BENCH_EVENT_PUMP_CONTINUE, &stop_reason, &records_written) == 0);
    assert(stop_reason == BENCH_STOP_TARGET_FRAMES);
    assert(records_written == 1);
    assert(feed_calls == 1);

    stop_reason = BENCH_STOP_NONE;
    records_written = -1;
    feed_calls = 0;
    assert(run_with_result(path, BENCH_EVENT_PUMP_STOP, &stop_reason, &records_written) == 0);
    assert(stop_reason == BENCH_STOP_USER);
    assert(records_written == 0);
    assert(feed_calls == 0);

    stop_reason = BENCH_STOP_NONE;
    records_written = -1;
    feed_calls = 0;
    assert(run_with_result(path, BENCH_EVENT_PUMP_ERROR, &stop_reason, &records_written) == -1);
    assert(stop_reason == BENCH_STOP_NONE);
    assert(records_written == 0);
    assert(feed_calls == 0);

    assert(unlink(path) == 0);
    return 0;
}
