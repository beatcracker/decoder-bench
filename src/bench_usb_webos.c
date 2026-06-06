#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_usb_webos.h"

#include "bench_platform.h"

#include "bench_path.h"
#include "bench_pbnjson_compat.h"
#include "bench_strings.h"
#include "bench_types.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "lunasynccall.h"
#include <pbnjson.h>

#define HBC_SERVICE "luna://org.webosbrew.hbchannel.service"
#define USB_MOUNT_SCRIPT "usb-mount.sh"
#define BENCH_TOAST_WARN_TIMEOUT_SEC 3u
#define BENCH_TOAST_ERROR_TIMEOUT_SEC 5u

static int usb_run_lock_fd = -1;

typedef struct LunaReturnValue {
    bool ok;
    bool present;
} LunaReturnValue;

static char *shell_quote_single(const char *value) {
    size_t quoted_len = 2;

    if (value == NULL) {
        return NULL;
    }

    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '\'') {
            if (quoted_len >= SIZE_MAX - 4) {
                return NULL;
            }
            quoted_len += 4;
            continue;
        }
        if (quoted_len >= SIZE_MAX - 1) {
            return NULL;
        }
        quoted_len++;
    }

    char *quoted = malloc(quoted_len + 1);
    if (quoted == NULL) {
        return NULL;
    }

    size_t len = 0;
    quoted[len++] = '\'';
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '\'') {
            memcpy(quoted + len, "'\\''", 4);
            len += 4;
            continue;
        }
        quoted[len++] = *cursor;
    }
    quoted[len++] = '\'';
    quoted[len] = '\0';
    return quoted;
}

