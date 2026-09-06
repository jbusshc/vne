#include "platform/window.h"

#include <SDL3/SDL.h>

#include "base/log.h"

bool platform_window_create(PlatformWindow* out, const char* title, i32 width, i32 height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_error("SDL_Init fallo: %s", SDL_GetError());
        return false;
    }

    SDL_Window* window = SDL_CreateWindow(title, width, height, 0);
    if (window == nullptr) {
        log_error("SDL_CreateWindow fallo: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        log_error("SDL_CreateRenderer fallo: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Vsync estabiliza el frame time al refresco del monitor; suficiente para el
    // criterio de 60 fps de M0 sin necesitar un limitador propio todavia.
    SDL_SetRenderVSync(renderer, 1);

    out->sdl_window   = window;
    out->sdl_renderer = renderer;
    out->width        = width;
    out->height       = height;
    return true;
}

void platform_window_destroy(PlatformWindow* w) {
    SDL_DestroyRenderer(w->sdl_renderer);
    SDL_DestroyWindow(w->sdl_window);
    SDL_Quit();
    *w = PlatformWindow{};
}

void platform_window_clear(PlatformWindow* w, u8 r, u8 g, u8 b) {
    SDL_SetRenderDrawColor(w->sdl_renderer, r, g, b, 255);
    SDL_RenderClear(w->sdl_renderer);
}

void platform_window_present(PlatformWindow* w) {
    SDL_RenderPresent(w->sdl_renderer);
}
