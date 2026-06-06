#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_platform_webos.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bench_path.h"
#include "bench_pbnjson_compat.h"
#include "bench_platform_internal.h"
#include "bench_strings.h"
#include "config.h"

#include "lunasynccall.h"

#define BENCH_PLATFORM_STDOUT_LOG BENCH_STRINGS_TMP_ROOT "/" BENCH_STRINGS_LOG_STDOUT
#define BENCH_PLATFORM_STDERR_LOG BENCH_STRINGS_TMP_ROOT "/" BENCH_STRINGS_LOG_STDERR

#define BENCH_TOAST_MAX_MSG 60
#define BENCH_TOAST_MAX_TIMEOUT_SEC 60U
#define BENCH_TOAST_ICON_PATH "/media/developer/apps/usr/palm/applications/" DECODER_BENCH_APP_ID "/icon.png"

#define BENCH_WEBOS_NOTIFICATION_CREATE_TOAST "luna://com.webos.notification/createToast"
#define BENCH_WEBOS_NOTIFICATION_CLOSE_TOAST "luna://com.webos.notification/closeToast"

typedef struct BenchToastIdParse {
    char *value;
    bool failed;
} BenchToastIdParse;

typedef struct BenchLunaReturnValue {
    bool ok;
    bool present;
} BenchLunaReturnValue;

static int redirect_stdio_to_logs(const char *stdout_path, const char *stderr_path) {
    int stdout_fd = -1;
    int stderr_fd = -1;
    int rc = -1;

    if (stdout_path == NULL || stderr_path == NULL) {
        return -1;
    }

    stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (stdout_fd < 0) {
        return -1;
    }
    stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (stderr_fd < 0) {
        goto cleanup;
    }

    if (fflush(stdout) != 0 || fflush(stderr) != 0) {
        goto cleanup;
    }
    if (dup2(stdout_fd, STDOUT_FILENO) < 0 || dup2(stderr_fd, STDERR_FILENO) < 0) {
        goto cleanup;
    }
    clearerr(stdout);
    clearerr(stderr);
    rc = 0;

cleanup:
    if (stderr_fd >= 0 && close(stderr_fd) != 0) {
        rc = -1;
    }
    if (close(stdout_fd) != 0) {
        rc = -1;
    }
    return rc;
}

