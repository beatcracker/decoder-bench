#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_source_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const unsigned char h264_sps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x2a, 0xac, 0x2b, 0x40,
                                         0x3c, 0x01, 0x13, 0xf2, 0xe0, 0x2d, 0x41, 0x81, 0x81, 0xa9, 0x40,
                                         0x00, 0x00, 0xfa, 0x00, 0x00, 0x75, 0x30, 0x23, 0xc7, 0x0a, 0xa8};
static const unsigned char h264_pps[] = {0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80};
static const unsigned char h264_idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x80};
static const unsigned char h264_aud[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0x10};

static void write_full(int fd, const unsigned char *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(fd, data + written, size - written);
        assert(n > 0);
        written += (size_t)n;
    }
}

static void write_zeroes(int fd, size_t size) {
    unsigned char zeroes[64 * 1024] = {0};
    size_t written = 0;
    while (written < size) {
        size_t chunk = size - written;
        if (chunk > sizeof(zeroes)) {
            chunk = sizeof(zeroes);
        }
        write_full(fd, zeroes, chunk);
        written += chunk;
    }
}

static void write_padded_idr(int fd, size_t total_size) {
    assert(total_size >= sizeof(h264_idr));
    write_full(fd, h264_idr, sizeof(h264_idr));
    write_zeroes(fd, total_size - sizeof(h264_idr));
}

static void make_small_fixture(char *path, size_t path_size) {
    int written = snprintf(path, path_size, "%s", "/tmp/decoder-bench-small-XXXXXX");
    assert(written > 0 && (size_t)written < path_size);
    int fd = mkstemp(path);
    assert(fd >= 0);
    write_full(fd, h264_sps, sizeof(h264_sps));
    write_full(fd, h264_pps, sizeof(h264_pps));
    write_full(fd, h264_idr, sizeof(h264_idr));
    assert(close(fd) == 0);
}

static void make_large_fixture(char *path, size_t path_size) {
    int written = snprintf(path, path_size, "%s", "/tmp/decoder-bench-large-XXXXXX");
    assert(written > 0 && (size_t)written < path_size);
    int fd = mkstemp(path);
    assert(fd >= 0);
    write_full(fd, h264_sps, sizeof(h264_sps));
    write_full(fd, h264_pps, sizeof(h264_pps));

    const size_t au_payload_size = 1024u * 1024u;
    const size_t buffer_size = (size_t)BENCH_SOURCE_BUFFER_MIN_MIB * 1024u * 1024u;
    const size_t target_size = buffer_size + (2u * au_payload_size);
    size_t total = sizeof(h264_sps) + sizeof(h264_pps);
    while (total < target_size) {
        write_padded_idr(fd, au_payload_size);
        total += au_payload_size;
        write_full(fd, h264_aud, sizeof(h264_aud));
        total += sizeof(h264_aud);
    }
    assert(close(fd) == 0);
}

static BenchSourceOptions min_buffer_options(void) {
    BenchSourceOptions options = {
        .source_buffer_mib = BENCH_SOURCE_BUFFER_MIN_MIB,
        .target_frames = 0,
    };
    return options;
}

static void test_complete_acquire_release_to_eof(void) {
    char path[64];
    make_small_fixture(path, sizeof(path));

    BenchSourceOptions options = min_buffer_options();
    BenchSource *source = NULL;
    assert(bench_source_open(path, &options, NULL, &source) == BENCH_SOURCE_OK);
    assert(source != NULL);
    assert(bench_source_mode(source) == BENCH_SOURCE_MODE_COMPLETE);
    assert(bench_source_buffer_allocated_for_testing(source, 0));
    assert(!bench_source_buffer_allocated_for_testing(source, 1));
    assert(bench_source_carry_capacity_for_testing(source) == BENCH_MAX_SINGLE_AU_BYTES);
    assert(!bench_source_needs_promotion_for_testing(source));
    assert(!bench_source_loader_started_for_testing(source));
    assert(bench_source_prepare_playback(source) == BENCH_SOURCE_OK);
    assert(!bench_source_loader_started_for_testing(source));

    AccessUnit au;
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_OK);
    assert(au.is_keyframe);
    assert(bench_source_has_acquired_for_testing(source));
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_IO_ERROR);
    assert(bench_source_release(source) == BENCH_SOURCE_OK);
    assert(!bench_source_has_acquired_for_testing(source));
    assert(bench_source_release(source) == BENCH_SOURCE_IO_ERROR);
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_EOF);

    bench_source_close(source);
    assert(unlink(path) == 0);
}

