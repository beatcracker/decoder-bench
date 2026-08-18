#include "bench_stats.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ss4s/video.h"

static BenchSummaryInputs explicit_inputs(int target_frames) {
    BenchSummaryInputs inputs = {
        .run_length_mode = BENCH_RUN_LENGTH_EXPLICIT,
        .target_frames = target_frames,
        .duration_sec = 3,
        .stop_reason = BENCH_STOP_TARGET_FRAMES,
        .source_error = BENCH_SOURCE_ERROR_NONE,
        .source_mode = BENCH_SOURCE_MODE_COMPLETE,
        .source_buffer_mib = BENCH_SOURCE_BUFFER_DEFAULT_MIB,
    };
    return inputs;
}

static FrameRecord make_record(int64_t submit_dur_us, int64_t decoder_latency_us, int32_t feed_result) {
    FrameRecord record = {
        .frame_idx = 0,
        .pts_us = 0,
        .au_size = 1024,
        .wake_jitter_us = 0,
        .submit_dur_us = submit_dur_us,
        .decoder_latency_us = decoder_latency_us,
        .feed_result = feed_result,
    };
    return record;
}

static void test_clean_run_passes(void) {
    FrameRecord records[] = {
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK),
        make_record(2100, 1100, SS4S_VIDEO_FEED_OK),
        make_record(2200, 1200, SS4S_VIDEO_FEED_OK),
    };
    BenchSummary summary;
    BenchSummaryInputs inputs = explicit_inputs((int)(sizeof(records) / sizeof(records[0])));

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &summary);

    assert(summary.verdict == BENCH_VERDICT_PASS);
    assert(summary.late_submits == 0);
    assert(summary.latency_probe_stall_max_frames == 0);
}

static void test_single_late_submit_warns(void) {
    FrameRecord records[] = {
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK),
        make_record(20000, 1100, SS4S_VIDEO_FEED_OK),
        make_record(2200, 1200, SS4S_VIDEO_FEED_OK),
    };
    BenchSummary summary;
    BenchSummaryInputs inputs = explicit_inputs((int)(sizeof(records) / sizeof(records[0])));

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &summary);

    assert(summary.verdict == BENCH_VERDICT_WARN);
    assert(summary.late_submits == 1);
    assert(summary.max_late_us > 0);
}

static void test_short_explicit_run_fails(void) {
    FrameRecord records[] = {
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK),
        make_record(2100, 1100, SS4S_VIDEO_FEED_OK),
    };
    BenchSummary summary;
    BenchSummaryInputs inputs = explicit_inputs(3);
    inputs.stop_reason = BENCH_STOP_NONE;

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &summary);

    assert(summary.verdict == BENCH_VERDICT_FAIL);
}

static void test_severe_growth_fails(void) {
    FrameRecord records[] = {
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK), make_record(2000, 1000, SS4S_VIDEO_FEED_OK),
        make_record(2000, 1500, SS4S_VIDEO_FEED_OK), make_record(2000, 1500, SS4S_VIDEO_FEED_OK),
        make_record(2000, 1500, SS4S_VIDEO_FEED_OK), make_record(2000, 1500, SS4S_VIDEO_FEED_OK),
        make_record(2000, 4000, SS4S_VIDEO_FEED_OK), make_record(2000, 4000, SS4S_VIDEO_FEED_OK),
    };
    BenchSummary summary;
    BenchSummaryInputs inputs = explicit_inputs((int)(sizeof(records) / sizeof(records[0])));

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &summary);

    assert(summary.latency_growth_detected);
    assert(summary.verdict == BENCH_VERDICT_FAIL);
}

static void test_latency_probe_stall_tracks_longest_run(void) {
    FrameRecord records[] = {
        make_record(2000, -1, SS4S_VIDEO_FEED_OK),   make_record(2000, -1, SS4S_VIDEO_FEED_OK),
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK), make_record(2000, -1, SS4S_VIDEO_FEED_OK),
        make_record(2000, -1, SS4S_VIDEO_FEED_OK),   make_record(2000, -1, SS4S_VIDEO_FEED_OK),
        make_record(2000, 1100, SS4S_VIDEO_FEED_OK),
    };
    BenchSummary summary;
    BenchSummaryInputs inputs = explicit_inputs((int)(sizeof(records) / sizeof(records[0])));

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &summary);

    assert(summary.latency_probe_stall_max_frames == 3);
    assert(summary.verdict == BENCH_VERDICT_PASS);
}

static void read_text_file(const char *path, char *buf, size_t buf_len) {
    assert(path != NULL);
    assert(buf != NULL);
    assert(buf_len > 0);

    FILE *f = fopen(path, "rb");
    assert(f != NULL);

    size_t len = fread(buf, 1, buf_len - 1, f);
    assert(ferror(f) == 0);
    buf[len] = '\0';
    assert(fclose(f) == 0);
}

