#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"

#include "array_list.h"
#include "bench_strings.h"
#include "os_info.h"
#include "ss4s.h"
#include "ss4s_modules.h"

#include "bench_decoder_support.h"
#include "bench_display.h"
#include "bench_feeder.h"
#include "bench_path.h"
#include "bench_platform.h"
#include "bench_stats.h"
#include "bench_suite.h"
#include "bench_types.h"
#include "bench_webos_compat.h"
#ifdef TARGET_WEBOS
#include "bench_usb_webos.h"
#endif

#define BENCH_MAX_PLAN_SUITES 64
#define BENCH_RESULTS_NAME_SEPARATOR "__"
#define BENCH_RESULTS_CSV_EXT ".csv"
#define BENCH_TOAST_WARN_TIMEOUT_SEC 3u
#define BENCH_TOAST_ERROR_TIMEOUT_SEC 5u
#define BENCH_SUITE_FILE_CAPACITY ((BENCH_MAX_NAME_LEN - 1u) + sizeof(BENCH_STRINGS_SUITE_EXT))
#define BENCH_RESULTS_FILE_CAPACITY                                                                                    \
    (((BENCH_MAX_NAME_LEN - 1u) * 2u) + (sizeof(BENCH_RESULTS_NAME_SEPARATOR) - 1u) + sizeof(BENCH_RESULTS_CSV_EXT))

static volatile sig_atomic_t g_interrupted = 0;
static bool g_storage_warmed = false;

typedef struct cli_opts {
    const char *suite_name;
    const char *file_path;
    const char *bench_dir;
    const char *results_dir;
    int fps;
    int run_seconds;
    bool list_suites;
    bool help;
    bool loop;
    unsigned int source_buffer_mib;
} cli_opts_t;

typedef struct test_run_result {
    bool invalid_fixture;
    bool stopped;
    StreamInfo stream;
    BenchSummary summary;
    char fixture[BENCH_MAX_NAME_LEN];
    BenchVerdict verdict;
    BenchTestOutcome test_outcome;
} test_run_result_t;

typedef enum run_status {
    RUN_STATUS_OK = 0,
    RUN_STATUS_STOPPED = 1,
    RUN_STATUS_INVALID = -1,
    RUN_STATUS_PLATFORM_ERROR = -2,
} run_status_t;

typedef enum execution_plan_kind {
    EXECUTION_PLAN_NONE = 0,
    EXECUTION_PLAN_DIRECT_FILE = 1,
    EXECUTION_PLAN_SUITES = 2,
} execution_plan_kind_t;

typedef struct execution_plan {
    execution_plan_kind_t kind;
    bool loop;
    char bench_root[BENCH_MAX_PATH_LEN];
    char suite_names[BENCH_MAX_PLAN_SUITES][BENCH_MAX_NAME_LEN];
    int suite_count;
    char direct_file[BENCH_MAX_PATH_LEN];
    int direct_fps;
    int direct_run_seconds;
    char direct_scope[BENCH_MAX_NAME_LEN];
} execution_plan_t;

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

static void sigint_handler(int sig) {
    (void)sig;
    g_interrupted = 1;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "\n"
           "Options:\n"
           "  --help                Show this help\n"
           "  --list                List discovered suites\n"
           "  --suite NAME          Run one suite\n"
           "  --file PATH           Run one fixture directly\n"
           "  --fps N               Required with --file; must match encoded FPS\n"
           "  --run-seconds N       Explicit workload for --file. Without this,\n"
           "                        --file runs in auto mode (EOF or %d-second cap).\n"
           "  --source-buffer-mib N Per-source buffer size in MiB (range %u..%u, default %u).\n"
           "                        CLI overrides any [suite] source_buffer_mib metadata.\n"
           "  --dir PATH            Explicit bench root directory\n"
           "  --results-dir PATH    Explicit writable results directory\n"
           "  --loop                Rerun the resolved plan until stopped\n"
           "\n"
           "Exit codes:\n"
           "  0  All completed tests PASS, every requested codec is unsupported,\n"
           "     or the run was stopped cleanly\n"
           "  1  One or more completed tests WARN, none FAIL\n"
           "  2  One or more completed tests FAIL\n"
           "  3  Infrastructure, configuration, fixture, or CLI error\n",
           prog, BENCH_AUTO_CAP_SECONDS, BENCH_SOURCE_BUFFER_MIN_MIB, BENCH_SOURCE_BUFFER_MAX_MIB,
           BENCH_SOURCE_BUFFER_DEFAULT_MIB);
}

static int parse_cli(int argc, char *argv[], cli_opts_t *opts) {
    memset(opts, 0, sizeof(*opts));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], BENCH_STRINGS_CLI_HELP) == 0 || strcmp(argv[i], BENCH_STRINGS_CLI_HELP_SHORT) == 0) {
            opts->help = true;
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_LIST) == 0) {
            opts->list_suites = true;
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_SUITE) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --suite requires a value\n");
                return -1;
            }
            opts->suite_name = argv[i];
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_FILE) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --file requires a value\n");
                return -1;
            }
            opts->file_path = argv[i];
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_FPS) == 0) {
            char *end = NULL;
            long value;
            if (++i >= argc) {
                fprintf(stderr, "error: --fps requires a value\n");
                return -1;
            }
            value = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || value <= 0 || value > BENCH_MAX_FPS) {
                fprintf(stderr, "error: invalid --fps '%s'\n", argv[i]);
                return -1;
            }
            opts->fps = (int)value;
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_RUN_SECONDS) == 0) {
            char *end = NULL;
            long value;
            if (++i >= argc) {
                fprintf(stderr, "error: --run-seconds requires a value\n");
                return -1;
            }
            value = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || value <= 0 || value > BENCH_MAX_RUN_SECONDS) {
                fprintf(stderr, "error: invalid --run-seconds '%s'\n", argv[i]);
                return -1;
            }
            opts->run_seconds = (int)value;
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_SOURCE_BUFFER_MIB) == 0) {
            char *end = NULL;
            long value;
            if (++i >= argc) {
                fprintf(stderr, "error: --source-buffer-mib requires a value\n");
                return -1;
            }
            value = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || value < (long)BENCH_SOURCE_BUFFER_MIN_MIB ||
                value > (long)BENCH_SOURCE_BUFFER_MAX_MIB) {
                fprintf(stderr, "error: invalid --source-buffer-mib '%s' (allowed %u..%u)\n", argv[i],
                        BENCH_SOURCE_BUFFER_MIN_MIB, BENCH_SOURCE_BUFFER_MAX_MIB);
                return -1;
            }
            opts->source_buffer_mib = (unsigned int)value;
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_DIR) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --dir requires a value\n");
                return -1;
            }
            opts->bench_dir = argv[i];
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_RESULTS_DIR) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --results-dir requires a value\n");
                return -1;
            }
            opts->results_dir = argv[i];
        } else if (strcmp(argv[i], BENCH_STRINGS_CLI_LOOP) == 0) {
            opts->loop = true;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    if (opts->list_suites && (opts->suite_name != NULL || opts->file_path != NULL || opts->loop)) {
        fprintf(stderr, "error: --list cannot be combined with --suite, --file, or --loop\n");
        return -1;
    }
    if (opts->suite_name != NULL && opts->file_path != NULL) {
        fprintf(stderr, "error: choose either --suite or --file, not both\n");
        return -1;
    }
    if (opts->file_path != NULL && opts->fps <= 0) {
        fprintf(stderr, "error: --fps is required with --file\n");
        return -1;
    }

    return 0;
}

