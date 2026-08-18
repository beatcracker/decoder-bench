#include "bench_stats.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "ss4s/video.h"

#include "bench_feeder.h"

/* ---------- Verdict computation ---------- */

#define BENCH_VERDICT_REASON_LEN 64
#define BENCH_VERDICT_DETAIL_LEN 160

static bool detect_latency_growth(const FrameRecord *records, int count, bool *is_severe) {
    if (is_severe == NULL) {
        return false;
    }
    *is_severe = false;
    if (count < 8) {
        return false;
    }

    int q = count / 4;
    int64_t early_sum = 0;
    int64_t late_sum = 0;
    int early_count = 0;
    int late_count = 0;

    for (int i = 0; i < q; i++) {
        if (records[i].decoder_latency_us > 0) {
            early_sum += records[i].decoder_latency_us;
            early_count++;
        }
    }
    for (int i = count - q; i < count; i++) {
        if (records[i].decoder_latency_us > 0) {
            late_sum += records[i].decoder_latency_us;
            late_count++;
        }
    }

    if (early_count == 0 || late_count == 0) {
        return false;
    }

    int64_t early_avg = early_sum / early_count;
    int64_t late_avg = late_sum / late_count;

    if (early_avg > 0 && late_avg > early_avg + 2000 && late_avg > early_avg * 3 / 2) {
        if (late_avg > early_avg * 3) {
            *is_severe = true;
        }
        return true;
    }
    return false;
}

static bool submit_exceeded_frame_budget(int64_t submit_us, int fps, int64_t *late_us) {
    if (submit_us <= 0 || fps <= 0) {
        *late_us = 0;
        return false;
    }
    int64_t frame_budget_us = (1000000LL + fps - 1) / fps;
    if (submit_us > frame_budget_us) {
        *late_us = submit_us - frame_budget_us;
        return true;
    }
    *late_us = 0;
    return false;
}

static void update_latency_probe_stall(BenchSummary *summary, uint32_t *current_stall_frames,
                                       int64_t decoder_latency_us) {
    if (summary == NULL || current_stall_frames == NULL) {
        return;
    }

    if (decoder_latency_us < 0) {
        (*current_stall_frames)++;
        if (*current_stall_frames > summary->latency_probe_stall_max_frames) {
            summary->latency_probe_stall_max_frames = *current_stall_frames;
        }
        return;
    }

    *current_stall_frames = 0;
}

static void copy_reason(char *dst, size_t dst_len, const char *value) {
    if (dst == NULL || dst_len == 0) {
        return;
    }

    int written = snprintf(dst, dst_len, "%s", value != NULL ? value : "");
    if (written < 0 || (size_t)written >= dst_len) {
        dst[dst_len - 1] = '\0';
    }
}

