#include "platform/window.h"

#include <SDL3/SDL.h>

#include "base/heap_guard_hooks.h"
#include "base/log.h"

bool platform_window_create(PlatformWindow* out, const char* title, i32 width, i32 height) {
    // Antes de SDL_Init a proposito: SDL no deja cambiar sus funciones de memoria una vez
    // ha asignado algo. Sin esto, todo lo que asigna SDL (eventos, que se bombean en cada
    // frame) seria invisible para la regla de cero heap (SPEC.md #4).
    heap_guard_install_sdl_hooks();

    // SDL asigna de forma perezosa dentro de su subsistema de eventos la primera vez que
    // se bombea la cola (medido en M12: 3 asignaciones, una sola vez en toda la vida del
    // proceso). Si eso cae dentro del bucle de frame, rompe la regla de cero heap por algo
    // que no es trabajo de frame sino inicializacion diferida de la libreria. Bombear aqui
    // la fuerza a ocurrir durante el arranque, donde asignar es legitimo. Ver ADR-0058.
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

    // Ver el comentario junto a SDL_Init: se vacia la cola aqui para que la inicializacion
    // diferida del subsistema de eventos (y los eventos de creacion/foco de la ventana,
    // que son los que la disparan) asigne durante el arranque y no dentro de un frame.
    SDL_Event drain;
    SDL_PumpEvents();
    while (SDL_PollEvent(&drain)) {
    }
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
