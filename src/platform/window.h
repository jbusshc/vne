#pragma once
#include "base/types.h"

struct SDL_Window;

// Ventana SDL3 pura: no sabe nada de sokol_gfx ni de D3D11/GL. El backend grafico
// (src/gfx/gfx_backend_*.cpp) extrae de aqui lo que necesita (HWND, contexto GL, ...).
struct PlatformWindow {
    SDL_Window* sdl_window = nullptr;
    i32         width      = 0;
    i32         height     = 0;
};

[[nodiscard]] bool platform_window_create(PlatformWindow* out, const char* title, i32 width,
                                           i32 height);
void platform_window_destroy(PlatformWindow* w);

// Tamano actual en pixeles (puede diferir del pedido en platform_window_create si el
// usuario redimensiono la ventana). Usado por gfx_present para el letterbox.
void platform_window_size_px(const PlatformWindow* w, i32* out_w, i32* out_h);