static void format_verdict_reason(const BenchSummary *summary, char *reason, size_t reason_len, char *detail,
                                  size_t detail_len) {
    if (reason == NULL || reason_len == 0 || detail == NULL || detail_len == 0) {
        return;
    }

    reason[0] = '\0';
    detail[0] = '\0';
    if (summary == NULL) {
        copy_reason(reason, reason_len, "invalid-summary");
        return;
    }

    if (summary->verdict == BENCH_VERDICT_PASS) {
        copy_reason(reason, reason_len, "ok");
        return;
    }

    if (summary->verdict == BENCH_VERDICT_WARN) {
        if (summary->late_submits > 0 && summary->latency_growth_detected) {
            copy_reason(reason, reason_len, "late-submit+latency-growth");
        } else if (summary->late_submits > 0) {
            copy_reason(reason, reason_len, "late-submit");
        } else if (summary->latency_growth_detected) {
            copy_reason(reason, reason_len, "latency-growth");
        } else {
            copy_reason(reason, reason_len, "warn");
        }

        if (summary->late_submits > 0) {
            int written = snprintf(detail, detail_len, "late=%u/%u max_late=%lldus", summary->late_submits,
                                   summary->frames_submitted, (long long)summary->max_late_us);
            if (written < 0 || (size_t)written >= detail_len) {
                detail[detail_len - 1] = '\0';
            }
        }
        return;
    }

    switch (summary->stop_reason) {
    case BENCH_STOP_DECODER_FAIL:
    case BENCH_STOP_STORAGE_UNDERFLOW:
    case BENCH_STOP_INVALID_FIXTURE:
    case BENCH_STOP_IO_ERROR:
        copy_reason(reason, reason_len, bench_stop_reason_str(summary->stop_reason));
        return;
    case BENCH_STOP_USER:
    case BENCH_STOP_TARGET_FRAMES:
    case BENCH_STOP_EOF:
    case BENCH_STOP_CAP:
    case BENCH_STOP_NONE:
    default:
        break;
    }

    if (summary->source_error != BENCH_SOURCE_ERROR_NONE) {
        copy_reason(reason, reason_len, "source-error");
        int written = snprintf(detail, detail_len, "source=%s", bench_source_error_str(summary->source_error));
        if (written < 0 || (size_t)written >= detail_len) {
            detail[detail_len - 1] = '\0';
        }
    } else if (summary->feed_errors > 0) {
        copy_reason(reason, reason_len, "feed-error");
        int written = snprintf(detail, detail_len, "errors=%u", summary->feed_errors);
        if (written < 0 || (size_t)written >= detail_len) {
            detail[detail_len - 1] = '\0';
        }
    } else if (summary->target_frames > 0 && summary->frames_submitted < summary->target_frames) {
        copy_reason(reason, reason_len, "short-run");
        int written = snprintf(detail, detail_len, "frames=%u/%u", summary->frames_submitted, summary->target_frames);
        if (written < 0 || (size_t)written >= detail_len) {
            detail[detail_len - 1] = '\0';
        }
    } else if (summary->latency_growth_detected) {
        copy_reason(reason, reason_len, "latency-growth");
    } else if (!summary->decoder_latency_available) {
        copy_reason(reason, reason_len, "decoder-latency-missing");
    } else {
        copy_reason(reason, reason_len, "fail");
    }
}

void bench_stats_compute(const FrameRecord *records, int count, int fps, const BenchSummaryInputs *inputs,
                         bool require_decoder_latency, BenchSummary *summary) {
    if (summary == NULL) {
        return;
    }
    memset(summary, 0, sizeof(*summary));

    if (records == NULL || count < 0 || fps <= 0 || inputs == NULL) {
        summary->verdict = BENCH_VERDICT_FAIL;
        return;
    }

    summary->run_length_mode = inputs->run_length_mode;
    summary->target_frames = inputs->target_frames > 0 ? (uint32_t)inputs->target_frames : 0;
    summary->duration_sec = inputs->duration_sec > 0 ? (uint32_t)inputs->duration_sec : 0;
    summary->stop_reason = inputs->stop_reason;
    summary->source_error = inputs->source_error;
    summary->source_mode = inputs->source_mode;
    summary->source_buffer_mib = inputs->source_buffer_mib;

    int64_t submit_sum = 0;
    int64_t wake_jitter_sum = 0;
    int64_t decoder_lat_sum = 0;
    int decoder_lat_count = 0;
    uint32_t current_latency_stall_frames = 0;

    for (int i = 0; i < count; i++) {
        const FrameRecord *r = &records[i];

        summary->frames_submitted++;

        if (r->feed_result != SS4S_VIDEO_FEED_OK) {
            summary->feed_errors++;
        }

        wake_jitter_sum += r->wake_jitter_us;
        if (r->wake_jitter_us > summary->max_wake_jitter_us) {
            summary->max_wake_jitter_us = r->wake_jitter_us;
        }

        int64_t late_us = 0;
        if (submit_exceeded_frame_budget(r->submit_dur_us, fps, &late_us)) {
            summary->late_submits++;
            if (late_us > summary->max_late_us) {
                summary->max_late_us = late_us;
            }
        }

        submit_sum += r->submit_dur_us;
        if (r->submit_dur_us > summary->max_submit_us) {
            summary->max_submit_us = r->submit_dur_us;
        }

        update_latency_probe_stall(summary, &current_latency_stall_frames, r->decoder_latency_us);

        if (r->decoder_latency_us >= 0) {
            decoder_lat_sum += r->decoder_latency_us;
            decoder_lat_count++;
            summary->decoder_latency_available = true;
            if (r->decoder_latency_us > summary->max_decoder_latency_us) {
                summary->max_decoder_latency_us = r->decoder_latency_us;
            }
        }
    }

    if (count > 0) {
        summary->avg_wake_jitter_us = wake_jitter_sum / count;
        summary->avg_submit_us = submit_sum / count;
    }
    if (decoder_lat_count > 0) {
        summary->avg_decoder_latency_us = decoder_lat_sum / decoder_lat_count;
    }

    bool severe_growth = false;
    summary->latency_growth_detected = detect_latency_growth(records, count, &severe_growth);

    /*
     * Hard FAIL paths: anything that says the run did not deliver the
     * requested workload OR encountered a source-side problem.
     */
    bool hard_fail = false;
    switch (inputs->stop_reason) {
    case BENCH_STOP_DECODER_FAIL:
    case BENCH_STOP_STORAGE_UNDERFLOW:
    case BENCH_STOP_INVALID_FIXTURE:
    case BENCH_STOP_IO_ERROR:
        hard_fail = true;
        break;
    case BENCH_STOP_USER:
        /* User interruption is not a test failure. Drop to quality checks. */
        break;
    case BENCH_STOP_TARGET_FRAMES:
    case BENCH_STOP_EOF:
    case BENCH_STOP_CAP:
    case BENCH_STOP_NONE:
    default:
        break;
    }
    if (inputs->source_error != BENCH_SOURCE_ERROR_NONE) {
        hard_fail = true;
    }
    if (inputs->run_length_mode == BENCH_RUN_LENGTH_EXPLICIT && inputs->target_frames > 0 &&
        (int)summary->frames_submitted < inputs->target_frames) {
        /* Did not reach configured workload (and stop_reason did not flag
         * a more specific reason — e.g. could be user-interrupt). For an
         * explicit run, short of target is FAIL unless it was a user stop. */
        if (inputs->stop_reason != BENCH_STOP_USER) {
            hard_fail = true;
        }
    }

    if (hard_fail) {
        summary->verdict = BENCH_VERDICT_FAIL;
        return;
    }

    bool workload_failed =
        summary->feed_errors > 0 || severe_growth || (require_decoder_latency && !summary->decoder_latency_available);

    bool workload_imperfect = summary->late_submits > 0 || summary->latency_growth_detected;

    if (workload_failed) {
        summary->verdict = BENCH_VERDICT_FAIL;
    } else if (workload_imperfect) {
        summary->verdict = BENCH_VERDICT_WARN;
    } else {
        summary->verdict = BENCH_VERDICT_PASS;
    }
}