static void test_summary_csv_includes_verdict_reason(void) {
    FrameRecord records[] = {
        make_record(2000, 1000, SS4S_VIDEO_FEED_OK),
        make_record(20000, 1100, SS4S_VIDEO_FEED_OK),
        make_record(2200, 1200, SS4S_VIDEO_FEED_OK),
    };
    BenchSummaryInputs inputs = explicit_inputs((int)(sizeof(records) / sizeof(records[0])));
    BenchSummaryRow row = {0};
    char path[128];
    char text[2048];

    bench_stats_compute(records, (int)(sizeof(records) / sizeof(records[0])), 60, &inputs, false, &row.summary);
    (void)snprintf(row.test_name, sizeof(row.test_name), "%s", "late-test");
    (void)snprintf(row.fixture, sizeof(row.fixture), "%s", "late-test.h264");
    row.info = (StreamInfo){
        .codec = SS4S_VIDEO_H264,
        .width = 1920,
        .height = 1080,
        .fps = 60,
        .run_seconds = 3,
    };

    int written = snprintf(path, sizeof(path), "/tmp/bench_stats_summary_%ld.csv", (long)getpid());
    assert(written > 0);
    assert((size_t)written < sizeof(path));
    (void)remove(path);

    assert(bench_stats_write_summary_csv(path, "scope", &row, 1) == 0);
    read_text_file(path, text, sizeof(text));
    assert(strstr(text, "latency_growth_detected,verdict_reason,verdict_detail,test_outcome") != NULL);
    assert(strstr(text, "\"WARN\"") != NULL);
    assert(strstr(text, "\"late-submit\",\"late=1/3 max_late=3333us\"") != NULL);
    assert(strstr(text, "\"completed\"") != NULL);
    assert(remove(path) == 0);
}

static void test_unsupported_summary_is_neutral(void) {
    BenchSummaryInputs inputs = explicit_inputs(180);
    BenchSummaryRow row = {0};
    char path[128];
    char text[2048];

    inputs.stop_reason = BENCH_STOP_NONE;
    bench_stats_init_empty(&inputs, &row.summary);
    row.test_outcome = BENCH_TEST_UNSUPPORTED;
    (void)snprintf(row.test_name, sizeof(row.test_name), "%s", "hevc-test");
    (void)snprintf(row.fixture, sizeof(row.fixture), "%s", "hevc-test.hevc");
    row.info = (StreamInfo){
        .codec = SS4S_VIDEO_H265,
        .width = 3840,
        .height = 2160,
        .fps = 60,
        .run_seconds = 3,
    };

    assert(row.summary.frames_submitted == 0);
    assert(row.summary.feed_errors == 0);
    assert(row.summary.late_submits == 0);
    assert(row.summary.target_frames == 180);

    int written = snprintf(path, sizeof(path), "/tmp/bench_stats_unsupported_%ld.csv", (long)getpid());
    assert(written > 0);
    assert((size_t)written < sizeof(path));
    (void)remove(path);

    assert(bench_stats_write_summary_csv(path, "scope", &row, 1) == 0);
    read_text_file(path, text, sizeof(text));
    assert(strstr(text, "\"HEVC\",3840,2160,60,3,\"\",0,180") != NULL);
    assert(strstr(text, "\"unsupported-codec\",\"\",\"unsupported\"") != NULL);
    assert(remove(path) == 0);
}

static void test_unsupported_aggregation_is_neutral(void) {
    BenchVerdict worst = BENCH_VERDICT_PASS;
    int completed = 0;
    int unsupported = 0;

    bench_stats_aggregate_outcome(BENCH_TEST_UNSUPPORTED, BENCH_VERDICT_FAIL, &worst, &completed, &unsupported);
    bench_stats_aggregate_outcome(BENCH_TEST_UNSUPPORTED, BENCH_VERDICT_WARN, &worst, &completed, &unsupported);

    assert(worst == BENCH_VERDICT_PASS);
    assert(completed == 0);
    assert(unsupported == 2);

    bench_stats_aggregate_outcome(BENCH_TEST_COMPLETED, BENCH_VERDICT_WARN, &worst, &completed, &unsupported);
    bench_stats_aggregate_outcome(BENCH_TEST_UNSUPPORTED, BENCH_VERDICT_FAIL, &worst, &completed, &unsupported);

    assert(worst == BENCH_VERDICT_WARN);
    assert(completed == 1);
    assert(unsupported == 3);
}

int main(void) {
    test_clean_run_passes();
    test_single_late_submit_warns();
    test_short_explicit_run_fails();
    test_severe_growth_fails();
    test_latency_probe_stall_tracks_longest_run();
    test_summary_csv_includes_verdict_reason();
    test_unsupported_summary_is_neutral();
    test_unsupported_aggregation_is_neutral();
    return 0;
}