static bool dir_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool bench_root_valid(const char *path) {
    char suites_dir[BENCH_MAX_PATH_LEN];
    if (path == NULL || bench_path_join(suites_dir, sizeof(suites_dir), path, BENCH_STRINGS_DIR_SUITES) != 0) {
        return false;
    }
    return dir_exists(suites_dir);
}

static bool executable_bench_dir(const char *argv0, char *out, size_t out_size) {
    char exe_path[BENCH_MAX_PATH_LEN];
    if (bench_path_readlink(exe_path, sizeof(exe_path), BENCH_STRINGS_PROC_SELF_EXE) != 0) {
        if (argv0 == NULL || strchr(argv0, '/') == NULL) {
            return false;
        }
        if (bench_path_copy(exe_path, sizeof(exe_path), argv0) != 0) {
            return false;
        }
    }

    char *resolved = realpath(exe_path, NULL);
    if (resolved != NULL) {
        if (bench_path_copy(exe_path, sizeof(exe_path), resolved) != 0) {
            free(resolved);
            return false;
        }
        free(resolved);
    }

    if (!bench_path_parent(exe_path)) {
        return false;
    }
    if (bench_path_join(out, out_size, exe_path, BENCH_STRINGS_DIR_BENCH) != 0) {
        return false;
    }
    return bench_root_valid(out);
}

static bool scan_dir_for_bench(const char *dir, int depth, char *out, size_t out_size) {
    char candidate[BENCH_MAX_PATH_LEN];
    if (bench_path_join(candidate, sizeof(candidate), dir, BENCH_STRINGS_DIR_BENCH) != 0) {
        return false;
    }
    if (bench_root_valid(candidate)) {
        fprintf(stderr, "scan: found bench root at %s\n", candidate);
        return bench_path_copy(out, out_size, candidate) == 0;
    }
    if (depth <= 0) {
        return false;
    }

    struct dirent **entries = NULL;
    int entry_count = scandir(dir, &entries, NULL, alphasort);
    if (entry_count < 0) {
        return false;
    }

    bool found = false;
    for (int j = 0; j < entry_count; j++) {
        struct dirent *entry = entries[j];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[BENCH_MAX_PATH_LEN];
        if (bench_path_join(child, sizeof(child), dir, entry->d_name) != 0) {
            continue;
        }
        if (scan_dir_for_bench(child, depth - 1, out, out_size)) {
            found = true;
            break;
        }
    }
    for (int j = 0; j < entry_count; j++) {
        free(entries[j]);
    }
    free(entries);
    return found;
}

static bool scan_mounts_for_bench(char *out, size_t out_size) {
    static const struct {
        const char *path;
        int depth;
    } roots[] = {
        {BENCH_STRINGS_WEBOS_USB_ROOT, 2},
        {BENCH_STRINGS_WEBOS_PLATFORM_USB_ROOT, 2},
        {"/media/developer", 1},
        {"/media", 1},
        {"/run/media", 1},
        {"/mnt", 1},
    };

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        fprintf(stderr, "scan: trying mount root %s (depth %d)\n", roots[i].path, roots[i].depth);
        if (scan_dir_for_bench(roots[i].path, roots[i].depth, out, out_size)) {
            return true;
        }
    }

    fprintf(stderr, "scan: no bench root found on any mount\n");
    return false;
}

static int resolve_bench_dir(const cli_opts_t *opts, const char *argv0, char *out, size_t out_size) {
    const char *env = getenv(BENCH_STRINGS_ENV_BENCH_PATH);

    if (opts != NULL && opts->bench_dir != NULL) {
        fprintf(stderr, "resolve: using --dir %s\n", opts->bench_dir);
        return bench_path_copy(out, out_size, opts->bench_dir);
    }
    if (env != NULL && env[0] != '\0') {
        fprintf(stderr, "resolve: using BENCH_PATH=%s\n", env);
        return bench_path_copy(out, out_size, env);
    }
    if (executable_bench_dir(argv0, out, out_size)) {
        fprintf(stderr, "resolve: using executable-relative %s\n", out);
        return 0;
    }
    if (bench_root_valid(BENCH_STRINGS_DIR_BENCH)) {
        fprintf(stderr, "resolve: using cwd-relative bench/\n");
        return bench_path_copy(out, out_size, BENCH_STRINGS_DIR_BENCH);
    }
    if (scan_mounts_for_bench(out, out_size)) {
        fprintf(stderr, "resolve: using mount-scanned %s\n", out);
        return 0;
    }

    fprintf(stderr, "resolve: no bench root found, falling back to bench/\n");
    return bench_path_copy(out, out_size, BENCH_STRINGS_DIR_BENCH);
}

static int resolve_bundled_bench_dir(const char *argv0, char *out, size_t out_size) {
    return executable_bench_dir(argv0, out, out_size) ? 0 : -1;
}

#ifdef TARGET_WEBOS
static bool path_is_under_webos_usb_root(const char *path) {
    const char prefix[] = BENCH_STRINGS_WEBOS_USB_ROOT "/";
    size_t prefix_len = sizeof(prefix) - 1u;

    return path != NULL && strncmp(path, prefix, prefix_len) == 0;
}

static int build_utc_run_stamp(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_buf;

    if (out == NULL || out_size == 0 || gmtime_r(&now, &tm_buf) == NULL) {
        return -1;
    }
    return strftime(out, out_size, "%Y%m%dT%H%M%SZ", &tm_buf) > 0 ? 0 : -1;
}

static int verify_results_root_writable(const char *dir) {
    char test_path[BENCH_MAX_PATH_LEN];
    FILE *f;

    if (bench_path_join(test_path, sizeof(test_path), dir, BENCH_STRINGS_WRITE_TEST_FILE) != 0) {
        return -1;
    }

    f = fopen(test_path, "w");
    if (f == NULL) {
        return -1;
    }
    if (fputs("decoder-bench results write test\n", f) < 0) {
        (void)fclose(f);
        (void)remove(test_path);
        return -1;
    }
    if (fclose(f) != 0) {
        (void)remove(test_path);
        return -1;
    }
    return remove(test_path) == 0 ? 0 : -1;
}

