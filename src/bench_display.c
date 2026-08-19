#include "bench_display.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

#ifndef SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY
#define SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY "SDL_VIDEO_EGL_ALLOW_TRANSPARENCY"
#endif

static int present_transparent(BenchDisplay *display) {
    if (SDL_RenderClear(display->renderer) != 0) {
        fprintf(stderr, "SDL_RenderClear transparent presentation failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderPresent(display->renderer);
    return 0;
}

int bench_display_init_sdl(BenchDisplay *display) {
    if (display == NULL) {
        fprintf(stderr, "bench_display_init_sdl: display is NULL\n");
        return -1;
    }

    memset(display, 0, sizeof(*display));
    if (SDL_SetHint(SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY, "1") != SDL_TRUE) {
        fprintf(stderr, "SDL_SetHint(%s) failed: %s\n", SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY, SDL_GetError());
        return -1;
    }
    const char *transparency_hint = SDL_GetHint(SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY);
    if (transparency_hint == NULL || strcmp(transparency_hint, "1") != 0) {
        fprintf(stderr, "SDL transparency hint was not stored (value=%s)\n",
                transparency_hint != NULL ? transparency_hint : "(null)");
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
        return -1;
    }
    display->sdl_ready = true;

    SDL_version runtime_version;
    SDL_GetVersion(&runtime_version);
    const char *driver = SDL_GetCurrentVideoDriver();
    printf("SDL versions: compiled=%u.%u.%u runtime=%u.%u.%u\n", (unsigned int)SDL_MAJOR_VERSION,
           (unsigned int)SDL_MINOR_VERSION, (unsigned int)SDL_PATCHLEVEL, (unsigned int)runtime_version.major,
           (unsigned int)runtime_version.minor, (unsigned int)runtime_version.patch);
    printf("SDL video driver: %s\n", driver != NULL ? driver : "(none)");
    printf("SDL transparency hint: requested=1 stored=%s\n", transparency_hint);
    return 0;
}

int bench_display_create_surface(BenchDisplay *display, BenchDisplayStopKeyFn should_stop_for_key) {
    if (display == NULL || !display->sdl_ready || display->window != NULL || display->renderer != NULL) {
        fprintf(stderr, "bench_display_create_surface: invalid display state\n");
        return -1;
    }

#if FEATURE_FORCE_FULLSCREEN
#if FEATURE_WINDOW_FULLSCREEN_DESKTOP
    const Uint32 window_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    const Uint32 window_flags = SDL_WINDOW_FULLSCREEN;
#endif
    SDL_DisplayMode display_mode;
    if (SDL_GetDisplayMode(0, 0, &display_mode) != 0) {
        fprintf(stderr, "SDL_GetDisplayMode failed: %s\n", SDL_GetError());
        return -1;
    }
    if (display_mode.w <= 0 || display_mode.h <= 0) {
        fprintf(stderr, "SDL_GetDisplayMode returned invalid size %dx%d\n", display_mode.w, display_mode.h);
        return -1;
    }
    const int window_width = display_mode.w;
    const int window_height = display_mode.h;
#else
    const Uint32 window_flags = SDL_WINDOW_SHOWN;
    const int window_width = 1280;
    const int window_height = 720;
#endif

    display->window = SDL_CreateWindow("decoder-bench", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width,
                                       window_height, window_flags);
    if (display->window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GetWindowSize(display->window, &display->viewport_width, &display->viewport_height);
    if (display->viewport_width <= 0 || display->viewport_height <= 0) {
        fprintf(stderr, "SDL_GetWindowSize returned invalid size %dx%d\n", display->viewport_width,
                display->viewport_height);
        bench_display_destroy_surface(display);
        return -1;
    }

    display->renderer = SDL_CreateRenderer(display->window, -1, SDL_RENDERER_ACCELERATED);
    if (display->renderer == NULL) {
#ifdef TARGET_WEBOS
        fprintf(stderr, "SDL_CreateRenderer accelerated failed on webOS: %s\n", SDL_GetError());
        bench_display_destroy_surface(display);
        return -1;
#else
        fprintf(stderr, "SDL_CreateRenderer accelerated failed: %s; trying software renderer\n", SDL_GetError());
        display->renderer = SDL_CreateRenderer(display->window, -1, SDL_RENDERER_SOFTWARE);
        if (display->renderer == NULL) {
            fprintf(stderr, "SDL_CreateRenderer software failed: %s\n", SDL_GetError());
            bench_display_destroy_surface(display);
            return -1;
        }
#endif
    }

    SDL_RendererInfo renderer_info;
    if (SDL_GetRendererInfo(display->renderer, &renderer_info) != 0) {
        fprintf(stderr, "SDL_GetRendererInfo failed: %s\n", SDL_GetError());
        bench_display_destroy_surface(display);
        return -1;
    }
#ifdef TARGET_WEBOS
    if ((renderer_info.flags & SDL_RENDERER_ACCELERATED) == 0) {
        fprintf(stderr, "SDL renderer '%s' is not accelerated on webOS (flags=0x%x)\n",
                renderer_info.name != NULL ? renderer_info.name : "(unnamed)", (unsigned int)renderer_info.flags);
        bench_display_destroy_surface(display);
        return -1;
    }
#endif

    int output_width = 0;
    int output_height = 0;
    if (SDL_GetRendererOutputSize(display->renderer, &output_width, &output_height) != 0) {
        fprintf(stderr, "SDL_GetRendererOutputSize failed: %s\n", SDL_GetError());
        bench_display_destroy_surface(display);
        return -1;
    }
    SDL_Rect renderer_viewport;
    SDL_RenderGetViewport(display->renderer, &renderer_viewport);

    if (SDL_SetRenderDrawBlendMode(display->renderer, SDL_BLENDMODE_NONE) != 0) {
        fprintf(stderr, "SDL_SetRenderDrawBlendMode(SDL_BLENDMODE_NONE) failed: %s\n", SDL_GetError());
        bench_display_destroy_surface(display);
        return -1;
    }
    if (SDL_SetRenderDrawColor(display->renderer, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "SDL_SetRenderDrawColor(transparent) failed: %s\n", SDL_GetError());
        bench_display_destroy_surface(display);
        return -1;
    }
    if (present_transparent(display) != 0) {
        bench_display_destroy_surface(display);
        return -1;
    }

    Uint32 pixel_format = SDL_GetWindowPixelFormat(display->window);
    printf("SDL window: size=%dx%d pixel-format=%s (0x%x)\n", display->viewport_width, display->viewport_height,
           SDL_GetPixelFormatName(pixel_format), (unsigned int)pixel_format);
    printf("SDL renderer: name=%s flags=0x%x output=%dx%d viewport=%d,%d %dx%d\n",
           renderer_info.name != NULL ? renderer_info.name : "(unnamed)", (unsigned int)renderer_info.flags,
           output_width, output_height, renderer_viewport.x, renderer_viewport.y, renderer_viewport.w,
           renderer_viewport.h);
    printf("SDL transparent presentation active: %s renderer\n",
           (renderer_info.flags & SDL_RENDERER_ACCELERATED) != 0 ? "accelerated" : "software");

    display->should_stop_for_key = should_stop_for_key;
    return 0;
}

int bench_display_get_viewport(const BenchDisplay *display, int *width, int *height) {
    if (display == NULL || display->window == NULL || width == NULL || height == NULL || display->viewport_width <= 0 ||
        display->viewport_height <= 0) {
        return -1;
    }
    *width = display->viewport_width;
    *height = display->viewport_height;
    return 0;
}

BenchEventPumpResult bench_display_service(void *ctx) {
    BenchDisplay *display = ctx;
    if (display == NULL || display->window == NULL || display->renderer == NULL) {
        fprintf(stderr, "bench_display_service: display surface is unavailable\n");
        return BENCH_EVENT_PUMP_ERROR;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return BENCH_EVENT_PUMP_STOP;
        }
        if (event.type == SDL_KEYDOWN && display->should_stop_for_key != NULL &&
            display->should_stop_for_key(&event.key)) {
            return BENCH_EVENT_PUMP_STOP;
        }
    }

    if (present_transparent(display) != 0) {
        return BENCH_EVENT_PUMP_ERROR;
    }
    return BENCH_EVENT_PUMP_CONTINUE;
}

void bench_display_destroy_surface(BenchDisplay *display) {
    if (display == NULL) {
        return;
    }
    if (display->renderer != NULL) {
        SDL_DestroyRenderer(display->renderer);
        display->renderer = NULL;
    }
    if (display->window != NULL) {
        SDL_DestroyWindow(display->window);
        display->window = NULL;
    }
    display->should_stop_for_key = NULL;
    display->viewport_width = 0;
    display->viewport_height = 0;
}

void bench_display_quit_sdl(BenchDisplay *display) {
    if (display == NULL) {
        return;
    }
    bench_display_destroy_surface(display);
    if (display->sdl_ready) {
        SDL_Quit();
        display->sdl_ready = false;
    }
}