void bench_stats_init_empty(const BenchSummaryInputs *inputs, BenchSummary *summary) {
    if (summary == NULL) {
        return;
    }
    memset(summary, 0, sizeof(*summary));
    if (inputs == NULL) {
        return;
    }
    summary->run_length_mode = inputs->run_length_mode;
    summary->target_frames = inputs->target_frames > 0 ? (uint32_t)inputs->target_frames : 0;
    summary->duration_sec = inputs->duration_sec > 0 ? (uint32_t)inputs->duration_sec : 0;
    summary->source_mode = inputs->source_mode;
    summary->source_buffer_mib = inputs->source_buffer_mib;
}

void bench_stats_aggregate_outcome(BenchTestOutcome outcome, BenchVerdict verdict, BenchVerdict *worst,
                                   int *completed_tests, int *unsupported_tests) {
    if (worst == NULL || completed_tests == NULL || unsupported_tests == NULL) {
        return;
    }

    switch (outcome) {
    case BENCH_TEST_UNSUPPORTED:
        if (*unsupported_tests < INT_MAX) {
            (*unsupported_tests)++;
        }
        break;
    case BENCH_TEST_COMPLETED:
        if (verdict > *worst) {
            *worst = verdict;
        }
        if (*completed_tests < INT_MAX) {
            (*completed_tests)++;
        }
        break;
    default:
        break;
    }
}
/* ---------- CSV output ---------- */

static const char *codec_name(SS4S_VideoCodec codec) {
    switch (codec) {
    case SS4S_VIDEO_H264:
        return "H264";
    case SS4S_VIDEO_H265:
        return "HEVC";
    default:
        return "UNKNOWN";
    }
}

