#pragma once

#ifdef TARGET_WEBOS

#include "bench_platform.h"

int bench_platform_webos_normalize_launch(int argc, char *argv[], BenchLaunchContext *out);
void bench_platform_webos_prepare_process(const BenchLaunchContext *ctx);
void bench_platform_webos_apply_sdl_hints(void);
void bench_platform_webos_get_module_preferences(SS4S_ModulePreferences *storage,
                                                 const SS4S_ModulePreferences **preferences);

#endif