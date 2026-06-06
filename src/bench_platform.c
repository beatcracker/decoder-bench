#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench_platform.h"

#ifdef TARGET_WEBOS
#include "bench_platform_webos.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bench_path.h"
#include "bench_strings.h"
#include "config.h"

#if defined(TARGET_WEBOS) || defined(BENCH_PLATFORM_EXPOSE_INTERNALS)
#define BENCH_PLATFORM_REMOTE_STOP_KEYS 1
#endif

#if defined(TARGET_WEBOS) || defined(BENCH_PLATFORM_EXPOSE_INTERNALS)
bool bench_platform_option_expects_value(const char *arg) {
    return arg != NULL && (strcmp(arg, BENCH_STRINGS_CLI_SUITE) == 0 || strcmp(arg, BENCH_STRINGS_CLI_FILE) == 0 ||
                           strcmp(arg, BENCH_STRINGS_CLI_FPS) == 0 || strcmp(arg, BENCH_STRINGS_CLI_RUN_SECONDS) == 0 ||
                           strcmp(arg, BENCH_STRINGS_CLI_SOURCE_BUFFER_MIB) == 0 ||
                           strcmp(arg, BENCH_STRINGS_CLI_DIR) == 0 || strcmp(arg, BENCH_STRINGS_CLI_RESULTS_DIR) == 0);
}
#endif

static bool bench_platform_is_stop_keycode(SDL_Keycode keycode) {
    if (keycode == SDLK_ESCAPE) {
        return true;
    }

#ifdef BENCH_PLATFORM_REMOTE_STOP_KEYS
    if (keycode == SDLK_AC_BACK || keycode == SDLK_EJECT) {
        return true;
    }
#endif

    return false;
}

#ifdef BENCH_PLATFORM_REMOTE_STOP_KEYS
static bool bench_platform_is_stop_scancode(SDL_Scancode scancode) {
    int code = (int)scancode;

    if (code == SDL_SCANCODE_AC_BACK || code == SDL_SCANCODE_EJECT) {
        return true;
    }

#ifdef SDL_WEBOS_SCANCODE_BACK
    if (code == SDL_WEBOS_SCANCODE_BACK) {
        return true;
    }
#endif

#ifdef SDL_WEBOS_SCANCODE_EXIT
    if (code == SDL_WEBOS_SCANCODE_EXIT) {
        return true;
    }
#endif

#ifdef SDL_SCANCODE_WEBOS_BACK
    if (code == SDL_SCANCODE_WEBOS_BACK) {
        return true;
    }
#endif

#ifdef SDL_SCANCODE_WEBOS_EXIT
    if (code == SDL_SCANCODE_WEBOS_EXIT) {
        return true;
    }
#endif

    return false;
}
#endif

#ifndef TARGET_WEBOS
static int resolve_sdl_pref_results_root(char *out, size_t out_size) {
    char *pref_path = SDL_GetPrefPath(DECODER_BENCH_PREF_ORG, DECODER_BENCH_PREF_APP);

    if (pref_path == NULL) {
        return -1;
    }

    size_t len = SDL_strlen(pref_path);
    if (len > 0 && pref_path[len - 1] == '/') {
        pref_path[len - 1] = '\0';
    }

    int rc = -1;
    if (bench_path_join(out, out_size, pref_path, BENCH_STRINGS_DIR_RESULTS) == 0 && bench_path_dir_ensure(out) == 0) {
        rc = 0;
    }
    SDL_free(pref_path);
    return rc;
}
#endif

static int resolve_tmp_results_root(char *out, size_t out_size) {
    char base[BENCH_MAX_PATH_LEN];
    if (bench_path_copy(base, sizeof(base), BENCH_STRINGS_TMP_ROOT) != 0 || bench_path_dir_ensure(base) != 0 ||
        bench_path_join(out, out_size, base, BENCH_STRINGS_DIR_RESULTS) != 0 || bench_path_dir_ensure(out) != 0) {
        return -1;
    }
    return 0;
}

int bench_platform_normalize_launch(int argc, char *argv[], BenchLaunchContext *out) {
    if (out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->argc = argc;
    out->argv = argv;
    out->source = BENCH_LAUNCH_SOURCE_CLI;

#ifdef TARGET_WEBOS
    return bench_platform_webos_normalize_launch(argc, argv, out);
#endif

    return 0;
}

void bench_platform_free_launch(BenchLaunchContext *ctx) {
    if (ctx == NULL) {
        return;
    }
    if (ctx->owns_argv) {
        free(ctx->argv);
    }
    memset(ctx, 0, sizeof(*ctx));
}

void bench_platform_prepare_process(const BenchLaunchContext *ctx) {
#ifdef TARGET_WEBOS
    bench_platform_webos_prepare_process(ctx);
#else
    (void)ctx;
#endif
}

void bench_platform_apply_sdl_hints(void) {
#ifdef TARGET_WEBOS
    bench_platform_webos_apply_sdl_hints();
#endif
}

void bench_platform_get_module_preferences(SS4S_ModulePreferences *storage,
                                           const SS4S_ModulePreferences **preferences) {
    if (preferences == NULL) {
        return;
    }

#ifdef TARGET_WEBOS
    bench_platform_webos_get_module_preferences(storage, preferences);
#else
    (void)storage;
    *preferences = NULL;
#endif
}

bool bench_platform_should_stop_for_key(const SDL_KeyboardEvent *event) {
    if (event == NULL || event->state != SDL_PRESSED) {
        return false;
    }

    if (bench_platform_is_stop_keycode(event->keysym.sym)) {
        return true;
    }

#ifdef BENCH_PLATFORM_REMOTE_STOP_KEYS
    if (bench_platform_is_stop_scancode(event->keysym.scancode)) {
        return true;
    }
#endif

    return false;
}

int bench_platform_resolve_results_root(const char *override_dir, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return -1;
    }

    if (override_dir != NULL && override_dir[0] != '\0') {
        if (bench_path_copy(out, out_size, override_dir) == 0 && bench_path_dir_ensure(out) == 0) {
            return 0;
        }
    }

#ifdef TARGET_WEBOS
    return resolve_tmp_results_root(out, out_size);
#else
    if (resolve_sdl_pref_results_root(out, out_size) == 0) {
        return 0;
    }

    return resolve_tmp_results_root(out, out_size);
#endif
}
