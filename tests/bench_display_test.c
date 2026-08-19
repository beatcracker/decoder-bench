#include "bench_display.h"

#include <assert.h>

static bool stop_for_escape(const SDL_KeyboardEvent *event) {
    return event != NULL && event->state == SDL_PRESSED && event->keysym.sym == SDLK_ESCAPE;
}

int main(void) {
    assert(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);

    BenchDisplay display;
    assert(bench_display_init_sdl(&display) == 0);
    assert(bench_display_create_surface(&display, stop_for_escape) == 0);

    int width = 0;
    int height = 0;
    assert(bench_display_get_viewport(&display, &width, &height) == 0);
    assert(width == 1280);
    assert(height == 720);
    assert(bench_display_service(&display) == BENCH_EVENT_PUMP_CONTINUE);

    SDL_Event quit_event = {.type = SDL_QUIT};
    assert(SDL_PushEvent(&quit_event) == 1);
    assert(bench_display_service(&display) == BENCH_EVENT_PUMP_STOP);

    bench_display_destroy_surface(&display);
    assert(bench_display_get_viewport(&display, &width, &height) == -1);
    bench_display_quit_sdl(&display);
    bench_display_quit_sdl(&display);
    return 0;
}
