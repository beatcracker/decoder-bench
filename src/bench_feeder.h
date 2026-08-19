#pragma once

#include <signal.h>

#include "bench_source.h"

struct SS4S_Player;

typedef enum BenchEventPumpResult {
    BENCH_EVENT_PUMP_CONTINUE = 0,
    BENCH_EVENT_PUMP_STOP = 1,
    BENCH_EVENT_PUMP_ERROR = 2,
} BenchEventPumpResult;

typedef BenchEventPumpResult (*BenchEventPumpFn)(void *ctx);

/**
 * Measure short monotonic timer wake overshoot using absolute deadlines.
 *
 * Returns -1 if arguments are invalid or deadline math is not representable.
 */
int bench_timer_probe(int samples, int64_t interval_us, BenchTimerProbe *probe);

/**
 * Run the paced feed loop for one benchmark test.
 *
 * @param player SS4S player instance (video already opened).
 * @param source Open BenchSource.
 * @param fps Feed cadence FPS.
 * @param target_frames Total target frames; the source decides what EOF means
 *                      using its own configured target. Pass <= 0 for auto.
 * @param records Preallocated array for per-frame records.
 * @param max_records Capacity of records array.
 * @param records_written Output: number of records actually written.
 * @param pump_events Optional platform service called once per frame before
 *                    sleeping. It may continue, request a clean user stop, or
 *                    report an infrastructure error.
 * @param pump_ctx Opaque pointer passed to pump_events.
 * @param interrupted Pointer to flag that can be set to stop the loop.
 * @param stop_reason Out: final stop reason.
 * @param source_error Out: final source error.
 * @return 0 on a normal stop. -1 on argument or platform-service error.
 */
int bench_feed_loop(struct SS4S_Player *player, BenchSource *source, int fps, int target_frames, FrameRecord *records,
                    int max_records, int *records_written, BenchEventPumpFn pump_events, void *pump_ctx,
                    const volatile sig_atomic_t *interrupted, BenchStopReason *stop_reason,
                    BenchSourceError *source_error);

/**
 * Stable label for stop reason, suitable for CSV/logging.
 */
const char *bench_stop_reason_str(BenchStopReason reason);

/**
 * Stable label for run length mode.
 */
const char *bench_run_length_mode_str(BenchRunLengthMode mode);