static void bench_platform_sleep_ms(long ms) {
    struct timespec ts;

    if (ms <= 0) {
        return;
    }

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static void ensure_webos_appid(void) {
    const char *appid = getenv(BENCH_STRINGS_ENV_APPID);
    if (appid == NULL || appid[0] == '\0') {
        if (setenv(BENCH_STRINGS_ENV_APPID, DECODER_BENCH_APP_ID, 0) != 0) {
            fprintf(stderr, "bench_platform: setenv APPID failed: %s\n", strerror(errno));
        }
    }
}

static bool bench_platform_is_webos_launch_payload(const char *arg) {
    return arg != NULL && bench_pbnjson_is_object(arg);
}

static bool bench_platform_read_return_value(jvalue_ref object, void *userdata) {
    BenchLunaReturnValue *result = userdata;
    jvalue_ref rv = jobject_get(object, J_CSTR_TO_BUF("returnValue"));

    if (result == NULL || !jis_valid(rv)) {
        return false;
    }

    result->present = true;
    jboolean_get(rv, &result->ok);
    return true;
}

static bool bench_platform_luna_return_ok(const char *json) {
    BenchLunaReturnValue result = {0};
    return bench_pbnjson_visit_object(json, bench_platform_read_return_value, &result) && result.present && result.ok;
}

static char *bench_platform_dup_json_string_field(jvalue_ref object, const char *field) {
    jvalue_ref value = jobject_get(object, J_CSTR_TO_BUF(field));

    if (!jis_string(value)) {
        return NULL;
    }

    raw_buffer buffer = jstring_get(value);
    if (buffer.m_str == NULL || buffer.m_len == 0 || buffer.m_len == SIZE_MAX) {
        return NULL;
    }

    char *copy = malloc(buffer.m_len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, buffer.m_str, buffer.m_len);
    copy[buffer.m_len] = '\0';
    return copy;
}

static bool bench_platform_read_toast_id(jvalue_ref object, void *userdata) {
    BenchToastIdParse *result = userdata;

    if (result == NULL) {
        return false;
    }

    result->value = bench_platform_dup_json_string_field(object, "toastId");
    if (result->value == NULL) {
        jvalue_ref value = jobject_get(object, J_CSTR_TO_BUF("toastId"));
        if (jis_string(value)) {
            result->failed = true;
            return false;
        }
    }
    return true;
}

static char *bench_platform_extract_toast_id(const char *json) {
    BenchToastIdParse result = {0};

    if (!bench_pbnjson_visit_object(json, bench_platform_read_toast_id, &result) || result.failed) {
        free(result.value);
        return NULL;
    }
    return result.value;
}

static void bench_platform_copy_toast_message(const char *message, char *out, size_t out_size) {
    int written = snprintf(out, out_size, "%s", message);

    if (written < 0) {
        out[0] = '\0';
        return;
    }
    if ((size_t)written >= out_size) {
        fprintf(stderr, "bench_platform: toast message truncated to %d bytes\n", BENCH_TOAST_MAX_MSG);
    }
}

static unsigned int bench_platform_clamp_toast_timeout(unsigned int timeout_sec) {
    if (timeout_sec > BENCH_TOAST_MAX_TIMEOUT_SEC) {
        fprintf(stderr, "bench_platform: toast timeout %u exceeds max %u; clamping\n", timeout_sec,
                BENCH_TOAST_MAX_TIMEOUT_SEC);
        return BENCH_TOAST_MAX_TIMEOUT_SEC;
    }
    return timeout_sec;
}

static char *bench_platform_build_toast_payload(const char *message, bool is_light) {
    jvalue_ref object =
        jobject_create_var(jkeyval(J_CSTR_TO_JVAL("sourceId"), jstring_create(DECODER_BENCH_APP_ID)),
                           jkeyval(J_CSTR_TO_JVAL("message"), jstring_create(message)),
                           jkeyval(J_CSTR_TO_JVAL("iconUrl"), jstring_create(BENCH_TOAST_ICON_PATH)),
                           jkeyval(J_CSTR_TO_JVAL("type"), jstring_create(is_light ? "light" : "standard")),
                           jkeyval(J_CSTR_TO_JVAL("persistent"), jboolean_create(false)),
                           jkeyval(J_CSTR_TO_JVAL("onlyToast"), jboolean_create(true)), J_END_OBJ_DECL);
    char *payload = bench_pbnjson_serialize_alloc(object);
    j_release(&object);
    return payload;
}

static char *bench_platform_build_close_toast_payload(const char *toast_id) {
    jvalue_ref object =
        jobject_create_var(jkeyval(J_CSTR_TO_JVAL("toastId"), jstring_create(toast_id)), J_END_OBJ_DECL);
    char *payload = bench_pbnjson_serialize_alloc(object);
    j_release(&object);
    return payload;
}

int bench_platform_webos_normalize_launch(int argc, char *argv[], BenchLaunchContext *out) {
    bool had_payload = false;
    int filtered_argc = argc > 0 ? 1 : 0;
    bool expect_value = false;

    if (out == NULL) {
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (expect_value) {
            filtered_argc++;
            expect_value = false;
            continue;
        }

        if (bench_platform_is_webos_launch_payload(argv[i])) {
            had_payload = true;
            continue;
        }

        filtered_argc++;
        expect_value = bench_platform_option_expects_value(argv[i]);
    }

    if (!had_payload) {
        return 0;
    }

    out->argv = calloc((size_t)filtered_argc + 1u, sizeof(char *));
    if (out->argv == NULL) {
        return -1;
    }

    out->owns_argv = true;
    out->argc = filtered_argc;
    out->source = BENCH_LAUNCH_SOURCE_WEBOS_LAUNCHER;
    out->argv[0] = argc > 0 ? argv[0] : NULL;

    int write_index = argc > 0 ? 1 : 0;
    expect_value = false;
    for (int i = 1; i < argc; i++) {
        if (expect_value) {
            out->argv[write_index++] = argv[i];
            expect_value = false;
            continue;
        }

        if (bench_platform_is_webos_launch_payload(argv[i])) {
            continue;
        }

        out->argv[write_index++] = argv[i];
        expect_value = bench_platform_option_expects_value(argv[i]);
    }
    out->argv[write_index] = NULL;
    out->autorun_no_args = filtered_argc <= 1;
    out->log_redirection_enabled = out->autorun_no_args;
    return 0;
}

void bench_platform_webos_prepare_process(const BenchLaunchContext *ctx) {
    ensure_webos_appid();
    if (ctx != NULL && ctx->log_redirection_enabled) {
        if (bench_path_dir_ensure(BENCH_STRINGS_TMP_ROOT) != 0) {
            _exit(1);
        }
        if (freopen(BENCH_PLATFORM_STDOUT_LOG, "w", stdout) == NULL) {
            _exit(1);
        }
        if (freopen(BENCH_PLATFORM_STDERR_LOG, "w", stderr) == NULL) {
            _exit(1);
        }
    }
}

int bench_platform_attach_logs_to_results(const BenchLaunchContext *ctx, const char *results_root) {
    char stdout_path[BENCH_MAX_PATH_LEN];
    char stderr_path[BENCH_MAX_PATH_LEN];

    if (ctx == NULL || !ctx->log_redirection_enabled) {
        return 0;
    }
    if (results_root == NULL || results_root[0] == '\0') {
        return -1;
    }
    if (bench_path_join(stdout_path, sizeof(stdout_path), results_root, BENCH_STRINGS_LOG_STDOUT) != 0 ||
        bench_path_join(stderr_path, sizeof(stderr_path), results_root, BENCH_STRINGS_LOG_STDERR) != 0) {
        return -1;
    }

    if (redirect_stdio_to_logs(stdout_path, stderr_path) != 0) {
        return -1;
    }

    fprintf(stderr, "bench_platform: launcher logs redirected to %s\n", results_root);
    return 0;
}

void bench_platform_webos_apply_sdl_hints(void) {
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_EXIT, "true");
}