static int csv_write_string(FILE *f, const char *value) {
    if (fputc('"', f) == EOF) {
        return -1;
    }
    if (value != NULL) {
        for (const char *p = value; *p != '\0'; p++) {
            if (*p == '"' && fputc('"', f) == EOF) {
                return -1;
            }
            if (fputc((unsigned char)*p, f) == EOF) {
                return -1;
            }
        }
    }
    return fputc('"', f) == EOF ? -1 : 0;
}

int bench_stats_write_csv(const char *path, const char *test_name, const StreamInfo *info, const char *fixture,
                          const FrameRecord *records, int count) {
    if (path == NULL || test_name == NULL || info == NULL || fixture == NULL || records == NULL || count < 0) {
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "bench_stats: cannot create '%s'\n", path);
        return -1;
    }

    if (fprintf(f, "test_name,codec,width,height,fps,duration_sec,fixture,"
                   "frame_idx,pts_us,au_size,wake_jitter_us,submit_dur_us,decoder_latency_us,feed_result\n") < 0) {
        fclose(f);
        fprintf(stderr, "bench_stats: cannot write '%s'\n", path);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const FrameRecord *r = &records[i];
        if (csv_write_string(f, test_name) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, codec_name(info->codec)) != 0 ||
            fprintf(f, ",%d,%d,%d,%d,", info->width, info->height, info->fps, info->run_seconds) < 0 ||
            csv_write_string(f, fixture) != 0 ||
            fprintf(f, ",%u,%" PRId64 ",%" PRIu64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%d\n", r->frame_idx, r->pts_us,
                    r->au_size, r->wake_jitter_us, r->submit_dur_us, r->decoder_latency_us, r->feed_result) < 0) {
            fclose(f);
            fprintf(stderr, "bench_stats: cannot write '%s'\n", path);
            return -1;
        }
    }

    if (fclose(f) != 0) {
        fprintf(stderr, "bench_stats: cannot close '%s'\n", path);
        return -1;
    }
    return 0;
}

int bench_stats_write_summary_csv(const char *path, const char *scope_name, const BenchSummaryRow *rows, int count) {
    if (path == NULL || scope_name == NULL || rows == NULL || count < 0) {
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "bench_stats: cannot create '%s'\n", path);
        return -1;
    }

    if (fprintf(f, "scope_name,test_name,fixture,codec,width,height,fps,duration_sec,verdict,"
                   "frames_submitted,target_frames,run_length_mode,stop_reason,source_mode,"
                   "source_buffer_mib,source_error,"
                   "feed_errors,late_submits,late_submit_pct,wake_avg_us,wake_max_us,"
                   "submit_avg_us,submit_max_us,max_late_us,decoder_latency_available,"
                   "decoder_latency_avg_us,decoder_latency_max_us,latency_probe_stall_max_frames,"
                   "latency_growth_detected,verdict_reason,verdict_detail,test_outcome\n") < 0) {
        fclose(f);
        fprintf(stderr, "bench_stats: cannot write '%s'\n", path);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const BenchSummaryRow *row = &rows[i];
        const BenchSummary *summary = &row->summary;
        float late_pct = summary->frames_submitted > 0
                             ? (float)summary->late_submits / (float)summary->frames_submitted * 100.0f
                             : 0.0f;
        char verdict_reason[BENCH_VERDICT_REASON_LEN];
        char verdict_detail[BENCH_VERDICT_DETAIL_LEN];

        if (row->test_outcome == BENCH_TEST_UNSUPPORTED) {
            copy_reason(verdict_reason, sizeof(verdict_reason), "unsupported-codec");
            verdict_detail[0] = '\0';
        } else {
            format_verdict_reason(summary, verdict_reason, sizeof(verdict_reason), verdict_detail,
                                  sizeof(verdict_detail));
        }

        if (csv_write_string(f, scope_name) != 0 || fprintf(f, ",") < 0 || csv_write_string(f, row->test_name) != 0 ||
            fprintf(f, ",") < 0 || csv_write_string(f, row->fixture) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, codec_name(row->info.codec)) != 0 ||
            fprintf(f, ",%d,%d,%d,%d,", row->info.width, row->info.height, row->info.fps, row->info.run_seconds) < 0 ||
            csv_write_string(
                f, row->test_outcome == BENCH_TEST_UNSUPPORTED ? "" : bench_verdict_str(summary->verdict)) != 0 ||
            fprintf(f, ",%u,%u,", summary->frames_submitted, summary->target_frames) < 0 ||
            csv_write_string(f, bench_run_length_mode_str(summary->run_length_mode)) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, bench_stop_reason_str(summary->stop_reason)) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, bench_source_mode_str(summary->source_mode)) != 0 ||
            fprintf(f, ",%u,", summary->source_buffer_mib) < 0 ||
            csv_write_string(f, bench_source_error_str(summary->source_error)) != 0 ||
            fprintf(f,
                    ",%u,%u,%.2f,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%d,%" PRId64 ",%" PRId64
                    ",%u,%d",
                    summary->feed_errors, summary->late_submits, late_pct, summary->avg_wake_jitter_us,
                    summary->max_wake_jitter_us, summary->avg_submit_us, summary->max_submit_us, summary->max_late_us,
                    summary->decoder_latency_available ? 1 : 0, summary->avg_decoder_latency_us,
                    summary->max_decoder_latency_us, summary->latency_probe_stall_max_frames,
                    summary->latency_growth_detected ? 1 : 0) < 0 ||
            fprintf(f, ",") < 0 || csv_write_string(f, verdict_reason) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, verdict_detail) != 0 || fprintf(f, ",") < 0 ||
            csv_write_string(f, bench_test_outcome_str(row->test_outcome)) != 0 || fprintf(f, "\n") < 0) {
            fclose(f);
            fprintf(stderr, "bench_stats: cannot write '%s'\n", path);
            return -1;
        }
    }

    if (fclose(f) != 0) {
        fprintf(stderr, "bench_stats: cannot close '%s'\n", path);
        return -1;
    }
    return 0;
}

