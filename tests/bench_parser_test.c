#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_annexb.h"
#include "bench_source.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const unsigned char h264_sps[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x2a, 0xac, 0x2b, 0x40,
                                         0x3c, 0x01, 0x13, 0xf2, 0xe0, 0x2d, 0x41, 0x81, 0x81, 0xa9, 0x40,
                                         0x00, 0x00, 0xfa, 0x00, 0x00, 0x75, 0x30, 0x23, 0xc7, 0x0a, 0xa8};
static const unsigned char h264_pps[] = {0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80};
static const unsigned char h264_aud[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0x10};
static const unsigned char h264_idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x80};

static void append_bytes(unsigned char *dst, size_t cap, size_t *pos, const unsigned char *src, size_t src_size) {
    assert(dst != NULL);
    assert(pos != NULL);
    assert(src != NULL);
    assert(*pos <= cap);
    assert(src_size <= cap - *pos);
    memcpy(dst + *pos, src, src_size);
    *pos += src_size;
}

static int parse_for_test(const unsigned char *data, size_t size, bool eof_seen, SS4S_VideoCodec *codec, int *width,
                          int *height, AccessUnit **aus, int *au_count, size_t *carry_offset) {
    int au_cap = 0;
    return bench_annexb_parse_access_units(data, size, eof_seen, codec, width, height, aus, au_count, &au_cap,
                                           carry_offset);
}

static void test_pending_non_vcl_headers_are_carried(void) {
    unsigned char data[sizeof(h264_sps) + sizeof(h264_pps) + sizeof(h264_idr) + sizeof(h264_sps) + sizeof(h264_pps) +
                       sizeof(h264_aud)];
    size_t pos = 0;
    append_bytes(data, sizeof(data), &pos, h264_sps, sizeof(h264_sps));
    append_bytes(data, sizeof(data), &pos, h264_pps, sizeof(h264_pps));
    append_bytes(data, sizeof(data), &pos, h264_idr, sizeof(h264_idr));
    size_t next_au_offset = pos;
    append_bytes(data, sizeof(data), &pos, h264_sps, sizeof(h264_sps));
    append_bytes(data, sizeof(data), &pos, h264_pps, sizeof(h264_pps));
    append_bytes(data, sizeof(data), &pos, h264_aud, sizeof(h264_aud));

    SS4S_VideoCodec codec = SS4S_VIDEO_NONE;
    int width = 0;
    int height = 0;
    AccessUnit *aus = NULL;
    int au_count = 0;
    size_t carry_offset = 0;
    assert(parse_for_test(data, pos, false, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(codec == SS4S_VIDEO_H264);
    assert(width == 1920);
    assert(height == 1080);
    assert(au_count == 1);
    assert(carry_offset == next_au_offset);
    free(aus);
}

static void test_mid_vcl_carries_from_au_start(void) {
    unsigned char data[sizeof(h264_sps) + sizeof(h264_pps) + sizeof(h264_idr)];
    size_t pos = 0;
    append_bytes(data, sizeof(data), &pos, h264_sps, sizeof(h264_sps));
    append_bytes(data, sizeof(data), &pos, h264_pps, sizeof(h264_pps));
    append_bytes(data, sizeof(data), &pos, h264_idr, sizeof(h264_idr));

    SS4S_VideoCodec codec = SS4S_VIDEO_NONE;
    int width = 0;
    int height = 0;
    AccessUnit *aus = NULL;
    int au_count = 0;
    size_t carry_offset = 0;
    assert(parse_for_test(data, pos, false, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(au_count == 0);
    assert(carry_offset == 0);
    free(aus);
}

static void test_start_code_straddle_is_preserved(void) {
    unsigned char first[sizeof(h264_sps) + sizeof(h264_pps) + sizeof(h264_idr) + 2];
    size_t first_size = 0;
    append_bytes(first, sizeof(first), &first_size, h264_sps, sizeof(h264_sps));
    append_bytes(first, sizeof(first), &first_size, h264_pps, sizeof(h264_pps));
    append_bytes(first, sizeof(first), &first_size, h264_idr, sizeof(h264_idr));
    first[first_size++] = 0x00;
    first[first_size++] = 0x00;

    SS4S_VideoCodec codec = SS4S_VIDEO_NONE;
    int width = 0;
    int height = 0;
    AccessUnit *aus = NULL;
    int au_count = 0;
    size_t carry_offset = 0;
    assert(parse_for_test(first, first_size, false, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(au_count == 0);
    assert(carry_offset == 0);
    free(aus);

    unsigned char second[sizeof(first) + 3];
    size_t second_size = 0;
    append_bytes(second, sizeof(second), &second_size, first, first_size);
    second[second_size++] = 0x01;
    second[second_size++] = 0x09;
    second[second_size++] = 0x10;
    aus = NULL;
    au_count = 0;
    carry_offset = 0;
    assert(parse_for_test(second, second_size, true, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(au_count == 1);
    assert(aus[0].is_keyframe);
    assert(carry_offset == second_size);
    free(aus);
}

static void test_split_sps_waits_for_reassembly(void) {
    const size_t split = 12;
    SS4S_VideoCodec codec = SS4S_VIDEO_H264;
    int width = 0;
    int height = 0;
    AccessUnit *aus = NULL;
    int au_count = 0;
    size_t carry_offset = 0;
    assert(parse_for_test(h264_sps, split, false, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(au_count == 0);
    assert(width == 0);
    assert(height == 0);
    assert(carry_offset == 0);
    free(aus);

    unsigned char data[sizeof(h264_sps) + sizeof(h264_pps) + sizeof(h264_idr)];
    size_t pos = 0;
    append_bytes(data, sizeof(data), &pos, h264_sps, sizeof(h264_sps));
    append_bytes(data, sizeof(data), &pos, h264_pps, sizeof(h264_pps));
    append_bytes(data, sizeof(data), &pos, h264_idr, sizeof(h264_idr));
    aus = NULL;
    au_count = 0;
    carry_offset = 0;
    assert(parse_for_test(data, pos, true, &codec, &width, &height, &aus, &au_count, &carry_offset) == 0);
    assert(width == 1920);
    assert(height == 1080);
    assert(au_count == 1);
    free(aus);
}

static void test_oversize_carry_is_invalid_fixture(void) {
    char path[] = "/tmp/decoder-bench-parser-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    off_t file_size = (off_t)BENCH_SOURCE_BUFFER_MIN_MIB * 1024 * 1024 + 1;
    assert(ftruncate(fd, file_size) == 0);
    assert(close(fd) == 0);

    BenchSourceOptions options = {
        .source_buffer_mib = BENCH_SOURCE_BUFFER_MIN_MIB,
        .target_frames = 0,
    };
    BenchSource *source = NULL;
    assert(bench_source_open(path, &options, NULL, &source) == BENCH_SOURCE_INVALID_FIXTURE);
    assert(source == NULL);
    assert(unlink(path) == 0);
}

int main(void) {
    test_pending_non_vcl_headers_are_carried();
    test_mid_vcl_carries_from_au_start();
    test_start_code_straddle_is_preserved();
    test_split_sps_waits_for_reassembly();
    test_oversize_carry_is_invalid_fixture();
    return 0;
}