void bench_platform_webos_get_module_preferences(SS4S_ModulePreferences *storage,
                                                 const SS4S_ModulePreferences **preferences) {
    if (preferences == NULL) {
        return;
    }
    if (storage == NULL) {
        *preferences = NULL;
        return;
    }

    *storage = (SS4S_ModulePreferences){
        .audio_module = MODULE_PREFERENCE_AUTO,
        .video_module = "ndl",
    };
    *preferences = storage;
}

void bench_platform_toast(const char *message, bool is_light, unsigned int timeout_sec) {
    if (message == NULL || message[0] == '\0') {
        return;
    }

    ensure_webos_appid();

    char safe_msg[BENCH_TOAST_MAX_MSG + 1];
    bench_platform_copy_toast_message(message, safe_msg, sizeof(safe_msg));

    char *payload = bench_platform_build_toast_payload(safe_msg, is_light);
    if (payload == NULL) {
        return;
    }

    char *output = NULL;
    if (!HLunaServiceCallSync(BENCH_WEBOS_NOTIFICATION_CREATE_TOAST, payload, true, &output)) {
        fprintf(stderr, "bench_platform: createToast call failed: %s\n", output != NULL ? output : "(null)");
        free(output);
        free(payload);
        return;
    }
    if (!bench_platform_luna_return_ok(output)) {
        fprintf(stderr, "bench_platform: createToast rejected: %s\n", output != NULL ? output : "(null)");
        free(output);
        free(payload);
        return;
    }
    if (timeout_sec == 0) {
        free(output);
        free(payload);
        return;
    }

    unsigned int clamped_timeout_sec = bench_platform_clamp_toast_timeout(timeout_sec);
    char *toast_id = bench_platform_extract_toast_id(output);
    free(output);

    if (toast_id == NULL) {
        fprintf(stderr, "bench_platform: createToast returned no toastId for forced close\n");
        free(payload);
        return;
    }

    bench_platform_sleep_ms((long)(clamped_timeout_sec * 1000u));

    char *close_payload = bench_platform_build_close_toast_payload(toast_id);
    if (close_payload != NULL) {
        char *close_output = NULL;
        if (!HLunaServiceCallSync(BENCH_WEBOS_NOTIFICATION_CLOSE_TOAST, close_payload, true, &close_output)) {
            fprintf(stderr, "bench_platform: closeToast call failed: %s\n",
                    close_output != NULL ? close_output : "(null)");
        } else if (!bench_platform_luna_return_ok(close_output)) {
            fprintf(stderr, "bench_platform: closeToast rejected: %s\n",
                    close_output != NULL ? close_output : "(null)");
        }
        free(close_output);
        free(close_payload);
    }
    free(toast_id);
    free(payload);

    bench_platform_sleep_ms(500);
}