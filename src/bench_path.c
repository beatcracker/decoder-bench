#include "bench_path.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bench_types.h"

static bool bench_path_dir_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool bench_path_is_sep(char ch) {
    return ch == '/';
}

static bool bench_path_is_filename_char(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
           ch == '.';
}

int bench_path_copy(char *dst, size_t dst_size, const char *src) {
    size_t src_len;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return -1;
    }

    src_len = strlen(src);
    if (src_len >= dst_size) {
        return -1;
    }
    memmove(dst, src, src_len + 1u);

    return 0;
}

int bench_path_join(char *dst, size_t dst_size, const char *left, const char *right) {
    char *joined;
    int rc;

    if (dst == NULL || dst_size == 0 || left == NULL || right == NULL) {
        return -1;
    }

    joined = bench_path_join_alloc(left, right);
    if (joined == NULL) {
        return -1;
    }
    rc = bench_path_copy(dst, dst_size, joined);
    free(joined);

    return rc;
}

int bench_path_dir_ensure(const char *dir) {
    char path[BENCH_MAX_PATH_LEN];
    size_t len;

    if (dir == NULL || bench_path_copy(path, sizeof(path), dir) != 0) {
        return -1;
    }

    len = strlen(path);
    while (len > 1 && bench_path_is_sep(path[len - 1])) {
        path[len - 1] = '\0';
        len--;
    }
    if (path[0] == '\0') {
        return -1;
    }

    for (char *cursor = path + 1; *cursor != '\0'; cursor++) {
        if (!bench_path_is_sep(*cursor)) {
            continue;
        }
        *cursor = '\0';
        if (!bench_path_dir_exists(path) && mkdir(path, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        *cursor = '/';
    }

    if (!bench_path_dir_exists(path) && mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return bench_path_dir_exists(path) ? 0 : -1;
}

int bench_path_sanitize_filename_component(char *dst, size_t dst_size, const char *src) {
    size_t written = 0;
    bool truncated = false;

    if (dst == NULL || dst_size == 0) {
        return -1;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return -1;
    }

    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (written + 1u >= dst_size) {
            truncated = true;
            break;
        }
        dst[written++] = bench_path_is_filename_char(ch) ? (char)ch : '_';
    }
    dst[written] = '\0';

    if (truncated || written == 0 || strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0) {
        return -1;
    }
    return 0;
}

int bench_path_readlink(char *dst, size_t dst_size, const char *link_path) {
    ssize_t len;

    if (dst == NULL || dst_size == 0) {
        return -1;
    }
    dst[0] = '\0';
    if (dst_size < 2 || link_path == NULL) {
        return -1;
    }

    len = readlink(link_path, dst, dst_size - 1u);
    if (len <= 0 || (size_t)len >= dst_size - 1u) {
        dst[0] = '\0';
        return -1;
    }
    dst[len] = '\0';
    return 0;
}

char *bench_path_join_alloc(const char *left, const char *right) {
    size_t left_len;
    size_t right_len;
    const char *right_part = right;
    size_t right_skip = 0;
    bool need_sep = false;
    size_t extra_len;
    size_t total_len;
    char *joined;

    if (left == NULL || right == NULL) {
        return NULL;
    }

    left_len = strlen(left);
    right_len = strlen(right);
    if (left_len > 0 && right_len > 0) {
        if (bench_path_is_sep(left[left_len - 1])) {
            if (bench_path_is_sep(right[0])) {
                right_part = right + 1;
                right_skip = 1;
            }
        } else if (!bench_path_is_sep(right[0])) {
            need_sep = true;
        }
    }

    extra_len = need_sep ? 2u : 1u;
    if (right_len < right_skip || right_len - right_skip > SIZE_MAX - left_len ||
        extra_len > SIZE_MAX - left_len - (right_len - right_skip)) {
        return NULL;
    }
    total_len = left_len + (need_sep ? 1u : 0u) + right_len - right_skip + 1u;

    joined = malloc(total_len);
    if (joined == NULL) {
        return NULL;
    }

    if (need_sep) {
        int written = snprintf(joined, total_len, "%s/%s", left, right_part);
        if (written < 0 || (size_t)written >= total_len) {
            free(joined);
            return NULL;
        }
    } else {
        int written = snprintf(joined, total_len, "%s%s", left, right_part);
        if (written < 0 || (size_t)written >= total_len) {
            free(joined);
            return NULL;
        }
    }

    return joined;
}

bool bench_path_parent(char *path) {
    size_t len;
    size_t end;
    char *sep = NULL;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    len = strlen(path);
    end = len;
    while (end > 1 && bench_path_is_sep(path[end - 1])) {
        end--;
    }
    if (end == 1 && bench_path_is_sep(path[0])) {
        return false;
    }

    for (size_t i = end; i > 0; i--) {
        if (bench_path_is_sep(path[i - 1])) {
            sep = &path[i - 1];
            break;
        }
    }
    if (sep == NULL) {
        return false;
    }
    if (sep == path) {
        sep[1] = '\0';
        return true;
    }

    *sep = '\0';
    return true;
}

const char *bench_path_basename(const char *path) {
    const char *slash;

    if (path == NULL) {
        return NULL;
    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
