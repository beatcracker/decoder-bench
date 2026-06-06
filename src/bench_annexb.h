#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bench_types.h"

/**
 * Parse an Annex B buffer into borrowed access-unit slices.
 *
 * The returned AccessUnit entries point into data. The caller owns the AU
 * array and must free it. The trailing incomplete AU is not emitted unless
 * eof_seen is true; out_carry_offset reports the byte offset that must be
 * carried into the next parse call.
 */
int bench_annexb_parse_access_units(const unsigned char *data, size_t size, bool eof_seen, SS4S_VideoCodec *codec,
                                    int *width, int *height, AccessUnit **aus, int *au_count, int *au_cap,
                                    size_t *out_carry_offset);
