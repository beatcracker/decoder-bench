#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_feeder.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ss4s.h"

/* ---------- Time helpers ---------- */

static int64_t clock_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "bench_feeder: clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        _exit(2);
    }
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static void sleep_until_ns(int64_t target_ns) {
#if defined(__linux__) && defined(TIMER_ABSTIME)
    struct timespec ts;
    ts.tv_sec = (time_t)(target_ns / 1000000000LL);
    ts.tv_nsec = (long)(target_ns % 1000000000LL);
    int rc = 0;
    while ((rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL)) == EINTR) {
    }
    (void)rc;
#else
    for (;;) {
        int64_t now_ns = clock_now_ns();
        if (now_ns >= target_ns) {
            return;
        }
        int64_t delta_ns = target_ns - now_ns;
        if (delta_ns > 0) {
            struct timespec ts;
            ts.tv_sec = (time_t)(delta_ns / 1000000000LL);
            ts.tv_nsec = (long)(delta_ns % 1000000000LL);
            while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
            }
        }
    }
#endif
}

/* ---------- Timer probe ---------- */

int bench_timer_probe(int samples, int64_t interval_us, BenchTimerProbe *probe) {
    if (samples <= 0 || interval_us <= 0 || probe == NULL) {
        return -1;
    }
    if (interval_us > INT64_MAX / 1000LL) {
        return -1;
    }
    memset(probe, 0, sizeof(*probe));
    probe->samples = samples;
    probe->interval_us = interval_us;

    int64_t interval_ns = interval_us * 1000LL;
    if ((int64_t)samples > INT64_MAX / interval_ns) {
        return -1;
    }

    int64_t start_ns = clock_now_ns();
    int64_t overshoot_sum = 0;

    for (int i = 1; i <= samples; i++) {
        int64_t offset_ns = (int64_t)i * interval_ns;
        if (start_ns > INT64_MAX - offset_ns) {
            return -1;
        }
        int64_t target_ns = start_ns + offset_ns;
        sleep_until_ns(target_ns);
        int64_t wake_ns = clock_now_ns();
        int64_t overshoot_us = (wake_ns - target_ns) / 1000LL;
        if (overshoot_us < 0) {
            overshoot_us = 0;
        }
        overshoot_sum += overshoot_us;
        if (overshoot_us > probe->max_overshoot_us) {
            probe->max_overshoot_us = overshoot_us;
        }
    }
    probe->avg_overshoot_us = overshoot_sum / samples;
    return 0;
}

const char *bench_stop_reason_str(BenchStopReason reason) {
    switch (reason) {
    case BENCH_STOP_NONE:
        return "none";
    case BENCH_STOP_TARGET_FRAMES:
        return "target-frames";
    case BENCH_STOP_EOF:
        return "eof";
    case BENCH_STOP_CAP:
        return "cap";
    case BENCH_STOP_STORAGE_UNDERFLOW:
        return "storage-underflow";
    case BENCH_STOP_IO_ERROR:
        return "io-error";
    case BENCH_STOP_USER:
        return "user";
    case BENCH_STOP_DECODER_FAIL:
        return "decoder-fail";
    case BENCH_STOP_INVALID_FIXTURE:
        return "invalid-fixture";
    default:
        return "unknown";
    }
}

const char *bench_run_length_mode_str(BenchRunLengthMode mode) {
    switch (mode) {
    case BENCH_RUN_LENGTH_EXPLICIT:
        return "explicit";
    case BENCH_RUN_LENGTH_AUTO:
        return "auto";
    default:
        return "unknown";
    }
}

/* ---------- Paced feed loop ---------- */

static int finish_source_outcome(BenchSourceOutcome outcome, bool auto_mode, BenchStopReason *stop_reason,
                                 BenchSourceError *source_error) {
    switch (outcome) {
    case BENCH_SOURCE_EOF:
        *stop_reason = auto_mode ? BENCH_STOP_EOF : BENCH_STOP_INVALID_FIXTURE;
        if (!auto_mode) {
            *source_error = BENCH_SOURCE_ERROR_INVALID_FIXTURE;
        }
        return 0;
    case BENCH_SOURCE_STORAGE_UNDERFLOW:
        *stop_reason = BENCH_STOP_STORAGE_UNDERFLOW;
        *source_error = BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW;
        return 0;
    case BENCH_SOURCE_INVALID_FIXTURE:
        *stop_reason = BENCH_STOP_INVALID_FIXTURE;
        *source_error = BENCH_SOURCE_ERROR_INVALID_FIXTURE;
        return 0;
    case BENCH_SOURCE_IO_ERROR:
        *stop_reason = BENCH_STOP_IO_ERROR;
        *source_error = BENCH_SOURCE_ERROR_IO;
        return 0;
    case BENCH_SOURCE_STOPPED:
    default:
        *stop_reason = BENCH_STOP_USER;
        return 0;
    }
}

