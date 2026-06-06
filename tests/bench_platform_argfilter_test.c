#include "bench_platform.h"
#include "bench_platform_internal.h"

#include <assert.h>
#include <stddef.h>

static void assert_stop_keycode(SDL_Keycode keycode) {
    SDL_KeyboardEvent event = {
        .state = SDL_PRESSED,
        .keysym = {.sym = keycode},
    };

    assert(bench_platform_should_stop_for_key(&event));
}

static void assert_stop_scancode(SDL_Scancode scancode) {
    SDL_KeyboardEvent event = {
        .state = SDL_PRESSED,
        .keysym = {.sym = SDLK_UNKNOWN, .scancode = scancode},
    };

    assert(bench_platform_should_stop_for_key(&event));
}

int main(void) {
    assert(bench_platform_option_expects_value("--run-seconds"));
    assert(bench_platform_option_expects_value("--source-buffer-mib"));
    assert(bench_platform_option_expects_value("--suite"));
    assert(bench_platform_option_expects_value("--file"));
    assert(bench_platform_option_expects_value("--fps"));
    assert(bench_platform_option_expects_value("--dir"));
    assert(bench_platform_option_expects_value("--results-dir"));

    assert(!bench_platform_option_expects_value("--unknown"));
    assert(!bench_platform_option_expects_value(NULL));

    assert_stop_keycode(SDLK_ESCAPE);
    assert_stop_keycode(SDLK_AC_BACK);
    assert_stop_keycode(SDLK_EJECT);

    assert_stop_scancode(SDL_SCANCODE_AC_BACK);
    assert_stop_scancode(SDL_SCANCODE_EJECT);
    assert_stop_scancode((SDL_Scancode)SDL_WEBOS_SCANCODE_BACK);
    assert_stop_scancode((SDL_Scancode)SDL_WEBOS_SCANCODE_EXIT);

    SDL_KeyboardEvent released_escape = {
        .state = SDL_RELEASED,
        .keysym = {.sym = SDLK_ESCAPE},
    };
    assert(!bench_platform_should_stop_for_key(&released_escape));
    assert(!bench_platform_should_stop_for_key(NULL));

    return 0;
}