static bool checked_add_size(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool usb_run_lock_held(void) {
    return usb_run_lock_fd >= 0;
}

static char *build_usb_mount_command_alloc(const char *script) {
    const char prefix[] = "sh ";
    const char separator[] = " ";
    char *quoted_script = NULL;
    char *quoted_appid = NULL;
    char *command = NULL;

    if (script == NULL) {
        return NULL;
    }

    quoted_script = shell_quote_single(script);
    quoted_appid = shell_quote_single(DECODER_BENCH_APP_ID);
    if (quoted_script == NULL || quoted_appid == NULL) {
        goto cleanup;
    }

    size_t command_len = sizeof(prefix) - 1u;
    size_t alloc_size = 0;
    if (!checked_add_size(command_len, strlen(quoted_script), &command_len) ||
        !checked_add_size(command_len, sizeof(separator) - 1u, &command_len) ||
        !checked_add_size(command_len, strlen(quoted_appid), &command_len) ||
        !checked_add_size(command_len, 1u, &alloc_size)) {
        goto cleanup;
    }

    command = malloc(alloc_size);
    if (command == NULL) {
        goto cleanup;
    }

    int written = snprintf(command, alloc_size, "%s%s%s%s", prefix, quoted_script, separator, quoted_appid);
    if (written < 0 || (size_t)written != command_len) {
        free(command);
        command = NULL;
    }

cleanup:
    free(quoted_script);
    free(quoted_appid);
    return command;
}

static char *build_exec_payload_alloc(const char *command) {
    if (command == NULL) {
        return NULL;
    }

    jvalue_ref object = jobject_create_var(jkeyval(J_CSTR_TO_JVAL("command"), jstring_create(command)), J_END_OBJ_DECL);
    char *payload = bench_pbnjson_serialize_alloc(object);
    j_release(&object);
    return payload;
}

static bool read_return_value(jvalue_ref object, void *userdata) {
    LunaReturnValue *result = userdata;
    jvalue_ref rv = jobject_get(object, J_CSTR_TO_BUF("returnValue"));

    if (result == NULL || !jis_valid(rv)) {
        return false;
    }

    result->present = true;
    jboolean_get(rv, &result->ok);
    return true;
}

static bool luna_return_ok(const char *json) {
    LunaReturnValue result = {0};
    return bench_pbnjson_visit_object(json, read_return_value, &result) && result.present && result.ok;
}

static void log_luna_string_field(jvalue_ref object, const char *field) {
    jvalue_ref value = jobject_get(object, J_CSTR_TO_BUF(field));

    if (!jis_string(value)) {
        return;
    }

    raw_buffer buffer = jstring_get(value);
    if (buffer.m_str == NULL || buffer.m_len == 0) {
        return;
    }

    int print_len = buffer.m_len <= (size_t)INT_MAX ? (int)buffer.m_len : INT_MAX;
    fprintf(stderr, "usb: %s: %.*s\n", field, print_len, buffer.m_str);
}

static bool log_luna_exec_fields(jvalue_ref object, void *userdata) {
    (void)userdata;
    log_luna_string_field(object, "stdoutString");
    log_luna_string_field(object, "stderrString");
    log_luna_string_field(object, "errorText");
    return true;
}

static void log_luna_exec_details(const char *json) {
    (void)bench_pbnjson_visit_object(json, log_luna_exec_fields, NULL);
}

bool bench_usb_acquire_run_lock(void) {
    char lock_path[BENCH_MAX_PATH_LEN];
    int fd;

    if (usb_run_lock_held()) {
        return true;
    }

    if (bench_path_dir_ensure(BENCH_STRINGS_TMP_ROOT) != 0) {
        fprintf(stderr, "usb: cannot create run lock directory %s: %s\n", BENCH_STRINGS_TMP_ROOT, strerror(errno));
        bench_platform_toast("Run lock unavailable", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        return false;
    }
    if (bench_path_join(lock_path, sizeof(lock_path), BENCH_STRINGS_TMP_ROOT, BENCH_STRINGS_WEBOS_RUN_LOCK) != 0) {
        fprintf(stderr, "usb: run lock path too long\n");
        bench_platform_toast("Run lock unavailable", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        return false;
    }

    fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "usb: cannot open run lock %s: %s\n", lock_path, strerror(errno));
        bench_platform_toast("Run lock unavailable", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        return false;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        if (close(fd) != 0) {
            fprintf(stderr, "usb: cannot close failed run lock: %s\n", strerror(errno));
        }
        if (saved_errno == EWOULDBLOCK
#if EAGAIN != EWOULDBLOCK
            || saved_errno == EAGAIN
#endif
        ) {
            fprintf(stderr, "usb: another decoder-bench instance already owns %s\n", lock_path);
            bench_platform_toast("Decoder bench already running", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
            return false;
        }
        fprintf(stderr, "usb: cannot acquire run lock %s: %s\n", lock_path, strerror(saved_errno));
        bench_platform_toast("Run lock unavailable", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        return false;
    }

    /* fd is intentionally retained for the process lifetime — the kernel
     * releases the flock on exit, which is the exit signal the helper script's
     * flock watcher waits on. Closing here would break the rendezvous. */
    usb_run_lock_fd = fd;
    fprintf(stderr, "usb: run lock acquired at %s\n", lock_path);
    return true;
}

static int resolve_usb_mount_script(char *script, size_t script_size) {
    char executable_dir[BENCH_MAX_PATH_LEN];

    if (bench_path_readlink(executable_dir, sizeof(executable_dir), BENCH_STRINGS_PROC_SELF_EXE) != 0) {
        fprintf(stderr, "usb: cannot resolve executable path\n");
        return -1;
    }
    if (!bench_path_parent(executable_dir)) {
        return -1;
    }

    if (bench_path_join(script, script_size, executable_dir, USB_MOUNT_SCRIPT) != 0) {
        return -1;
    }

    if (access(script, R_OK) != 0) {
        fprintf(stderr, "usb: helper script unavailable at %s: %s\n", script, strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * HBC /exec runs outside the app jail as host root, which is required for the
 * bind mounts under the decoder-bench-owned jail path.
 */
bool bench_usb_prepare_access(void) {
    char *check_out = NULL;
    char *command = NULL;
    char *payload = NULL;
    char *exec_out = NULL;
    bool prepared = false;

    if (!usb_run_lock_held()) {
        fprintf(stderr, "usb: refusing helper mount without the webOS run lock\n");
        bench_platform_toast("Run lock unavailable", false, BENCH_TOAST_ERROR_TIMEOUT_SEC);
        return false;
    }

    bool ok = HLunaServiceCallSync(HBC_SERVICE "/checkRoot", "{}", true, &check_out);
    if (!ok || !luna_return_ok(check_out)) {
        fprintf(stderr, "usb: HBC not available or not root (response: %s)\n", check_out ? check_out : "(none)");
        free(check_out);
        bench_platform_toast("No root/HBC - running built-in benchmark", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        return false;
    }
    free(check_out);
    fprintf(stderr, "usb: HBC root confirmed\n");

    char script[BENCH_MAX_PATH_LEN];
    if (resolve_usb_mount_script(script, sizeof(script)) != 0) {
        fprintf(stderr, "usb: cannot resolve helper script\n");
        bench_platform_toast("USB helper missing - running built-in benchmark", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        return false;
    }

    command = build_usb_mount_command_alloc(script);
    if (command == NULL) {
        fprintf(stderr, "usb: helper command too large\n");
        bench_platform_toast("USB helper too large - running built-in benchmark", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        goto cleanup;
    }

    payload = build_exec_payload_alloc(command);
    if (payload == NULL) {
        fprintf(stderr, "usb: exec payload too large\n");
        bench_platform_toast("USB helper payload too large - running built-in benchmark", true,
                             BENCH_TOAST_WARN_TIMEOUT_SEC);
        goto cleanup;
    }

    fprintf(stderr, "usb: exec: %s\n", command);

    ok = HLunaServiceCallSync(HBC_SERVICE "/exec", payload, true, &exec_out);
    log_luna_exec_details(exec_out);
    if (!ok || !luna_return_ok(exec_out)) {
        fprintf(stderr, "usb: mount failed (response: %s)\n", exec_out ? exec_out : "(none)");
        bench_platform_toast("No usable USB drive - running built-in benchmark", true, BENCH_TOAST_WARN_TIMEOUT_SEC);
        goto cleanup;
    }
    fprintf(stderr, "usb: mount succeeded (response: %s)\n", exec_out);

    prepared = true;

cleanup:
    free(exec_out);
    free(payload);
    free(command);
    return prepared;
}