static void drain_until_one_active_au(BenchSource *source) {
    while (bench_source_active_au_remaining_for_testing(source) > 1) {
        AccessUnit au;
        assert(bench_source_acquire(source, &au) == BENCH_SOURCE_OK);
        assert(bench_source_release(source) == BENCH_SOURCE_OK);
    }
    assert(bench_source_active_au_remaining_for_testing(source) == 1);
}

static void test_streaming_prepare_and_swap_wait_for_release(void) {
    char path[64];
    make_large_fixture(path, sizeof(path));

    BenchSourceOptions options = min_buffer_options();
    BenchSource *source = NULL;
    assert(bench_source_open(path, &options, NULL, &source) == BENCH_SOURCE_OK);
    assert(source != NULL);
    assert(bench_source_mode(source) == BENCH_SOURCE_MODE_STREAMING);
    assert(bench_source_buffer_allocated_for_testing(source, 0));
    assert(!bench_source_buffer_allocated_for_testing(source, 1));
    assert(bench_source_carry_capacity_for_testing(source) == BENCH_MAX_SINGLE_AU_BYTES);
    assert(bench_source_needs_promotion_for_testing(source));
    assert(!bench_source_loader_started_for_testing(source));

    assert(bench_source_prepare_playback(source) == BENCH_SOURCE_OK);
    assert(!bench_source_needs_promotion_for_testing(source));
    assert(bench_source_loader_started_for_testing(source));
    assert(bench_source_buffer_allocated_for_testing(source, 1));
    assert(bench_source_wait_inactive_loaded_for_testing(source, 5000));

    int first_active = bench_source_active_buffer_for_testing(source);
    drain_until_one_active_au(source);

    AccessUnit au;
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_OK);
    assert(bench_source_active_au_remaining_for_testing(source) == 0);
    assert(bench_source_active_buffer_for_testing(source) == first_active);
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_IO_ERROR);
    assert(bench_source_active_buffer_for_testing(source) == first_active);
    assert(bench_source_release(source) == BENCH_SOURCE_OK);
    assert(bench_source_acquire(source, &au) == BENCH_SOURCE_OK);
    assert(bench_source_active_buffer_for_testing(source) != first_active);
    assert(bench_source_release(source) == BENCH_SOURCE_OK);

    bench_source_close(source);
    assert(unlink(path) == 0);
}

static void test_prefetch_opens_first_buffer_only(void) {
    char path[64];
    make_large_fixture(path, sizeof(path));

    BenchSourceOptions options = min_buffer_options();
    BenchSourcePrefetch *prefetch = NULL;
    assert(bench_source_prefetch_start(path, &options, NULL, &prefetch) == BENCH_SOURCE_OK);
    assert(prefetch != NULL);

    BenchSource *source = NULL;
    assert(bench_source_prefetch_join(prefetch, &source) == BENCH_SOURCE_OK);
    assert(source != NULL);
    assert(bench_source_mode(source) == BENCH_SOURCE_MODE_STREAMING);
    assert(bench_source_buffer_allocated_for_testing(source, 0));
    assert(!bench_source_buffer_allocated_for_testing(source, 1));
    assert(bench_source_needs_promotion_for_testing(source));
    assert(!bench_source_loader_started_for_testing(source));

    bench_source_close(source);
    assert(unlink(path) == 0);
}

int main(void) {
    test_complete_acquire_release_to_eof();
    test_streaming_prepare_and_swap_wait_for_release();
    test_prefetch_opens_first_buffer_only();
    return 0;
}
