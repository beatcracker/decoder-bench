#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_suite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_tree(char *root, size_t root_size) {
    int written = snprintf(root, root_size, "%s", "/tmp/decoder-bench-suite-XXXXXX");
    assert(written > 0 && (size_t)written < root_size);
    assert(mkdtemp(root) != NULL);

    char path[BENCH_MAX_PATH_LEN];
    written = snprintf(path, sizeof(path), "%s/suites", root);
    assert(written > 0 && (size_t)written < sizeof(path));
    assert(mkdir(path, 0755) == 0);

    written = snprintf(path, sizeof(path), "%s/samples", root);
    assert(written > 0 && (size_t)written < sizeof(path));
    assert(mkdir(path, 0755) == 0);
}

static void suite_path(char *out, size_t out_size, const char *root, const char *leaf) {
    int written = snprintf(out, out_size, "%s/suites/%s", root, leaf);
    assert(written > 0 && (size_t)written < out_size);
}

static void write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(content, f) >= 0);
    assert(fclose(f) == 0);
}

static void cleanup_tree(const char *root, const char *leaf) {
    char path[BENCH_MAX_PATH_LEN];
    suite_path(path, sizeof(path), root, leaf);
    assert(unlink(path) == 0);

    int written = snprintf(path, sizeof(path), "%s/samples", root);
    assert(written > 0 && (size_t)written < sizeof(path));
    assert(rmdir(path) == 0);

    written = snprintf(path, sizeof(path), "%s/suites", root);
    assert(written > 0 && (size_t)written < sizeof(path));
    assert(rmdir(path) == 0);

    assert(rmdir(root) == 0);
}

static void test_title_card_skip_stats_is_allowed(void) {
    char root[BENCH_MAX_PATH_LEN];
    char path[BENCH_MAX_PATH_LEN];
    BenchSuite suite;

    make_tree(root, sizeof(root));
    suite_path(path, sizeof(path), root, "valid.bench");
    write_text_file(path, "[suite]\n"
                          "source_buffer_mib = 128\n"
                          "\n"
                          "[title-card]\n"
                          "file = title.h264\n"
                          "fps = 30\n"
                          "run_seconds = 3\n"
                          "skip_stats = true\n"
                          "\n"
                          "[sample]\n"
                          "file = sample.h264\n"
                          "fps = 60\n"
                          "run_seconds = 3\n");

    assert(bench_suite_load(path, &suite) == 0);
    assert(strcmp(suite.name, "valid") == 0);
    assert(suite.source_buffer_mib == 128u);
    assert(suite.test_count == 2);
    assert(strcmp(suite.tests[0].name, "title-card") == 0);
    assert(suite.tests[0].skip_stats);
    assert(!suite.tests[1].skip_stats);

    cleanup_tree(root, "valid.bench");
}

static void test_skip_stats_is_title_card_only(void) {
    char root[BENCH_MAX_PATH_LEN];
    char path[BENCH_MAX_PATH_LEN];
    BenchSuite suite;

    make_tree(root, sizeof(root));
    suite_path(path, sizeof(path), root, "normal-skip.bench");
    write_text_file(path, "[sample]\n"
                          "file = sample.h264\n"
                          "fps = 60\n"
                          "run_seconds = 3\n"
                          "skip_stats = true\n");

    assert(bench_suite_load(path, &suite) != 0);

    cleanup_tree(root, "normal-skip.bench");
}

static void test_skip_stats_boolean_is_strict(void) {
    char root[BENCH_MAX_PATH_LEN];
    char path[BENCH_MAX_PATH_LEN];
    BenchSuite suite;

    make_tree(root, sizeof(root));
    suite_path(path, sizeof(path), root, "bad-bool.bench");
    write_text_file(path, "[title-card]\n"
                          "file = title.h264\n"
                          "fps = 30\n"
                          "run_seconds = 3\n"
                          "skip_stats = maybe\n");

    assert(bench_suite_load(path, &suite) != 0);

    cleanup_tree(root, "bad-bool.bench");
}

static void test_unknown_key_is_rejected(void) {
    char root[BENCH_MAX_PATH_LEN];
    char path[BENCH_MAX_PATH_LEN];
    BenchSuite suite;

    make_tree(root, sizeof(root));
    suite_path(path, sizeof(path), root, "unknown-key.bench");
    write_text_file(path, "[sample]\n"
                          "file = sample.h264\n"
                          "fps = 60\n"
                          "run_seconds = 3\n"
                          "unexpected = value\n");

    assert(bench_suite_load(path, &suite) != 0);

    cleanup_tree(root, "unknown-key.bench");
}

int main(void) {
    test_title_card_skip_stats_is_allowed();
    test_skip_stats_is_title_card_only();
    test_skip_stats_boolean_is_strict();
    test_unknown_key_is_rejected();
    return 0;
}