static int create_timestamped_results_root(const char *base_dir, char *out, size_t out_size) {
    char stamp[32];

    if (base_dir == NULL || out == NULL || out_size == 0 || build_utc_run_stamp(stamp, sizeof(stamp)) != 0 ||
        bench_path_dir_ensure(base_dir) != 0) {
        return -1;
    }

    for (unsigned int attempt = 0; attempt < 100u; attempt++) {
        char leaf[48];
        char candidate[BENCH_MAX_PATH_LEN];

        if (attempt == 0) {
            if ((size_t)snprintf(leaf, sizeof(leaf), "%s", stamp) >= sizeof(leaf)) {
                return -1;
            }
        } else if ((size_t)snprintf(leaf, sizeof(leaf), "%s-%02u", stamp, attempt) >= sizeof(leaf)) {
            return -1;
        }

        if (bench_path_join(candidate, sizeof(candidate), base_dir, leaf) != 0) {
            return -1;
        }
        if (mkdir(candidate, 0755) != 0) {
            if (errno == EEXIST) {
                continue;
            }
            return -1;
        }
        if (verify_results_root_writable(candidate) != 0) {
            return -1;
        }
        return bench_path_copy(out, out_size, candidate);
    }

    return -1;
}

static int resolve_webos_usb_results_root(const execution_plan_t *plan, char *out, size_t out_size) {
    char partition_root[BENCH_MAX_PATH_LEN];
    char results_base[BENCH_MAX_PATH_LEN];

    if (plan == NULL || plan->kind != EXECUTION_PLAN_SUITES || !path_is_under_webos_usb_root(plan->bench_root) ||
        strcmp(bench_path_basename(plan->bench_root), BENCH_STRINGS_DIR_BENCH) != 0) {
        return -1;
    }
    if (bench_path_copy(partition_root, sizeof(partition_root), plan->bench_root) != 0 ||
        !bench_path_parent(partition_root) ||
        bench_path_join(results_base, sizeof(results_base), partition_root, BENCH_STRINGS_DIR_USB_RESULTS) != 0) {
        return -1;
    }

    return create_timestamped_results_root(results_base, out, out_size);
}

