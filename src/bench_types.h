#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ss4s/video.h"

/**
 * Maximum number of tests in a single suite.
 */
#define BENCH_MAX_TESTS 64

/**
 * Maximum length of a test name (section name in .bench file).
 */
#define BENCH_MAX_NAME_LEN 128

/**
 * Maximum path length for file references.
 */
#ifdef PATH_MAX
#define BENCH_MAX_PATH_LEN PATH_MAX
#else
#define BENCH_MAX_PATH_LEN 4096
#endif

#define BENCH_SOURCE_BUFFER_MIN_MIB 32u
#define BENCH_SOURCE_BUFFER_MAX_MIB 512u
#define BENCH_SOURCE_BUFFER_DEFAULT_MIB 64u
#define BENCH_MAX_SINGLE_AU_BYTES (16u * 1024u * 1024u)
#define BENCH_AUTO_CAP_SECONDS 30
#define BENCH_MAX_FPS 300
#define BENCH_MAX_RUN_SECONDS 3600
#define BENCH_WARMUP_BYTES_DEFAULT (4u * 1024u * 1024u)
#define BENCH_WARMUP_MAX_MS_DEFAULT 5000
#define BENCH_STORAGE_CHUNK (1u * 1024u * 1024u)

typedef enum BenchVerdict {
    BENCH_VERDICT_PASS = 0,
    BENCH_VERDICT_WARN = 1,
    BENCH_VERDICT_FAIL = 2,
} BenchVerdict;

typedef enum BenchRunLengthMode {
    BENCH_RUN_LENGTH_EXPLICIT = 0,
    BENCH_RUN_LENGTH_AUTO = 1,
} BenchRunLengthMode;

typedef enum BenchSourceMode {
    BENCH_SOURCE_MODE_NONE = 0,
    /* Entire fixture was parsed from the initial source buffer. */
    BENCH_SOURCE_MODE_COMPLETE = 1,
    /* Source uses the loader-backed double-buffer path after the initial fill. */
    BENCH_SOURCE_MODE_STREAMING = 2,
} BenchSourceMode;

typedef enum BenchStopReason {
    BENCH_STOP_NONE = 0,
    BENCH_STOP_TARGET_FRAMES,
    BENCH_STOP_EOF,
    BENCH_STOP_CAP,
    BENCH_STOP_STORAGE_UNDERFLOW,
    BENCH_STOP_IO_ERROR,
    BENCH_STOP_USER,
    BENCH_STOP_DECODER_FAIL,
    BENCH_STOP_INVALID_FIXTURE,
} BenchStopReason;

typedef enum BenchSourceError {
    BENCH_SOURCE_ERROR_NONE = 0,
    BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW,
    BENCH_SOURCE_ERROR_INVALID_FIXTURE,
    BENCH_SOURCE_ERROR_IO,
} BenchSourceError;

typedef enum BenchSourceOutcome {
    BENCH_SOURCE_OK = 0,
    BENCH_SOURCE_EOF,
    BENCH_SOURCE_STORAGE_UNDERFLOW,
    BENCH_SOURCE_INVALID_FIXTURE,
    BENCH_SOURCE_IO_ERROR,
    BENCH_SOURCE_STOPPED,
} BenchSourceOutcome;

/**
 * One test definition parsed from a .bench INI file.
 */
typedef struct BenchTest {
    char name[BENCH_MAX_NAME_LEN];
    char file[BENCH_MAX_PATH_LEN];
    int fps;
    int run_seconds;
    bool skip_stats;
} BenchTest;

/**
 * A suite of tests loaded from one .bench file.
 */
typedef struct BenchSuite {
    char name[BENCH_MAX_NAME_LEN];
    char path[BENCH_MAX_PATH_LEN];
    char samples_dir[BENCH_MAX_PATH_LEN];
    BenchTest tests[BENCH_MAX_TESTS];
    int test_count;
    unsigned int source_buffer_mib;
} BenchSuite;

/**
 * Per-frame record written during the measured loop.
 * No allocation, no I/O, no logging during the loop — only struct writes.
 */
typedef struct FrameRecord {
    uint32_t frame_idx;
    int64_t pts_us;
    uint64_t au_size;
    int64_t wake_jitter_us;
    int64_t submit_dur_us;
    int64_t decoder_latency_us;
    int32_t feed_result;
} FrameRecord;

/**
 * Summary computed after the measured loop completes.
 */
typedef struct BenchSummary {
    uint32_t frames_submitted;
    uint32_t target_frames;
    uint32_t feed_errors;
    /**
     * Submit calls whose measured SS4S_PlayerVideoFeed duration exceeded the
     * frame budget. This is backend blocking/backpressure, not OS wake jitter.
     */
    uint32_t late_submits;
    uint32_t duration_sec;
    int64_t max_wake_jitter_us;
    int64_t avg_wake_jitter_us;
    int64_t max_submit_us;
    int64_t avg_submit_us;
    int64_t max_late_us;
    int64_t avg_decoder_latency_us;
    int64_t max_decoder_latency_us;
    uint32_t latency_probe_stall_max_frames;
    bool decoder_latency_available;
    bool latency_growth_detected;
    BenchVerdict verdict;
    BenchRunLengthMode run_length_mode;
    BenchStopReason stop_reason;
    BenchSourceMode source_mode;
    unsigned int source_buffer_mib;
    BenchSourceError source_error;
} BenchSummary;

/**
 * A single access unit (one complete frame) parsed from an Annex B stream.
 *
 * Payload pointers are borrowed from a source buffer. Callers must release a
 * successfully acquired AU before the source can hand out another AU or close.
 * Bytes remain valid until release returns.
 */
typedef struct AccessUnit {
    const unsigned char *data;
    size_t size;
    bool is_keyframe;
} AccessUnit;

/**
 * Stream metadata detected from the elementary stream + .bench config.
 */
typedef struct StreamInfo {
    SS4S_VideoCodec codec;
    int width;
    int height;
    int fps;
    int run_seconds;
} StreamInfo;

/**
 * One operator-facing summary row emitted after a completed test.
 */
typedef struct BenchSummaryRow {
    char test_name[BENCH_MAX_NAME_LEN];
    char fixture[BENCH_MAX_NAME_LEN];
    StreamInfo info;
    BenchSummary summary;
} BenchSummaryRow;

/**
 * Startup timer probe metadata. This is diagnostic only and is never folded
 * into PASS/WARN/FAIL verdict logic.
 */
typedef struct BenchTimerProbe {
    int samples;
    int64_t interval_us;
    int64_t avg_overshoot_us;
    int64_t max_overshoot_us;
} BenchTimerProbe;

/**
 * Options governing how a BenchSource is opened and operated.
 *
 * source_buffer_mib clamps to [BENCH_SOURCE_BUFFER_MIN_MIB,
 * BENCH_SOURCE_BUFFER_MAX_MIB]. Zero selects the default.
 * target_frames > 0 makes the source treat EOF before the requested frame
 * count as invalid-fixture; 0 enables auto mode (EOF is normal).
 */
typedef struct BenchSourceOptions {
    unsigned int source_buffer_mib;
    int target_frames;
} BenchSourceOptions;