/* ---------- Summary printing ---------- */

const char *bench_verdict_str(BenchVerdict v) {
    switch (v) {
    case BENCH_VERDICT_PASS:
        return "PASS";
    case BENCH_VERDICT_WARN:
        return "WARN";
    case BENCH_VERDICT_FAIL:
        return "FAIL";
    default:
        return "???";
    }
}

const char *bench_test_outcome_str(BenchTestOutcome outcome) {
    switch (outcome) {
    case BENCH_TEST_COMPLETED:
        return "completed";
    case BENCH_TEST_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}

void bench_stats_print_summary(const char *test_name, const StreamInfo *info, const BenchSummary *summary) {
    if (test_name == NULL || info == NULL || summary == NULL) {
        return;
    }

    float late_pct =
        summary->frames_submitted > 0 ? (float)summary->late_submits / (float)summary->frames_submitted * 100.0f : 0.0f;
    char verdict_reason[BENCH_VERDICT_REASON_LEN];
    char verdict_detail[BENCH_VERDICT_DETAIL_LEN];

    format_verdict_reason(summary, verdict_reason, sizeof(verdict_reason), verdict_detail, sizeof(verdict_detail));

    printf("  %-40s %s %dx%d@%d  %4s  stop=%s src=%s buf=%uMiB  "
           "frames=%u/%u errors=%u submit_late=%.1f%% "
           "wake_avg=%lld/%lld us submit_avg=%lld/%lld us",
           test_name, codec_name(info->codec), info->width, info->height, info->fps,
           bench_verdict_str(summary->verdict), bench_stop_reason_str(summary->stop_reason),
           bench_source_mode_str(summary->source_mode), summary->source_buffer_mib, summary->frames_submitted,
           summary->target_frames, summary->feed_errors, late_pct, (long long)summary->avg_wake_jitter_us,
           (long long)summary->max_wake_jitter_us, (long long)summary->avg_submit_us,
           (long long)summary->max_submit_us);

    if (summary->source_error != BENCH_SOURCE_ERROR_NONE) {
        printf("  src_err=%s", bench_source_error_str(summary->source_error));
    }

    if (summary->decoder_latency_available) {
        printf("  dec_lat=%lld/%lld us%s", (long long)summary->avg_decoder_latency_us,
               (long long)summary->max_decoder_latency_us, summary->latency_growth_detected ? " GROWTH" : "");
    } else {
        printf("  dec_lat=n/a");
    }
    if (summary->verdict != BENCH_VERDICT_PASS) {
        printf("  why=%s", verdict_reason);
        if (verdict_detail[0] != '\0') {
            printf(" %s", verdict_detail);
        }
    }
    printf("\n");
}
