#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "bench_feeder.h"

typedef bool (*BenchDisplayStopKeyFn)(const SDL_KeyboardEvent *event);

typedef struct BenchDisplay {
    SDL_Window *window;
    SDL_Renderer *renderer;
    BenchDisplayStopKeyFn should_stop_for_key;
    int viewport_width;
    int viewport_height;
    bool sdl_ready;
} BenchDisplay;

/**
 * Request an alpha-capable EGL surface and initialize SDL video.
 *
 * This is separate from surface creation to preserve the required
 * SDL_Init -> SS4S_Init -> window -> SS4S_PostInit order.
 */
int bench_display_init_sdl(BenchDisplay *display);

/** Create and configure the transparent fullscreen/windowed UI surface. */
int bench_display_create_surface(BenchDisplay *display, BenchDisplayStopKeyFn should_stop_for_key);

/** Return the window viewport passed to the decoder. */
int bench_display_get_viewport(const BenchDisplay *display, int *width, int *height);

/** Poll events and refresh the transparent UI surface. */
BenchEventPumpResult bench_display_service(void *ctx);

/** Destroy the SDL surface without shutting down SDL video. */
void bench_display_destroy_surface(BenchDisplay *display);

/** Destroy any remaining surface and shut down SDL video. */
void bench_display_quit_sdl(BenchDisplay *display);
