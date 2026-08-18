#include "bench_decoder_support.h"

#include <assert.h>

int main(void) {
    SS4S_VideoCapabilities webos4 = {
        .codecs = SS4S_VIDEO_H264,
    };
    SS4S_VideoCapabilities webos5 = {
        .codecs = SS4S_VIDEO_H264 | SS4S_VIDEO_H265,
    };

    assert(bench_decoder_supports_codec(&webos4, SS4S_VIDEO_H264));
    assert(!bench_decoder_supports_codec(&webos4, SS4S_VIDEO_H265));
    assert(bench_decoder_supports_codec(&webos5, SS4S_VIDEO_H264));
    assert(bench_decoder_supports_codec(&webos5, SS4S_VIDEO_H265));
    assert(!bench_decoder_supports_codec(NULL, SS4S_VIDEO_H264));
    assert(bench_decoder_open_is_unsupported(SS4S_VIDEO_OPEN_UNSUPPORTED_CODEC));
    assert(!bench_decoder_open_is_unsupported(SS4S_VIDEO_OPEN_OK));
    assert(!bench_decoder_open_is_unsupported(SS4S_VIDEO_OPEN_ERROR));
    return 0;
}
