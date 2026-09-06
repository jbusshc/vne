#include "platform/window.h"

#include <SDL3/SDL.h>

#include "base/log.h"

bool platform_window_create(PlatformWindow* out, const char* title, i32 width, i32 height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log_error("SDL_Init fallo: %s", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = 0;
#if defined(VNE_GFX_BACKEND_GL)
    // Los atributos de contexto GL deben fijarse antes de crear la ventana: el backend
    // GL (gfx_backend_gl.cpp) solo llama a SDL_GL_CreateContext despues, sobre esta
    // ventana ya marcada con SDL_WINDOW_OPENGL.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    flags |= SDL_WINDOW_OPENGL;
#endif
    flags |= SDL_WINDOW_RESIZABLE;

    SDL_Window* window = SDL_CreateWindow(title, width, height, flags);
    if (window == nullptr) {
        log_error("SDL_CreateWindow fallo: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    out->sdl_window = window;
    out->width      = width;
    out->height     = height;
    return true;
}

void platform_window_destroy(PlatformWindow* w) {
    SDL_DestroyWindow(w->sdl_window);
    SDL_Quit();
    *w = PlatformWindow{};
}

void platform_window_size_px(const PlatformWindow* w, i32* out_w, i32* out_h) {
    SDL_GetWindowSizeInPixels(w->sdl_window, out_w, out_h);
}
