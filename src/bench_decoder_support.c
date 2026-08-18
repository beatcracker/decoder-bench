#include "bench_decoder_support.h"

bool bench_decoder_supports_codec(const SS4S_VideoCapabilities *capabilities, SS4S_VideoCodec codec) {
    if (capabilities == NULL || codec == SS4S_VIDEO_NONE) {
        return false;
    }
    return (capabilities->codecs & codec) == codec;
}

bool bench_decoder_open_is_unsupported(SS4S_VideoOpenResult result) {
    return result == SS4S_VIDEO_OPEN_UNSUPPORTED_CODEC;
}
