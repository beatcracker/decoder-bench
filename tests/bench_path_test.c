#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_path.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_sanitize_filename_component(void) {
    char out[32];

    assert(bench_path_sanitize_filename_component(out, sizeof(out), "Suite 01/HDR") == 0);
    assert(strcmp(out, "Suite_01_HDR") == 0);

    assert(bench_path_sanitize_filename_component(out, sizeof(out), "A-Z_a.z09") == 0);
    assert(strcmp(out, "A-Z_a.z09") == 0);

    assert(bench_path_sanitize_filename_component(out, sizeof(out), "") != 0);
    assert(bench_path_sanitize_filename_component(out, sizeof(out), ".") != 0);
    assert(bench_path_sanitize_filename_component(out, sizeof(out), "..") != 0);
    assert(bench_path_sanitize_filename_component(out, sizeof(out), NULL) != 0);
}

static void test_sanitize_filename_component_truncation(void) {
    char out[4] = {'x', 'x', 'x', 'x'};

    assert(bench_path_sanitize_filename_component(out, sizeof(out), "abcd") != 0);
    assert(strcmp(out, "abc") == 0);
}

static void test_readlink_checked(void) {
    char dir[] = "/tmp/decoder-bench-path-XXXXXX";
    char link_path[128];
    char out[5];
    char truncated[4] = {'x', 'x', 'x', 'x'};

    assert(mkdtemp(dir) != NULL);
    int written = snprintf(link_path, sizeof(link_path), "%s/link", dir);
    assert(written > 0 && (size_t)written < sizeof(link_path));
    assert(symlink("abc", link_path) == 0);

    assert(bench_path_readlink(out, sizeof(out), link_path) == 0);
    assert(strcmp(out, "abc") == 0);

    assert(bench_path_readlink(truncated, sizeof(truncated), link_path) != 0);
    assert(truncated[0] == '\0');

    assert(bench_path_readlink(out, sizeof(out), "/tmp/decoder-bench-missing-link") != 0);

    assert(unlink(link_path) == 0);
    assert(rmdir(dir) == 0);
}

int main(void) {
    test_sanitize_filename_component();
    test_sanitize_filename_component_truncation();
    test_readlink_checked();
    return 0;
}
