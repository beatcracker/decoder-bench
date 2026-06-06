#pragma once

#include <signal.h>

#include "bench_types.h"

typedef struct BenchSource BenchSource;
typedef struct BenchSourcePrefetch BenchSourcePrefetch;

/**
 * Run the once-per-process storage warmup. Performs a bounded read of the
 * given file and asks the kernel to drop those pages, leaving the storage
 * subsystem hot but the page cache cold.
 *
 * @param path Absolute path to the fixture used as warmup target.
 * @param limit_bytes Maximum bytes to read. Pass 0 for BENCH_WARMUP_BYTES_DEFAULT.
 * @param max_ms Best-effort wall-clock deadline checked between chunks.
 *               Pass 0 for BENCH_WARMUP_MAX_MS_DEFAULT.
 * @return 0 on success (bytes read or EOF). -1 on open/read failure or deadline trip.
 */
int bench_storage_warmup(const char *path, size_t limit_bytes, int max_ms);

/**
 * Open an Annex B access-unit source and validate initial stream metadata.
 * On success, the caller owns the returned source.
 */
BenchSourceOutcome bench_source_open(const char *path, const BenchSourceOptions *options,
                                     const volatile sig_atomic_t *interrupted, BenchSource **out_source);

/**
 * Free a source plus any loader thread, buffers, and file handle. Accepts NULL.
 * Callers must release any acquired AU before closing.
 */
void bench_source_close(BenchSource *source);

SS4S_VideoCodec bench_source_codec(const BenchSource *source);
void bench_source_dimensions(const BenchSource *source, int *width, int *height);
BenchSourceMode bench_source_mode(const BenchSource *source);
unsigned int bench_source_buffer_mib(const BenchSource *source);

/**
 * Prepare a source for measured playback after open or prefetch join.
 */
BenchSourceOutcome bench_source_prepare_playback(BenchSource *source);

/**
 * Acquire one borrowed access unit.
 *
 * A successful acquire must be followed by exactly one bench_source_release
 * before another acquire or source close. Returned bytes remain valid until
 * release returns.
 */
BenchSourceOutcome bench_source_acquire(BenchSource *source, AccessUnit *au_out);

/**
 * Release the access unit returned by the most recent successful acquire.
 */
BenchSourceOutcome bench_source_release(BenchSource *source);

/**
 * Start one-test-ahead source prefetch. The returned slot must be joined or
 * abandoned.
 */
BenchSourceOutcome bench_source_prefetch_start(const char *path, const BenchSourceOptions *options,
                                               const volatile sig_atomic_t *interrupted,
                                               BenchSourcePrefetch **out_prefetch);

/**
 * Wait for a prefetch to finish and consume the slot. On success, out_source
 * receives the opened source and the caller owns it. The prefetch object is
 * freed before return.
 */
BenchSourceOutcome bench_source_prefetch_join(BenchSourcePrefetch *prefetch, BenchSource **out_source);

/**
 * Wait for a prefetch to finish, close any opened source, and free the slot.
 * Accepts NULL.
 */
void bench_source_prefetch_abandon(BenchSourcePrefetch *prefetch);

BenchSourceError bench_source_outcome_to_error(BenchSourceOutcome outcome);
const char *bench_source_error_str(BenchSourceError err);
const char *bench_source_mode_str(BenchSourceMode mode);
