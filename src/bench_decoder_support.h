#pragma once

#include <stdbool.h>

#include "ss4s/video.h"

bool bench_decoder_supports_codec(const SS4S_VideoCapabilities *capabilities, SS4S_VideoCodec codec);
bool bench_decoder_open_is_unsupported(SS4S_VideoOpenResult result);