static int resolve_results_root_for_plan(const BenchLaunchContext *launch_ctx, const cli_opts_t *opts,
                                         const execution_plan_t *plan, char *out, size_t out_size) {
    if (opts == NULL) {
        return -1;
    }

    if (opts->results_dir == NULL && launch_ctx != NULL && launch_ctx->autorun_no_args &&
        resolve_webos_usb_results_root(plan, out, out_size) == 0) {
        return 0;
    }

    if (opts->results_dir == NULL && launch_ctx != NULL && launch_ctx->autorun_no_args && plan != NULL &&
        path_is_under_webos_usb_root(plan->bench_root)) {
        fprintf(stderr, "Warning: failed to create USB results directory beside %s; falling back to local temp\n",
                plan->bench_root);
        bench_platform_toast("USB results unavailable - using local temp", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
    }

    return bench_platform_resolve_results_root(opts->results_dir, out, out_size);
}
#endif

static int build_suite_path(const char *bench_root, const char *suite_name, char *out, size_t out_size) {
    char suites_dir[BENCH_MAX_PATH_LEN];
    char suite_file[BENCH_SUITE_FILE_CAPACITY];

    if (bench_root == NULL || suite_name == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    if ((size_t)snprintf(suite_file, sizeof(suite_file), "%s%s", suite_name, BENCH_STRINGS_SUITE_EXT) >=
            sizeof(suite_file) ||
        bench_path_join(suites_dir, sizeof(suites_dir), bench_root, BENCH_STRINGS_DIR_SUITES) != 0 ||
        bench_path_join(out, out_size, suites_dir, suite_file) != 0) {
        return -1;
    }
    return 0;
}

static int build_scoped_results_path(const char *results_root, const char *scope_name, const char *leaf_name, char *out,
                                     size_t out_size) {
    char scope_safe[BENCH_MAX_NAME_LEN];
    char leaf_safe[BENCH_MAX_NAME_LEN];
    char file_name[BENCH_RESULTS_FILE_CAPACITY];

    if (results_root == NULL || scope_name == NULL || leaf_name == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    if (bench_path_sanitize_filename_component(scope_safe, sizeof(scope_safe), scope_name) != 0 ||
        bench_path_sanitize_filename_component(leaf_safe, sizeof(leaf_safe), leaf_name) != 0) {
        return -1;
    }
    if ((size_t)snprintf(file_name, sizeof(file_name), "%s%s%s%s", scope_safe, BENCH_RESULTS_NAME_SEPARATOR, leaf_safe,
                         BENCH_RESULTS_CSV_EXT) >= sizeof(file_name)) {
        return -1;
    }
    return bench_path_join(out, out_size, results_root, file_name);
}

static void print_run_options(const cli_opts_t *opts, bool loop_enabled) {
    unsigned int mib = opts->source_buffer_mib != 0 ? opts->source_buffer_mib : BENCH_SOURCE_BUFFER_DEFAULT_MIB;
    printf("Loop mode: %s\n", loop_enabled ? "enabled" : "disabled");
    printf("Source buffer (CLI default): %u MiB%s\n", mib,
           opts->source_buffer_mib == 0 ? " (built-in default; suite may override)" : " (CLI override)");
}

static void fill_summary_row(BenchSummaryRow *row, const char *test_name, const test_run_result_t *result) {
    if (row == NULL || result == NULL) {
        return;
    }

    memset(row, 0, sizeof(*row));
    (void)snprintf(row->test_name, sizeof(row->test_name), "%s", test_name != NULL ? test_name : "");
    (void)snprintf(row->fixture, sizeof(row->fixture), "%s", result->fixture);
    row->info = result->stream;
    row->summary = result->summary;
    row->test_outcome = result->test_outcome;
}

static void maybe_write_summary_csv(const char *results_root, const char *scope_name, const BenchSummaryRow *rows,
                                    int row_count) {
    char summary_path[BENCH_MAX_PATH_LEN];

    if (results_root == NULL || scope_name == NULL || rows == NULL || row_count <= 0) {
        return;
    }
    if (build_scoped_results_path(results_root, scope_name, "summary", summary_path, sizeof(summary_path)) != 0) {
        fprintf(stderr, "Warning: failed to resolve summary CSV path for scope '%s'\n", scope_name);
        return;
    }
    if (bench_stats_write_summary_csv(summary_path, scope_name, rows, row_count) != 0) {
        fprintf(stderr, "Warning: failed to write summary CSV to %s\n", summary_path);
        return;
    }
    printf("  Summary CSV written: %s\n", summary_path);
}

static unsigned int effective_source_buffer_mib(const cli_opts_t *opts, const BenchSuite *suite) {
    if (opts != NULL && opts->source_buffer_mib != 0) {
        return opts->source_buffer_mib;
    }
    if (suite != NULL && suite->source_buffer_mib != 0) {
        return suite->source_buffer_mib;
    }
    return BENCH_SOURCE_BUFFER_DEFAULT_MIB;
}

static BenchSourceOutcome open_source_for_test(const char *fixture_path, int fps, int run_seconds,
                                               unsigned int source_buffer_mib, BenchSource **out_source) {
    BenchSourceOptions options = {0};
    options.source_buffer_mib = source_buffer_mib;
    options.target_frames = (fps > 0 && run_seconds > 0) ? fps * run_seconds : 0;
    return bench_source_open(fixture_path, &options, &g_interrupted, out_source);
}

static BenchSourceOutcome prefetch_source_for_test(const char *fixture_path, unsigned int source_buffer_mib, int fps,
                                                   int run_seconds, BenchSourcePrefetch **out_prefetch) {
    BenchSourceOptions options = {0};
    options.source_buffer_mib = source_buffer_mib;
    options.target_frames = (fps > 0 && run_seconds > 0) ? fps * run_seconds : 0;
    return bench_source_prefetch_start(fixture_path, &options, &g_interrupted, out_prefetch);
}

/* ---------- Single test runner ---------- */

static void set_early_failure(const BenchSummaryInputs *inputs, BenchStopReason stop_reason,
                              test_run_result_t *result) {
    if (inputs == NULL || result == NULL) {
        return;
    }
    bench_stats_init_empty(inputs, &result->summary);
    result->summary.stop_reason = stop_reason;
    result->summary.verdict = BENCH_VERDICT_FAIL;
    result->verdict = BENCH_VERDICT_FAIL;
}

static run_status_t run_single_test_with_source(const char *scope_name, const char *test_name, const char *fixture_path,
                                                BenchSource *source, int fps, int run_seconds, bool auto_mode,
                                                const char *results_root,
                                                const SS4S_VideoCapabilities *video_capabilities,
                                                bool require_decoder_latency, BenchDisplay *display,
                                                test_run_result_t *out_result) {
    SS4S_Player *player = NULL;
    bool video_open = false;
    FrameRecord *records = NULL;
    int max_records = 0;
    int records_written = 0;
    int feed_rc = -1;
    BenchStopReason stop_reason = BENCH_STOP_NONE;
    BenchSourceError source_error = BENCH_SOURCE_ERROR_NONE;
    run_status_t status = RUN_STATUS_INVALID;
    int viewport_width = 0;
    int viewport_height = 0;

    if (out_result == NULL || test_name == NULL || fixture_path == NULL || source == NULL || display == NULL ||
        bench_display_get_viewport(display, &viewport_width, &viewport_height) != 0) {
        return RUN_STATUS_INVALID;
    }

    memset(out_result, 0, sizeof(*out_result));
    out_result->verdict = BENCH_VERDICT_PASS;
    out_result->test_outcome = BENCH_TEST_COMPLETED;

    if (g_interrupted != 0) {
        out_result->stopped = true;
        return RUN_STATUS_STOPPED;
    }

    printf("\n--- %s ---\n", test_name);

    SS4S_VideoCodec codec = bench_source_codec(source);
    int width = 0;
    int height = 0;
    bench_source_dimensions(source, &width, &height);
    BenchSourceMode mode = bench_source_mode(source);
    unsigned int buf_mib = bench_source_buffer_mib(source);

    int effective_run_seconds = run_seconds;
    if (auto_mode) {
        effective_run_seconds = BENCH_AUTO_CAP_SECONDS;
    }

    out_result->stream.codec = codec;
    out_result->stream.width = width;
    out_result->stream.height = height;
    out_result->stream.fps = fps;
    out_result->stream.run_seconds = effective_run_seconds;
    (void)snprintf(out_result->fixture, sizeof(out_result->fixture), "%s", bench_path_basename(fixture_path));

    printf("  codec: %s\n", codec == SS4S_VIDEO_H264 ? "H264" : "HEVC");
    printf("  resolution: %dx%d\n", width, height);
    if (mode == BENCH_SOURCE_MODE_STREAMING) {
        printf("  source: %s (2 x %u MiB buffers + <=%u MiB carry; suite prefetch may add one first-fill buffer)\n",
               bench_source_mode_str(mode), buf_mib, BENCH_MAX_SINGLE_AU_BYTES / (1024u * 1024u));
    } else {
        printf("  source: %s (1 x %u MiB buffer + <=%u MiB carry; suite prefetch may add one first-fill buffer)\n",
               bench_source_mode_str(mode), buf_mib, BENCH_MAX_SINGLE_AU_BYTES / (1024u * 1024u));
    }
    printf("  run length: %s (%d frames at %d fps over %d seconds)\n", auto_mode ? "auto" : "explicit",
           fps * effective_run_seconds, fps, effective_run_seconds);

    int64_t target_frames_64 = (int64_t)fps * (int64_t)effective_run_seconds;
    if (target_frames_64 <= 0 || target_frames_64 > INT_MAX) {
        printf("  INVALID: requested frame count is not representable\n");
        out_result->invalid_fixture = true;
        goto cleanup;
    }
    max_records = (int)target_frames_64;

    BenchSummaryInputs inputs = {
        .run_length_mode = auto_mode ? BENCH_RUN_LENGTH_AUTO : BENCH_RUN_LENGTH_EXPLICIT,
        .target_frames = (int)target_frames_64,
        .duration_sec = effective_run_seconds,
        .stop_reason = BENCH_STOP_NONE,
        .source_error = BENCH_SOURCE_ERROR_NONE,
        .source_mode = mode,
        .source_buffer_mib = buf_mib,
    };

    if (video_capabilities != NULL && !bench_decoder_supports_codec(video_capabilities, codec)) {
        printf("  UNSUPPORTED: selected decoder does not advertise this codec\n");
        out_result->test_outcome = BENCH_TEST_UNSUPPORTED;
        bench_stats_init_empty(&inputs, &out_result->summary);
        status = RUN_STATUS_OK;
        goto cleanup;
    }

    player = SS4S_PlayerOpen();
    if (player == NULL) {
        printf("  FAIL: SS4S_PlayerOpen failed\n");
        set_early_failure(&inputs, BENCH_STOP_DECODER_FAIL, out_result);
        status = RUN_STATUS_OK;
        goto cleanup;
    }
    SS4S_PlayerSetViewportSize(player, viewport_width, viewport_height);

    SS4S_VideoInfo vinfo = {
        .codec = codec,
        .width = width,
        .height = height,
        .frameRateNumerator = fps,
        .frameRateDenominator = 1,
    };
    SS4S_VideoOpenResult video_open_result = SS4S_PlayerVideoOpen(player, &vinfo);
    if (bench_decoder_open_is_unsupported(video_open_result)) {
        printf("  UNSUPPORTED: selected decoder explicitly rejected this codec\n");
        out_result->test_outcome = BENCH_TEST_UNSUPPORTED;
        bench_stats_init_empty(&inputs, &out_result->summary);
        status = RUN_STATUS_OK;
        goto cleanup;
    }
    if (video_open_result != SS4S_VIDEO_OPEN_OK) {
        printf("  FAIL: SS4S_PlayerVideoOpen failed (%d)\n", (int)video_open_result);
        set_early_failure(&inputs, BENCH_STOP_DECODER_FAIL, out_result);
        status = RUN_STATUS_OK;
        goto cleanup;
    }
    video_open = true;

    records = calloc((size_t)max_records, sizeof(FrameRecord));
    if (records == NULL) {
        printf("  FAIL: cannot allocate frame records\n");
        set_early_failure(&inputs, BENCH_STOP_NONE, out_result);
        status = RUN_STATUS_OK;
        goto cleanup;
    }

    int feed_target_frames = auto_mode ? 0 : (int)target_frames_64;
    feed_rc = bench_feed_loop(player, source, fps, feed_target_frames, records, max_records, &records_written,
                              bench_display_service, display, &g_interrupted, &stop_reason, &source_error);

    SS4S_PlayerVideoClose(player);
    SS4S_PlayerClose(player);
    video_open = false;
    player = NULL;

    if (feed_rc != 0) {
        fprintf(stderr, "  presentation service failed; aborting benchmark\n");
        status = RUN_STATUS_PLATFORM_ERROR;
        goto cleanup;
    }

    if (stop_reason == BENCH_STOP_USER) {
        printf("  stopped by user\n");
        out_result->stopped = true;
        status = RUN_STATUS_STOPPED;
        goto cleanup;
    }

    inputs.stop_reason = stop_reason;
    inputs.source_error = source_error;
    bench_stats_compute(records, records_written, fps, &inputs, require_decoder_latency, &out_result->summary);
    out_result->verdict = out_result->summary.verdict;

    if (source_error == BENCH_SOURCE_ERROR_INVALID_FIXTURE) {
        out_result->invalid_fixture = true;
    }

    if (results_root != NULL) {
        char raw_csv_path[BENCH_MAX_PATH_LEN];
        if (build_scoped_results_path(results_root, scope_name, test_name, raw_csv_path, sizeof(raw_csv_path)) != 0) {
            fprintf(stderr, "Warning: failed to resolve raw CSV path for scope '%s' test '%s'\n", scope_name,
                    test_name);
        } else if (bench_stats_write_csv(raw_csv_path, test_name, &out_result->stream, out_result->fixture, records,
                                         records_written) != 0) {
            fprintf(stderr, "Warning: failed to write raw CSV to %s\n", raw_csv_path);
        } else {
            printf("  Raw CSV written: %s\n", raw_csv_path);
        }
    }

    bench_stats_print_summary(test_name, &out_result->stream, &out_result->summary);
    status = RUN_STATUS_OK;

    if (source_error == BENCH_SOURCE_ERROR_STORAGE_UNDERFLOW) {
        printf("FAIL: storage underflow; source buffer emptied before the next chunk was ready.\n"
               "Try --source-buffer-mib %u. If this repeats at larger buffers, the storage path\n"
               "cannot sustain this fixture.\n",
               buf_mib < BENCH_SOURCE_BUFFER_MAX_MIB / 2 ? buf_mib * 2u : BENCH_SOURCE_BUFFER_MAX_MIB);
        bench_platform_toast("Storage underflow. Try a larger source buffer or faster USB storage.", false,
                             BENCH_TOAST_ERROR_TIMEOUT_SEC);
    }
    if (source_error == BENCH_SOURCE_ERROR_INVALID_FIXTURE) {
        bench_platform_toast("Invalid fixture; suite aborted.", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
    }

cleanup:
    if (video_open && player != NULL) {
        SS4S_PlayerVideoClose(player);
    }
    if (player != NULL) {
        SS4S_PlayerClose(player);
    }
    free(records);
    return status;
}

/* ---------- Suite runner ---------- */

static int storage_warmup_if_needed(const BenchSuite *suite) {
    if (g_storage_warmed) {
        return 0;
    }
    if (suite->test_count == 0) {
        return 0;
    }
    const char *fixture = suite->tests[0].file;
    int64_t start = monotonic_ms();
    int rc = bench_storage_warmup(fixture, 0, 0);
    int64_t elapsed = monotonic_ms() - start;
    if (rc == 0) {
        printf("warming storage %s... done (%lldms)\n", bench_path_basename(fixture), (long long)elapsed);
        g_storage_warmed = true;
        return 0;
    }
    fprintf(stderr,
            "FAIL: storage warmup did not complete within %d ms.\n"
            "Storage device is not responding fast enough to run any benchmark.\n",
            BENCH_WARMUP_MAX_MS_DEFAULT);
    bench_platform_toast("Storage warmup failed. Try a different USB port or stick.", false,
                         BENCH_TOAST_ERROR_TIMEOUT_SEC);
    return -1;
}

static run_status_t run_direct_once(const execution_plan_t *plan, const cli_opts_t *opts, const char *results_root,
                                    const SS4S_VideoCapabilities *video_capabilities, bool require_decoder_latency,
                                    BenchDisplay *display, BenchVerdict *worst, int *completed_tests,
                                    int *unsupported_tests) {
    test_run_result_t result;
    BenchSummaryRow row;
    run_status_t status;

    if (plan == NULL || opts == NULL || worst == NULL || completed_tests == NULL || unsupported_tests == NULL) {
        return RUN_STATUS_INVALID;
    }

    unsigned int buf_mib = effective_source_buffer_mib(opts, NULL);
    bool auto_mode = plan->direct_run_seconds <= 0;
    BenchSource *src = NULL;
    BenchSourceOutcome open_rc =
        open_source_for_test(plan->direct_file, plan->direct_fps, plan->direct_run_seconds, buf_mib, &src);
    if (open_rc != BENCH_SOURCE_OK) {
        fprintf(stderr, "Failed to open source for %s (source outcome %d)\n", plan->direct_file, (int)open_rc);
        return RUN_STATUS_INVALID;
    }

    status = run_single_test_with_source(plan->direct_scope, plan->direct_scope, plan->direct_file, src,
                                         plan->direct_fps, plan->direct_run_seconds, auto_mode, results_root,
                                         video_capabilities, require_decoder_latency, display, &result);
    bench_source_close(src);
    if (status != RUN_STATUS_OK) {
        return status;
    }

    fill_summary_row(&row, plan->direct_scope, &result);
    bench_stats_aggregate_outcome(result.test_outcome, result.verdict, worst, completed_tests, unsupported_tests);
    maybe_write_summary_csv(results_root, plan->direct_scope, &row, 1);
    return RUN_STATUS_OK;
}

static run_status_t run_suite_once(const char *bench_root, const char *suite_name, const cli_opts_t *opts,
                                   const char *results_root, const SS4S_VideoCapabilities *video_capabilities,
                                   bool require_decoder_latency, BenchDisplay *display, BenchVerdict *worst,
                                   int *completed_tests, int *unsupported_tests) {
    BenchSuite suite;
    BenchSummaryRow rows[BENCH_MAX_TESTS];
    int row_count = 0;
    char suite_path[BENCH_MAX_PATH_LEN];
    BenchSourcePrefetch *prefetch = NULL;
    bool suite_aborted = false;
    bool prefetch_scheduling_failed = false;
    run_status_t result_status = RUN_STATUS_OK;

    if (bench_root == NULL || suite_name == NULL || opts == NULL || worst == NULL || completed_tests == NULL ||
        unsupported_tests == NULL) {
        return RUN_STATUS_INVALID;
    }
    if (build_suite_path(bench_root, suite_name, suite_path, sizeof(suite_path)) != 0 ||
        bench_suite_load(suite_path, &suite) != 0) {
        fprintf(stderr, "Failed to load suite '%s'\n", suite_name);
        return RUN_STATUS_INVALID;
    }

    printf("Suite: %s (%d tests)\n", suite.name, suite.test_count);

    if (storage_warmup_if_needed(&suite) != 0) {
        return RUN_STATUS_INVALID;
    }

    unsigned int buf_mib = effective_source_buffer_mib(opts, &suite);

    BenchSource *current_source = NULL;

    for (int i = 0; i < suite.test_count; i++) {
        if (g_interrupted != 0) {
            result_status = RUN_STATUS_STOPPED;
            break;
        }

        BenchSourceOutcome open_rc = BENCH_SOURCE_OK;
        if (prefetch != NULL) {
            open_rc = bench_source_prefetch_join(prefetch, &current_source);
            prefetch = NULL;
        } else {
            open_rc = open_source_for_test(suite.tests[i].file, suite.tests[i].fps, suite.tests[i].run_seconds, buf_mib,
                                           &current_source);
        }
        if (open_rc != BENCH_SOURCE_OK) {
            fprintf(stderr, "\nSuite '%s': failed to open source for test '%s' (%s); aborting suite.\n", suite.name,
                    suite.tests[i].name, bench_source_error_str(bench_source_outcome_to_error(open_rc)));
            if (current_source != NULL) {
                bench_source_close(current_source);
                current_source = NULL;
            }
            bench_platform_toast("Suite aborted: failed to open fixture.", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
            suite_aborted = true;
            break;
        }

        if (i + 1 < suite.test_count) {
            if (prefetch_source_for_test(suite.tests[i + 1].file, buf_mib, suite.tests[i + 1].fps,
                                         suite.tests[i + 1].run_seconds, &prefetch) != BENCH_SOURCE_OK) {
                prefetch_scheduling_failed = true;
            }
        }

        test_run_result_t result;
        run_status_t status =
            run_single_test_with_source(suite.name, suite.tests[i].name, suite.tests[i].file, current_source,
                                        suite.tests[i].fps, suite.tests[i].run_seconds, false, results_root,
                                        video_capabilities, require_decoder_latency, display, &result);
        bench_source_close(current_source);
        current_source = NULL;

        if (status != RUN_STATUS_OK) {
            result_status = status;
            break;
        }

        if (!suite.tests[i].skip_stats) {
            fill_summary_row(&rows[row_count++], suite.tests[i].name, &result);
            bench_stats_aggregate_outcome(result.test_outcome, result.verdict, worst, completed_tests,
                                          unsupported_tests);
        }
        if (prefetch_scheduling_failed) {
            fprintf(stderr, "\nSuite '%s': could not schedule prefetch for test '%s'; aborting suite.\n", suite.name,
                    suite.tests[i + 1].name);
            bench_platform_toast("Suite aborted: prefetch failed.", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
            suite_aborted = true;
            break;
        }
    }

    /* Drain any prefetch slot that survived the loop. */
    if (prefetch != NULL) {
        bench_source_prefetch_abandon(prefetch);
    }
    if (current_source != NULL) {
        bench_source_close(current_source);
    }

    maybe_write_summary_csv(results_root, suite.name, rows, row_count);

    if (suite_aborted) {
        return RUN_STATUS_INVALID;
    }
    return result_status;
}

static int build_execution_plan(const BenchLaunchContext *launch_ctx, const cli_opts_t *opts, const char *argv0,
                                execution_plan_t *plan) {
    if (opts == NULL || plan == NULL) {
        return -1;
    }

    memset(plan, 0, sizeof(*plan));

    if (opts->file_path != NULL) {
        plan->kind = EXECUTION_PLAN_DIRECT_FILE;
        plan->loop = opts->loop;
        plan->direct_fps = opts->fps;
        plan->direct_run_seconds = opts->run_seconds;
        if (bench_path_copy(plan->direct_file, sizeof(plan->direct_file), opts->file_path) != 0 ||
            (size_t)snprintf(plan->direct_scope, sizeof(plan->direct_scope), "%s", BENCH_STRINGS_DIRECT_SCOPE) >=
                sizeof(plan->direct_scope)) {
            return -1;
        }
        return 0;
    }

    if (opts->suite_name != NULL) {
        plan->kind = EXECUTION_PLAN_SUITES;
        plan->loop = opts->loop;
        plan->suite_count = 1;
        if (resolve_bench_dir(opts, argv0, plan->bench_root, sizeof(plan->bench_root)) != 0 ||
            (size_t)snprintf(plan->suite_names[0], sizeof(plan->suite_names[0]), "%s", opts->suite_name) >=
                sizeof(plan->suite_names[0])) {
            return -1;
        }
        return 0;
    }

    if (launch_ctx != NULL && launch_ctx->autorun_no_args) {
#ifdef TARGET_WEBOS
        bench_usb_prepare_access();
#endif
        fprintf(stderr, "autorun: scanning mounts for external suites\n");
        char media_root[BENCH_MAX_PATH_LEN];
        int discovered = scan_mounts_for_bench(media_root, sizeof(media_root))
                             ? bench_suite_discover(media_root, plan->suite_names, BENCH_MAX_PLAN_SUITES)
                             : -1;
        if (discovered > 0) {
            fprintf(stderr, "autorun: found %d suite(s) at %s\n", discovered, media_root);
            for (int k = 0; k < discovered; k++) {
                fprintf(stderr, "autorun:   [%d] %s\n", k, plan->suite_names[k]);
            }
            char usb_toast[128];
            (void)snprintf(usb_toast, sizeof(usb_toast), "Running %d suite(s) from USB", discovered);
            bench_platform_toast(usb_toast, true, BENCH_TOAST_WARN_TIMEOUT_SEC);
            plan->kind = EXECUTION_PLAN_SUITES;
            plan->loop = false;
            plan->suite_count = discovered;
            return bench_path_copy(plan->bench_root, sizeof(plan->bench_root), media_root);
        }

        fprintf(stderr, "autorun: no external suites, scanning bundled suites\n");
        if (resolve_bundled_bench_dir(argv0, plan->bench_root, sizeof(plan->bench_root)) != 0) {
            fprintf(stderr, "Failed to resolve bundled bench directory for launcher autorun\n");
            return -1;
        }

        discovered = bench_suite_discover(plan->bench_root, plan->suite_names, BENCH_MAX_PLAN_SUITES);
        if (discovered <= 0) {
            fprintf(stderr, "autorun: no bundled suites found at %s\n", plan->bench_root);
            return -1;
        }

        fprintf(stderr, "autorun: found %d bundled suite(s) at %s\n", discovered, plan->bench_root);
        for (int k = 0; k < discovered; k++) {
            fprintf(stderr, "autorun:   [%d] %s\n", k, plan->suite_names[k]);
        }

        plan->kind = EXECUTION_PLAN_SUITES;
        plan->loop = false;
        plan->suite_count = discovered;
        return 0;
    }

    return -1;
}

int main(int argc, char *argv[]) {
    int exit_code = 3;
    bool show_overall = false;
    bool stopped = false;
    bool presentation_failed = false;
    bool modules_ready = false;
    bool os_info_ready = false;
    bool ss4s_ready = false;
    BenchDisplay display = {0};
    array_list_t modules = {0};
    os_info_t os_info = {0};
    SS4S_VideoCapabilities video_capabilities = {0};
    bool video_capabilities_available = false;
    BenchVerdict worst = BENCH_VERDICT_PASS;
    int completed_tests = 0;
    int unsupported_tests = 0;
    char results_root[BENCH_MAX_PATH_LEN];
    const char *results_root_arg = NULL;
    BenchLaunchContext launch_ctx;

    if (bench_platform_normalize_launch(argc, argv, &launch_ctx) != 0) {
        return 3;
    }
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        fprintf(stderr, "warning: could not install SIGINT handler\n");
    }

    cli_opts_t opts;
    if (parse_cli(launch_ctx.argc, launch_ctx.argv, &opts) != 0) {
        bench_platform_free_launch(&launch_ctx);
        return 3;
    }
    if (opts.help) {
        print_usage(launch_ctx.argv != NULL && launch_ctx.argv[0] != NULL ? launch_ctx.argv[0] : "decoder-bench");
        exit_code = 0;
        goto cleanup;
    }

    if (opts.list_suites) {
        char bench_root[BENCH_MAX_PATH_LEN];
        char names[BENCH_MAX_PLAN_SUITES][BENCH_MAX_NAME_LEN];
        int count;

        if (resolve_bench_dir(&opts, launch_ctx.argv != NULL ? launch_ctx.argv[0] : NULL, bench_root,
                              sizeof(bench_root)) != 0) {
            fprintf(stderr, "Failed to resolve bench directory\n");
            goto cleanup;
        }
        count = bench_suite_discover(bench_root, names, BENCH_MAX_PLAN_SUITES);
        if (count < 0) {
            fprintf(stderr, "No suites found in %s/suites/\n", bench_root);
            goto cleanup;
        }
        printf("Available suites in %s:\n", bench_root);
        for (int i = 0; i < count; i++) {
            printf("  %s\n", names[i]);
        }
        exit_code = 0;
        goto cleanup;
    }

#ifdef TARGET_WEBOS
    if (!bench_usb_acquire_run_lock()) {
        bench_platform_free_launch(&launch_ctx);
        return 3;
    }
#endif
    bench_platform_prepare_process(&launch_ctx);

    execution_plan_t plan;
    if (build_execution_plan(&launch_ctx, &opts, launch_ctx.argv != NULL ? launch_ctx.argv[0] : NULL, &plan) != 0) {
        print_usage(launch_ctx.argv != NULL && launch_ctx.argv[0] != NULL ? launch_ctx.argv[0] : "decoder-bench");
        bench_platform_toast("Invalid benchmark options", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }

#ifdef TARGET_WEBOS
    if (resolve_results_root_for_plan(&launch_ctx, &opts, &plan, results_root, sizeof(results_root)) == 0) {
        results_root_arg = results_root;
        if (bench_platform_attach_logs_to_results(&launch_ctx, results_root_arg) != 0) {
            fprintf(stderr, "Warning: failed to attach launcher logs to %s\n", results_root_arg);
        }
        printf("Resolved results directory: %s\n", results_root_arg);
    } else {
        fprintf(stderr, "Warning: no writable results directory; CSV output disabled\n");
    }
#endif

    int os_info_rc = os_info_get(&os_info);
    os_info_ready = true;
    if (os_info_rc != 0) {
        printf("Detected sdkVersion: unknown (system-property query failed)\n");
    } else {
        char *sdk_version = version_info_str(&os_info.version);
        if (sdk_version == NULL) {
            printf("Detected sdkVersion: unknown (invalid system-property value)\n");
        } else {
            printf("Detected sdkVersion: %s\n", sdk_version);
            free(sdk_version);
        }
    }

#ifdef TARGET_WEBOS
    const char *required_video_module_id = bench_webos_ndl_module_id(os_info.version.major, os_info.version.minor);
    if (os_info_rc != 0 || required_video_module_id == NULL) {
        fprintf(stderr, "Compatibility error: sdkVersion is unknown or has no compatible NDL backend\n");
        bench_platform_toast("Cannot match webOS version to decoder", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    printf("Compatibility route: sdkVersion -> %s\n", required_video_module_id);
#endif

    if (SS4S_ModulesList(&modules, &os_info) != 0) {
        fprintf(stderr, "Compatibility error: failed to load SS4S module index for detected sdkVersion\n");
        bench_platform_toast("Cannot list decoder modules", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    modules_ready = true;

    SS4S_ModulePreferences module_preferences_storage;
    const SS4S_ModulePreferences *module_preferences = NULL;
    bench_platform_get_module_preferences(&module_preferences_storage, &module_preferences);
#ifdef TARGET_WEBOS
    module_preferences_storage.video_module = required_video_module_id;
    module_preferences = &module_preferences_storage;
#endif

    SS4S_ModuleSelection selected = {0};
    bool selection_complete = SS4S_ModulesSelect(&modules, module_preferences, &selected, true);
    if (selected.video_module == NULL) {
#ifdef TARGET_WEBOS
        fprintf(stderr, "Compatibility error: module '%s' is unavailable or its loader/check rejected video\n",
                required_video_module_id);
#else
        fprintf(stderr, "Failed to select an SS4S video module: loader/check rejected every candidate\n");
#endif
        bench_platform_toast("Cannot select decoder module", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    if (!selection_complete) {
        printf("Audio module unavailable; continuing with the selected video module\n");
    }

    const char *video_module_id = SS4S_ModuleInfoGetId(selected.video_module);
    const char *video_module_group = SS4S_ModuleInfoGetGroup(selected.video_module);
    bool require_decoder_latency = video_module_group != NULL && strcmp(video_module_group, "ndl") == 0;

#ifdef TARGET_WEBOS
    if (video_module_id == NULL || strcmp(video_module_id, required_video_module_id) != 0) {
        fprintf(stderr, "Compatibility error: selected module '%s', expected '%s' for detected sdkVersion\n",
                video_module_id != NULL ? video_module_id : "(none)", required_video_module_id);
        bench_platform_toast("Wrong decoder module for webOS version", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    if (video_module_group == NULL || strcmp(video_module_group, "ndl") != 0) {
        fprintf(stderr, "Refusing non-NDL video module on webOS: %s\n",
                video_module_group != NULL ? video_module_group : "(none)");
        bench_platform_toast("Decoder module is not webOS NDL", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
#endif

    const char *video_module_name = SS4S_ModuleInfoGetName(selected.video_module);
    printf("Selected video module: %s (%s)\n", video_module_id,
           video_module_name != NULL ? video_module_name : "unnamed");

    bench_platform_apply_sdl_hints();
    if (bench_display_init_sdl(&display) != 0) {
        bench_platform_toast("Video startup failed", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }

    SS4S_Config config = {
        .audioDriver = NULL,
        .videoDriver = video_module_id,
    };
    if (SS4S_Init(launch_ctx.argc, launch_ctx.argv, &config) != 0) {
        fprintf(stderr, "Compatibility error: SS4S_Init failed for selected module '%s'\n", video_module_id);
        bench_platform_toast("Decoder startup failed", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    ss4s_ready = true;

    if (SS4S_GetVideoModuleName() == NULL) {
        fprintf(stderr, "No video module available\n");
        bench_platform_toast("No video module available", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }

    if (bench_display_create_surface(&display, bench_platform_should_stop_for_key) != 0) {
        bench_platform_toast("Window creation failed", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }

    if (SS4S_PostInit(launch_ctx.argc, launch_ctx.argv) != 0) {
        fprintf(stderr, "SS4S_PostInit failed\n");
        bench_platform_toast("Decoder post-init failed", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        goto cleanup;
    }
    video_capabilities_available = SS4S_GetVideoCapabilities(&video_capabilities);
    if (video_capabilities_available) {
        printf("Video capabilities: codec mask=0x%x\n", (unsigned int)video_capabilities.codecs);
    } else {
        fprintf(stderr, "Warning: selected video module did not report capabilities; relying on video-open result\n");
    }

    BenchTimerProbe timer_probe;
    if (bench_timer_probe(30, 5000, &timer_probe) == 0) {
        long long interval_ms = (long long)timer_probe.interval_us / 1000;
        printf("Sleep wake-up latency: %d x %lld ms sleeps; OS woke late avg %lld us, max %lld us\n",
               timer_probe.samples, interval_ms, (long long)timer_probe.avg_overshoot_us,
               (long long)timer_probe.max_overshoot_us);
        const char *verdict;
        if (timer_probe.max_overshoot_us * 10 < timer_probe.interval_us) {
            verdict = "OK";
        } else if (timer_probe.max_overshoot_us * 2 < timer_probe.interval_us) {
            verdict = "noisy";
        } else {
            verdict = "unreliable";
        }
        long long pct_tenths = (timer_probe.max_overshoot_us * 1000LL) / timer_probe.interval_us;
        printf("Feeder pacing: %s (worst lateness %lld.%lld%% of %lld ms tick)\n", verdict, pct_tenths / 10,
               pct_tenths % 10, interval_ms);
    }
    print_run_options(&opts, plan.loop);

#ifndef TARGET_WEBOS
    if (bench_platform_resolve_results_root(opts.results_dir, results_root, sizeof(results_root)) == 0) {
        results_root_arg = results_root;
        printf("Resolved results directory: %s\n", results_root_arg);
    } else {
        fprintf(stderr, "Warning: no writable results directory; CSV output disabled\n");
    }
#endif

    show_overall = true;
    exit_code = -1;

    for (unsigned int iteration = 1;; iteration++) {
        run_status_t status = RUN_STATUS_OK;

        if (plan.loop) {
            printf("\n=== Iteration %u ===\n", iteration);
        }

        if (plan.kind == EXECUTION_PLAN_DIRECT_FILE) {
            status = run_direct_once(&plan, &opts, results_root_arg,
                                     video_capabilities_available ? &video_capabilities : NULL, require_decoder_latency,
                                     &display, &worst, &completed_tests, &unsupported_tests);
        } else {
            for (int i = 0; i < plan.suite_count && status == RUN_STATUS_OK && g_interrupted == 0; i++) {
                status =
                    run_suite_once(plan.bench_root, plan.suite_names[i], &opts, results_root_arg,
                                   video_capabilities_available ? &video_capabilities : NULL, require_decoder_latency,
                                   &display, &worst, &completed_tests, &unsupported_tests);
            }
        }

        if (status == RUN_STATUS_PLATFORM_ERROR) {
            fprintf(stderr, "Presentation error: transparent SDL surface servicing failed\n");
            bench_platform_toast("Presentation failed; benchmark aborted", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
            presentation_failed = true;
            exit_code = 3;
            break;
        }
        if (status == RUN_STATUS_INVALID) {
            exit_code = 3;
            break;
        }
        if (status == RUN_STATUS_STOPPED) {
            stopped = true;
            exit_code = 0;
            break;
        }
        if (!plan.loop) {
            break;
        }
        if (g_interrupted != 0) {
            stopped = true;
            exit_code = 0;
            break;
        }
    }

cleanup:
    if (show_overall) {
        if (presentation_failed) {
            printf("\n=== Overall: PRESENTATION ERROR ===\n");
        } else if (exit_code == 3) {
            printf("\n=== Overall: INVALID ===\n");
            bench_platform_toast("/!\\ Benchmark INVALID", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        } else if (stopped && completed_tests == 0) {
            printf("\n=== Overall: STOPPED ===\n");
            bench_platform_toast("\xe2\x8f\xb9 Benchmark stopped", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        } else if (stopped) {
            printf("\n=== Overall: %s (stopped) ===\n", bench_verdict_str(worst));
            bench_platform_toast("\xe2\x8f\xb9 Benchmark stopped", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        } else if (completed_tests == 0 && unsupported_tests > 0) {
            printf("\n=== Overall: UNSUPPORTED ===\n");
            bench_platform_toast("Requested codec is unsupported", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        } else {
            printf("\n=== Overall: %s ===\n", bench_verdict_str(worst));
            switch (worst) {
            case BENCH_VERDICT_PASS:
                bench_platform_toast("\xe2\x9c\x85 Benchmark PASS", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
                break;
            case BENCH_VERDICT_WARN:
                bench_platform_toast("Benchmark WARN", false, BENCH_TOAST_WARN_TIMEOUT_SEC);
                break;
            case BENCH_VERDICT_FAIL:
                bench_platform_toast("/!\\ Benchmark FAIL", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
                break;
            default:
                bench_platform_toast("/!\\ Benchmark failed", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
                break;
            }
        }
    }

    bench_display_destroy_surface(&display);
    if (ss4s_ready) {
        SS4S_Quit();
    }
    if (modules_ready) {
        SS4S_ModulesListClear(&modules);
    }
    if (os_info_ready) {
        os_info_clear(&os_info);
    }
    bench_display_quit_sdl(&display);
    bench_platform_free_launch(&launch_ctx);

    if (exit_code >= 0) {
        return exit_code;
    }
    switch (worst) {
    case BENCH_VERDICT_PASS:
        return 0;
    case BENCH_VERDICT_WARN:
        return 1;
    case BENCH_VERDICT_FAIL:
        return 2;
    default:
        return 3;
    }
}
