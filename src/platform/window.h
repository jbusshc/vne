#pragma once
#include "base/types.h"

struct SDL_Window;
struct SDL_Renderer;

// En M0 la ventana se limpia con el SDL_Renderer que trae SDL3, sin dependencias nuevas.
// Se descarta por completo en M1 cuando sokol_gfx toma el control del dibujado
// (docs/DECISIONS.md ADR-0006).
struct PlatformWindow {
    SDL_Window*   sdl_window   = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    i32           width        = 0;
    i32           height       = 0;
};

[[nodiscard]] bool platform_window_create(PlatformWindow* out, const char* title, i32 width,
                                           i32 height);
void platform_window_destroy(PlatformWindow* w);
void platform_window_clear(PlatformWindow* w, u8 r, u8 g, u8 b);
void platform_window_present(PlatformWindow* w);
