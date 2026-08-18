#pragma once

#include "bench_types.h"

/**
 * Run/source metadata threaded into summary computation. These values cannot
 * be inferred from per-frame records alone — the feed loop and source layer
 * own the ground truth.
 */
typedef struct BenchSummaryInputs {
    BenchRunLengthMode run_length_mode;
    int target_frames; /* explicit target or auto cap */
    int duration_sec;  /* explicit run_seconds or auto cap */
    BenchStopReason stop_reason;
    BenchSourceError source_error;
    BenchSourceMode source_mode;
    unsigned int source_buffer_mib;
} BenchSummaryInputs;

/**
 * Compute summary statistics from per-frame records and run metadata.
 *
 * Invalid inputs (NULL pointers, negative count) produce FAIL.
 */
void bench_stats_compute(const FrameRecord *records, int count, int fps, const BenchSummaryInputs *inputs,
                         bool require_decoder_latency, BenchSummary *summary);

/**
 * Initialize a metric-free summary for a row that did not enter the measured
 * feed loop.
 */
void bench_stats_init_empty(const BenchSummaryInputs *inputs, BenchSummary *summary);

/**
 * Write per-frame CSV after the run completes.
 *
 * @param path Output CSV path.
 * @param test_name Test section name.
 * @param info Stream info (codec, resolution, fps, run_seconds).
 * @param fixture Fixture filename.
 * @param records Frame records.
 * @param count Number of records.
 * @return 0 on success, -1 on error.
 *
 * All pointer arguments must be non-NULL. String fields are CSV-quoted.
 */
int bench_stats_write_csv(const char *path, const char *test_name, const StreamInfo *info, const char *fixture,
                          const FrameRecord *records, int count);

/**
 * Write one operator-facing summary CSV with one row per completed test.
 *
 * @param path Output CSV path.
 * @param scope_name Suite name or direct-run scope label.
 * @param rows Summary rows.
 * @param count Number of rows.
 * @return 0 on success, -1 on error.
 */
int bench_stats_write_summary_csv(const char *path, const char *scope_name, const BenchSummaryRow *rows, int count);

/**
 * Print a one-line summary to stdout.
 *
 * Invalid pointer arguments are ignored.
 */
void bench_stats_print_summary(const char *test_name, const StreamInfo *info, const BenchSummary *summary);

/**
 * Return a string representation of a verdict.
 */
const char *bench_verdict_str(BenchVerdict v);

/**
 * Return the stable summary-CSV spelling of a test outcome.
 */
const char *bench_test_outcome_str(BenchTestOutcome outcome);

/**
 * Fold one completed or unsupported row into overall counters.
 */
void bench_stats_aggregate_outcome(BenchTestOutcome outcome, BenchVerdict verdict, BenchVerdict *worst,
                                   int *completed_tests, int *unsupported_tests);
