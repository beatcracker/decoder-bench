#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_annexb.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "sps_parser.h"

typedef struct NalBitReader {
    const unsigned char *data;
    size_t size;
    size_t bit_offset;
    int zero_count;
} NalBitReader;

typedef struct NaluEntry {
    const unsigned char *data;
    size_t size;
    bool complete;
} NaluEntry;

static const unsigned char *find_start_code(const unsigned char *p, const unsigned char *end, int *sc_len) {
    while (p + 2 < end) {
        if (p[0] == 0x00 && p[1] == 0x00) {
            if (p[2] == 0x01) {
                *sc_len = 3;
                return p;
            }
            if (p[2] == 0x00 && p + 3 < end && p[3] == 0x01) {
                *sc_len = 4;
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static int get_start_code_len(const unsigned char *data, size_t size) {
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
        return 4;
    }
    if (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        return 3;
    }
    return 0;
}

static bool nal_read_bit(NalBitReader *br, unsigned int *value) {
    for (;;) {
        if (br->bit_offset / 8 >= br->size) {
            return false;
        }
        unsigned char byte = br->data[br->bit_offset / 8];
        if (br->bit_offset % 8 == 0) {
            if (byte == 0x03 && br->zero_count >= 2) {
                br->bit_offset += 8;
                br->zero_count = 0;
                continue;
            }
            if (byte == 0x00) {
                br->zero_count++;
            } else {
                br->zero_count = 0;
            }
        }
        *value = (byte >> (7 - (br->bit_offset % 8))) & 0x01;
        br->bit_offset++;
        return true;
    }
}

static bool nal_read_ue(NalBitReader *br, unsigned int *value) {
    unsigned int bit = 0;
    unsigned int leading_zeroes = 0;
    while (true) {
        if (!nal_read_bit(br, &bit)) {
            return false;
        }
        if (bit != 0) {
            break;
        }
        leading_zeroes++;
        if (leading_zeroes > 31) {
            return false;
        }
    }

    unsigned int suffix = 0;
    for (unsigned int i = 0; i < leading_zeroes; i++) {
        if (!nal_read_bit(br, &bit)) {
            return false;
        }
        suffix = (suffix << 1) | bit;
    }
    *value = ((1u << leading_zeroes) - 1u) + suffix;
    return true;
}

static bool h264_is_vcl(uint8_t nal_type) {
    return nal_type >= 1 && nal_type <= 5;
}
static bool h264_is_keyframe(uint8_t nal_type) {
    return nal_type == 5;
}
static bool hevc_is_vcl(uint8_t nal_type) {
    return nal_type <= 31;
}
static bool hevc_is_keyframe(uint8_t nal_type) {
    return nal_type == 19 || nal_type == 20 || nal_type == 21;
}
static bool hevc_header_valid(const unsigned char *hdr, size_t hdr_size) {
    return hdr_size >= 2 && (hdr[0] & 0x80) == 0 && (hdr[1] & 0x07) != 0;
}
static bool h264_non_vcl_starts_next_au(uint8_t nal_type) {
    return nal_type == 7 || nal_type == 8 || nal_type == 9;
}
static bool hevc_non_vcl_starts_next_au(uint8_t nal_type) {
    return nal_type == 32 || nal_type == 33 || nal_type == 34 || nal_type == 35 || nal_type == 39;
}

static int h264_starts_new_picture(const unsigned char *hdr, size_t hdr_size, bool *starts_new_picture) {
    if (starts_new_picture == NULL || hdr_size < 2) {
        return -1;
    }
    NalBitReader br = {.data = hdr + 1, .size = hdr_size - 1, .bit_offset = 0, .zero_count = 0};
    unsigned int first_mb = 0;
    if (!nal_read_ue(&br, &first_mb)) {
        return -1;
    }
    *starts_new_picture = first_mb == 0;
    return 0;
}

static int hevc_starts_new_picture(const unsigned char *hdr, size_t hdr_size, bool *starts_new_picture) {
    if (starts_new_picture == NULL || hdr_size < 3) {
        return -1;
    }
    NalBitReader br = {.data = hdr + 2, .size = hdr_size - 2, .bit_offset = 0, .zero_count = 0};
    unsigned int first_slice = 0;
    if (!nal_read_bit(&br, &first_slice)) {
        return -1;
    }
    *starts_new_picture = first_slice != 0;
    return 0;
}

static int scan_nalus(const unsigned char *data, size_t size, bool eof_seen, NaluEntry **out_nalus, int *out_count) {
    if (data == NULL || out_nalus == NULL || out_count == NULL) {
        return -1;
    }
    *out_nalus = NULL;
    *out_count = 0;

    int cap = 1024;
    int count = 0;
    NaluEntry *nalus = malloc(sizeof(NaluEntry) * (size_t)cap);
    if (nalus == NULL) {
        return -1;
    }

    const unsigned char *end = data + size;
    int sc_len = 0;
    const unsigned char *sc = find_start_code(data, end, &sc_len);
    if (sc == NULL) {
        free(nalus);
        return 0;
    }

    while (sc != NULL) {
        const unsigned char *nalu_start = sc + sc_len;
        int next_sc_len = 0;
        const unsigned char *next_sc = find_start_code(nalu_start, end, &next_sc_len);
        size_t nalu_size = next_sc != NULL ? (size_t)(next_sc - nalu_start) : (size_t)(end - nalu_start);
        bool complete = next_sc != NULL || eof_seen;
        if (nalu_size > 0 || !complete) {
            if (count >= cap) {
                if (cap > INT_MAX / 2 || (size_t)cap * 2u > SIZE_MAX / sizeof(NaluEntry)) {
                    free(nalus);
                    return -1;
                }
                int new_cap = cap * 2;
                NaluEntry *tmp = realloc(nalus, sizeof(NaluEntry) * (size_t)new_cap);
                if (tmp == NULL) {
                    free(nalus);
                    return -1;
                }
                nalus = tmp;
                cap = new_cap;
            }
            nalus[count].data = sc;
            nalus[count].size = (size_t)(nalu_start - sc) + nalu_size;
            nalus[count].complete = complete;
            count++;
        }
        sc = next_sc;
        sc_len = next_sc_len;
    }

    *out_nalus = nalus;
    *out_count = count;
    return 0;
}

static int set_detected_codec(SS4S_VideoCodec *codec, SS4S_VideoCodec candidate) {
    if (*codec == SS4S_VIDEO_NONE) {
        *codec = candidate;
        return 0;
    }
    return *codec == candidate ? 0 : -1;
}

static int aus_push(AccessUnit **aus, int *count, int *cap, const unsigned char *data, size_t size, bool is_keyframe) {
    if (aus == NULL || count == NULL || cap == NULL || *count < 0 || *cap < 0 || size > BENCH_MAX_SINGLE_AU_BYTES) {
        return -1;
    }
    if (*count >= *cap) {
        int new_cap = *cap == 0 ? 256 : *cap;
        while (new_cap < *count + 1) {
            if (new_cap > INT_MAX / 2) {
                return -1;
            }
            new_cap *= 2;
        }
        if ((size_t)new_cap > SIZE_MAX / sizeof(AccessUnit)) {
            return -1;
        }
        AccessUnit *tmp = realloc(*aus, sizeof(AccessUnit) * (size_t)new_cap);
        if (tmp == NULL) {
            return -1;
        }
        *aus = tmp;
        *cap = new_cap;
    }
    (*aus)[*count].data = data;
    (*aus)[*count].size = size;
    (*aus)[*count].is_keyframe = is_keyframe;
    (*count)++;
    return 0;
}

int bench_annexb_parse_access_units(const unsigned char *data, size_t size, bool eof_seen, SS4S_VideoCodec *codec,
                                    int *width, int *height, AccessUnit **aus, int *au_count, int *au_cap,
                                    size_t *out_carry_offset) {
    if (data == NULL || codec == NULL || width == NULL || height == NULL || aus == NULL || au_count == NULL ||
        au_cap == NULL || out_carry_offset == NULL) {
        return -1;
    }
    *au_count = 0;
    *out_carry_offset = size;

    NaluEntry *nalus = NULL;
    int nalu_count = 0;
    if (scan_nalus(data, size, eof_seen, &nalus, &nalu_count) != 0) {
        free(nalus);
        return -1;
    }
    if (nalu_count == 0) {
        if (eof_seen) {
            free(nalus);
            return size == 0 ? 0 : -1;
        }
        *out_carry_offset = 0;
        free(nalus);
        return 0;
    }

    for (int i = 0; i < nalu_count; i++) {
        if (!nalus[i].complete) {
            continue;
        }
        const unsigned char *nalu_data = nalus[i].data;
        int sc_len = get_start_code_len(nalu_data, nalus[i].size);
        if (sc_len <= 0 || (size_t)sc_len >= nalus[i].size) {
            free(nalus);
            return -1;
        }
        const unsigned char *hdr = nalu_data + sc_len;
        size_t hdr_size = nalus[i].size - (size_t)sc_len;
        uint8_t h264_type = hdr[0] & 0x1F;
        if (h264_type == 7) {
            sps_dimension_t dim;
            if (sps_parse_dimension_h264(hdr, &dim)) {
                if (set_detected_codec(codec, SS4S_VIDEO_H264) != 0) {
                    free(nalus);
                    return -1;
                }
                if (*width <= 0) {
                    *width = dim.width;
                }
                if (*height <= 0) {
                    *height = dim.height;
                }
            }
        }
        if (*codec != SS4S_VIDEO_H264 && hevc_header_valid(hdr, hdr_size)) {
            uint8_t hevc_type = (hdr[0] >> 1) & 0x3F;
            if (hevc_type == 33) {
                sps_dimension_t dim;
                if (sps_parse_dimension_hevc(hdr, &dim)) {
                    if (set_detected_codec(codec, SS4S_VIDEO_H265) != 0) {
                        free(nalus);
                        return -1;
                    }
                    if (*width <= 0) {
                        *width = dim.width;
                    }
                    if (*height <= 0) {
                        *height = dim.height;
                    }
                }
            }
        }
    }

    if (*codec == SS4S_VIDEO_NONE) {
        if (!eof_seen) {
            *out_carry_offset = (size_t)(nalus[0].data - data);
            free(nalus);
            return 0;
        }
        free(nalus);
        return -1;
    }

    const unsigned char *au_start = nalus[0].data;
    bool au_has_vcl = false;
    bool au_is_keyframe = false;

    for (int i = 0; i < nalu_count; i++) {
        if (!nalus[i].complete) {
            break;
        }
        const unsigned char *nalu_data = nalus[i].data;
        int sc_len = get_start_code_len(nalu_data, nalus[i].size);
        if (sc_len <= 0 || (size_t)sc_len >= nalus[i].size) {
            free(nalus);
            return -1;
        }
        const unsigned char *hdr = nalu_data + sc_len;
        size_t hdr_size = nalus[i].size - (size_t)sc_len;

        bool is_vcl = false;
        bool is_keyframe = false;
        bool starts_new_picture = false;
        bool starts_next_au = false;

        if (*codec == SS4S_VIDEO_H264) {
            uint8_t nt = hdr[0] & 0x1F;
            is_vcl = h264_is_vcl(nt);
            is_keyframe = h264_is_keyframe(nt);
            if (is_vcl && h264_starts_new_picture(hdr, hdr_size, &starts_new_picture) != 0) {
                free(nalus);
                return -1;
            }
            starts_next_au = !is_vcl && h264_non_vcl_starts_next_au(nt);
        } else {
            if (!hevc_header_valid(hdr, hdr_size)) {
                free(nalus);
                return -1;
            }
            uint8_t nt = (hdr[0] >> 1) & 0x3F;
            is_vcl = hevc_is_vcl(nt);
            is_keyframe = hevc_is_keyframe(nt);
            if (is_vcl && hevc_starts_new_picture(hdr, hdr_size, &starts_new_picture) != 0) {
                free(nalus);
                return -1;
            }
            starts_next_au = !is_vcl && hevc_non_vcl_starts_next_au(nt);
        }

        if (au_has_vcl && (starts_next_au || (is_vcl && starts_new_picture))) {
            size_t au_size = (size_t)(nalu_data - au_start);
            if (aus_push(aus, au_count, au_cap, au_start, au_size, au_is_keyframe) != 0) {
                free(nalus);
                return -1;
            }
            au_start = nalu_data;
            au_has_vcl = false;
            au_is_keyframe = false;
        }
        if (is_vcl) {
            au_has_vcl = true;
        }
        if (is_keyframe) {
            au_is_keyframe = true;
        }
    }

    if (au_has_vcl) {
        if (eof_seen) {
            size_t au_size = (size_t)((data + size) - au_start);
            if (aus_push(aus, au_count, au_cap, au_start, au_size, au_is_keyframe) != 0) {
                free(nalus);
                return -1;
            }
            *out_carry_offset = size;
        } else {
            *out_carry_offset = (size_t)(au_start - data);
        }
    } else {
        *out_carry_offset = eof_seen ? size : (size_t)(au_start - data);
    }

    free(nalus);
    return 0;
}
