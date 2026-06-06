#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <SDL2/SDL.h>

#include "ss4s.h"
#include "ss4s_modules.h"

#include "bench_types.h"

typedef enum BenchLaunchSource {
    BENCH_LAUNCH_SOURCE_CLI = 0,
    BENCH_LAUNCH_SOURCE_WEBOS_LAUNCHER = 1,
} BenchLaunchSource;

typedef struct BenchLaunchContext {
    int argc;
    char **argv;
    bool owns_argv;
    BenchLaunchSource source;
    bool autorun_no_args;
    bool log_redirection_enabled;
} BenchLaunchContext;

int bench_platform_normalize_launch(int argc, char *argv[], BenchLaunchContext *out);
void bench_platform_free_launch(BenchLaunchContext *ctx);
void bench_platform_prepare_process(const BenchLaunchContext *ctx);
void bench_platform_apply_sdl_hints(void);
void bench_platform_get_module_preferences(SS4S_ModulePreferences *storage, const SS4S_ModulePreferences **preferences);
bool bench_platform_should_stop_for_key(const SDL_KeyboardEvent *event);
int bench_platform_resolve_results_root(const char *override_dir, char *out, size_t out_size);

#ifdef TARGET_WEBOS
int bench_platform_attach_logs_to_results(const BenchLaunchContext *ctx, const char *results_root);
void bench_platform_toast(const char *message, bool is_light, unsigned int timeout_sec);
#else
static inline void bench_platform_toast(const char *message, bool is_light, unsigned int timeout_sec) {
    (void)message;
    (void)is_light;
    (void)timeout_sec;
}
#endif
