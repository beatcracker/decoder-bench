#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_suite.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>

#include "bench_path.h"
#include "bench_strings.h"
#include "ini.h"

typedef struct parse_ctx {
    BenchSuite *suite;
    bool error;
} parse_ctx_t;

static int parse_uint(const char *value, long min_v, long max_v, long *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || v < min_v || v > max_v) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_bool(const char *value, bool *out) {
    if (value == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0) {
        *out = true;
        return 0;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcmp(value, "no") == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int handle_suite_meta(parse_ctx_t *ctx, const char *name, const char *value) {
    if (strcmp(name, "source_buffer_mib") == 0) {
        long v = 0;
        if (parse_uint(value, (long)BENCH_SOURCE_BUFFER_MIN_MIB, (long)BENCH_SOURCE_BUFFER_MAX_MIB, &v) != 0) {
            fprintf(stderr, "bench_suite: invalid [suite] source_buffer_mib '%s' (allowed %u..%u)\n", value,
                    BENCH_SOURCE_BUFFER_MIN_MIB, BENCH_SOURCE_BUFFER_MAX_MIB);
            ctx->error = true;
            return 0;
        }
        ctx->suite->source_buffer_mib = (unsigned int)v;
        return 1;
    }
    fprintf(stderr, "bench_suite: unknown key '%s' in [suite]\n", name);
    ctx->error = true;
    return 0;
}

static int handle_test_row(parse_ctx_t *ctx, const char *section, const char *name, const char *value) {
    if (strcmp(section, "suite") == 0) {
        /* Defense-in-depth: dispatch above should have routed this elsewhere. */
        fprintf(stderr, "bench_suite: [suite] is reserved; not a test section\n");
        ctx->error = true;
        return 0;
    }

    BenchSuite *suite = ctx->suite;
    BenchTest *test = NULL;
    for (int i = 0; i < suite->test_count; i++) {
        if (strcmp(suite->tests[i].name, section) == 0) {
            test = &suite->tests[i];
            break;
        }
    }
    if (test == NULL) {
        if (suite->test_count >= BENCH_MAX_TESTS) {
            fprintf(stderr, "bench_suite: too many tests (max %d)\n", BENCH_MAX_TESTS);
            ctx->error = true;
            return 0;
        }
        test = &suite->tests[suite->test_count++];
        memset(test, 0, sizeof(*test));
        if ((size_t)snprintf(test->name, sizeof(test->name), "%s", section) >= sizeof(test->name)) {
            fprintf(stderr, "bench_suite: test name too long '%s'\n", section);
            ctx->error = true;
            return 0;
        }
    }

    if (strcmp(name, "file") == 0) {
        if (bench_path_copy(test->file, sizeof(test->file), value) != 0) {
            fprintf(stderr, "bench_suite: file path too long in [%s]\n", section);
            ctx->error = true;
            return 0;
        }
    } else if (strcmp(name, "fps") == 0) {
        long v = 0;
        if (parse_uint(value, 1, BENCH_MAX_FPS, &v) != 0) {
            fprintf(stderr, "bench_suite: invalid fps '%s' in [%s]\n", value, section);
            ctx->error = true;
            return 0;
        }
        test->fps = (int)v;
    } else if (strcmp(name, "run_seconds") == 0) {
        long v = 0;
        if (parse_uint(value, 1, BENCH_MAX_RUN_SECONDS, &v) != 0) {
            fprintf(stderr, "bench_suite: invalid run_seconds '%s' in [%s]\n", value, section);
            ctx->error = true;
            return 0;
        }
        test->run_seconds = (int)v;
    } else if (strcmp(name, "skip_stats") == 0) {
        bool skip_stats = false;
        if (strcmp(section, "title-card") != 0) {
            fprintf(stderr, "bench_suite: skip_stats is only supported in [title-card]\n");
            ctx->error = true;
            return 0;
        }
        if (parse_bool(value, &skip_stats) != 0) {
            fprintf(stderr, "bench_suite: invalid skip_stats '%s' in [%s]\n", value, section);
            ctx->error = true;
            return 0;
        }
        test->skip_stats = skip_stats;
    } else {
        fprintf(stderr, "bench_suite: unknown key '%s' in [%s]\n", name, section);
        ctx->error = true;
        return 0;
    }
    return 1;
}

static int bench_ini_handler(void *user, const char *section, const char *name, const char *value) {
    parse_ctx_t *ctx = user;
    if (section == NULL || section[0] == '\0') {
        fprintf(stderr, "bench_suite: key '%s' outside of a section\n", name);
        ctx->error = true;
        return 0;
    }
    if (strcmp(section, "suite") == 0) {
        return handle_suite_meta(ctx, name, value);
    }
    return handle_test_row(ctx, section, name, value);
}

int bench_suite_load(const char *path, BenchSuite *suite) {
    char *suite_path = NULL;
    int rc = -1;

    if (path == NULL || suite == NULL) {
        return -1;
    }
    memset(suite, 0, sizeof(*suite));

    suite_path = realpath(path, NULL);
    if (suite_path == NULL) {
        fprintf(stderr, "bench_suite: cannot resolve '%s'\n", path);
        goto cleanup;
    }

    const char *base = bench_path_basename(suite_path);
    const char *dot = strrchr(base, '.');
    size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
    if (name_len >= sizeof(suite->name)) {
        fprintf(stderr, "bench_suite: suite name too long in '%s'\n", base);
        goto cleanup;
    }
    memcpy(suite->name, base, name_len);
    suite->name[name_len] = '\0';

    if (bench_path_copy(suite->path, sizeof(suite->path), suite_path) != 0) {
        fprintf(stderr, "bench_suite: suite path too long\n");
        goto cleanup;
    }

    if (!bench_path_parent(suite_path) || !bench_path_parent(suite_path)) {
        if (bench_path_copy(suite->samples_dir, sizeof(suite->samples_dir), BENCH_STRINGS_DIR_SAMPLES) != 0) {
            fprintf(stderr, "bench_suite: samples_dir path too long\n");
            goto cleanup;
        }
    } else if (bench_path_join(suite->samples_dir, sizeof(suite->samples_dir), suite_path, BENCH_STRINGS_DIR_SAMPLES) !=
               0) {
        fprintf(stderr, "bench_suite: samples_dir path too long\n");
        goto cleanup;
    }
    free(suite_path);
    suite_path = NULL;

    parse_ctx_t ctx = {.suite = suite, .error = false};
    int parse_rc = ini_parse(suite->path, bench_ini_handler, &ctx);
    if (parse_rc != 0) {
        if (parse_rc == -1) {
            fprintf(stderr, "bench_suite: cannot open '%s'\n", suite->path);
        } else if (parse_rc == -2) {
            fprintf(stderr, "bench_suite: memory error parsing '%s'\n", suite->path);
        } else {
            fprintf(stderr, "bench_suite: parse error at line %d in '%s'\n", parse_rc, suite->path);
        }
        goto cleanup;
    }
    if (ctx.error) {
        goto cleanup;
    }

    /*
     * Validate test syntax: required keys and that paths join to a string that
     * fits in storage. Fixture existence is NOT checked here — that moves to
     * source open / prefetch so a missing fixture for test N+1 surfaces as a
     * prefetch failure after test N completes.
     */
    for (int i = 0; i < suite->test_count; i++) {
        BenchTest *t = &suite->tests[i];
        if (t->file[0] == '\0') {
            fprintf(stderr, "bench_suite: missing 'file' in [%s]\n", t->name);
            goto cleanup;
        }
        if (t->fps <= 0) {
            fprintf(stderr, "bench_suite: missing or invalid 'fps' in [%s]\n", t->name);
            goto cleanup;
        }
        if (t->run_seconds <= 0) {
            fprintf(stderr, "bench_suite: missing or invalid 'run_seconds' in [%s]\n", t->name);
            goto cleanup;
        }

        char *constructed = bench_path_join_alloc(suite->samples_dir, t->file);
        if (constructed == NULL) {
            fprintf(stderr, "bench_suite: resolved path too long for [%s]\n", t->name);
            goto cleanup;
        }
        if (bench_path_copy(t->file, sizeof(t->file), constructed) != 0) {
            fprintf(stderr, "bench_suite: resolved path too long for [%s]\n", t->name);
            free(constructed);
            goto cleanup;
        }
        free(constructed);
    }

    if (suite->test_count == 0) {
        fprintf(stderr, "bench_suite: no tests found in '%s'\n", suite->path);
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(suite_path);
    if (rc != 0) {
        memset(suite, 0, sizeof(*suite));
    }
    return rc;
}

int bench_suite_discover(const char *bench_root, char names[][BENCH_MAX_NAME_LEN], int max_names) {
    char suites_dir[BENCH_MAX_PATH_LEN];
    if (bench_root == NULL || names == NULL || max_names <= 0) {
        return -1;
    }
    if (bench_path_join(suites_dir, sizeof(suites_dir), bench_root, BENCH_STRINGS_DIR_SUITES) != 0) {
        return -1;
    }

    struct dirent **entries = NULL;
    int entry_count = scandir(suites_dir, &entries, NULL, alphasort);
    if (entry_count < 0) {
        return -1;
    }

    int count = 0;
    const size_t suite_ext_len = strlen(BENCH_STRINGS_SUITE_EXT);
    for (int i = 0; i < entry_count; i++) {
        const char *name = entries[i]->d_name;
        size_t len = strlen(name);
        if (len > suite_ext_len && strcmp(name + len - suite_ext_len, BENCH_STRINGS_SUITE_EXT) == 0 &&
            count < max_names) {
            size_t name_len = len - suite_ext_len;
            if (name_len >= BENCH_MAX_NAME_LEN) {
                fprintf(stderr, "bench_suite: skipping suite with name too long: %s\n", name);
            } else {
                memcpy(names[count], name, name_len);
                names[count][name_len] = '\0';
                count++;
            }
        }
        free(entries[i]);
    }
    free(entries);
    return count;
}