int bench_feed_loop(SS4S_Player *player, BenchSource *source, int fps, int target_frames, FrameRecord *records,
                    int max_records, int *records_written, BenchEventPumpFn pump_events, void *pump_ctx,
                    const volatile sig_atomic_t *interrupted, BenchStopReason *stop_reason,
                    BenchSourceError *source_error) {
    if (stop_reason != NULL) {
        *stop_reason = BENCH_STOP_NONE;
    }
    if (source_error != NULL) {
        *source_error = BENCH_SOURCE_ERROR_NONE;
    }
    if (player == NULL || source == NULL || records == NULL || records_written == NULL || interrupted == NULL ||
        fps <= 0 || max_records < 0 || stop_reason == NULL || source_error == NULL) {
        return -1;
    }
    *records_written = 0;
    bool auto_mode = target_frames <= 0;

    if (*interrupted != 0) {
        *stop_reason = BENCH_STOP_USER;
        return 0;
    }

    BenchSourceOutcome prepare_rc = bench_source_prepare_playback(source);
    if (prepare_rc != BENCH_SOURCE_OK) {
        return finish_source_outcome(prepare_rc, auto_mode, stop_reason, source_error);
    }

    int64_t start_time_ns = clock_now_ns();

    for (int i = 0;; i++) {
        if (!auto_mode && i >= target_frames) {
            *stop_reason = BENCH_STOP_TARGET_FRAMES;
            return 0;
        }
        if (i >= max_records) {
            *stop_reason = auto_mode ? BENCH_STOP_CAP : BENCH_STOP_TARGET_FRAMES;
            return 0;
        }
        if (*interrupted != 0) {
            *stop_reason = BENCH_STOP_USER;
            return 0;
        }
        if (pump_events != NULL && !pump_events(pump_ctx)) {
            *stop_reason = BENCH_STOP_USER;
            return 0;
        }

        int64_t target_ns = start_time_ns + ((int64_t)i * 1000000000LL) / fps;
        sleep_until_ns(target_ns);
        int64_t actual_wake_ns = clock_now_ns();

        AccessUnit au;
        BenchSourceOutcome acquire_rc = bench_source_acquire(source, &au);
        if (acquire_rc != BENCH_SOURCE_OK) {
            return finish_source_outcome(acquire_rc, auto_mode, stop_reason, source_error);
        }
        int64_t source_ready_ns = clock_now_ns();

        SS4S_VideoFeedFlags flags = SS4S_VIDEO_FEED_DATA_FRAME_START | SS4S_VIDEO_FEED_DATA_FRAME_END;
        if (au.is_keyframe) {
            flags |= SS4S_VIDEO_FEED_DATA_KEYFRAME;
        }

        size_t au_size = au.size;
        SS4S_VideoFeedResult feed_result = SS4S_PlayerVideoFeed(player, au.data, au.size, flags);
        int64_t after_ns = clock_now_ns();

        BenchSourceOutcome release_rc = bench_source_release(source);
        if (release_rc != BENCH_SOURCE_OK) {
            return finish_source_outcome(release_rc, auto_mode, stop_reason, source_error);
        }

        int64_t decoder_latency_us = -1;
        int latency_raw = 0;
        /* avgIntervalUs=0 selects SS4S's default 1 s rolling average. On webOS5
         * this is fed through the driver's ReportFrame path, not end-to-end UI latency. */
        if (SS4S_PlayerGetVideoLatency(player, 0, &latency_raw)) {
            decoder_latency_us = latency_raw;
        }

        FrameRecord *rec = &records[i];
        rec->frame_idx = (uint32_t)i;
        rec->pts_us = ((int64_t)i * 1000000LL) / fps;
        rec->au_size = (uint64_t)au_size;
        rec->wake_jitter_us = (actual_wake_ns - target_ns) / 1000LL;
        rec->submit_dur_us = (after_ns - source_ready_ns) / 1000LL;
        rec->decoder_latency_us = decoder_latency_us;
        rec->feed_result = (int32_t)feed_result;
        *records_written = i + 1;

        if (feed_result != SS4S_VIDEO_FEED_OK) {
            *stop_reason = BENCH_STOP_DECODER_FAIL;
            return 0;
        }
    }
}
