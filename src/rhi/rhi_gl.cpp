#include "rhi/rhi.h"

#if defined(SZ_RHI_BACKEND_GL)

#include <SDL3/SDL.h>

#include "core/log.h"

// NOTA: este backend no se ha podido compilar ni probar en este entorno (no hay Linux
// disponible). Escrito siguiendo la API documentada de SDL3 y sokol_gfx; revisar en
// cuanto exista una maquina Linux real (ver docs/DECISIONS.md, "Pendientes observados").

namespace {
PlatformWindow* g_window     = nullptr;
SDL_GLContext   g_gl_context = nullptr;
}  // namespace

bool rhi_init(PlatformWindow* window) {
    g_window = window;

    g_gl_context = SDL_GL_CreateContext(window->sdl_window);
    if (g_gl_context == nullptr) {
        log_error("SDL_GL_CreateContext fallo: %s", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(window->sdl_window, g_gl_context)) {
        log_error("SDL_GL_MakeCurrent fallo: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    return true;
}

void rhi_shutdown() {
    if (g_gl_context != nullptr) {
        SDL_GL_DestroyContext(g_gl_context);
        g_gl_context = nullptr;
    }
    g_window = nullptr;
}

sg_environment rhi_environment() {
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    return env;
}

sg_swapchain rhi_begin_frame(i32 window_w, i32 window_h) {
    sg_swapchain sc{};
    sc.width          = window_w;
    sc.height         = window_h;
    sc.sample_count   = 1;
    sc.color_format   = SG_PIXELFORMAT_RGBA8;
    sc.depth_format   = SG_PIXELFORMAT_NONE;
    sc.gl.framebuffer = 0;
    return sc;
}

void rhi_present() {
    SDL_GL_SwapWindow(g_window->sdl_window);
}

bool rhi_capture_thumbnail(sg_image scene_image, u8* out_rgb, i32 out_w, i32 out_h) {
    // No implementado todavia (mismo hueco que el resto de este backend, ADR-0009: sin
    // Linux disponible aqui para escribirlo y probarlo con glReadPixels contra un FBO).
    // La pantalla de guardado se queda sin miniatura real en este backend hasta entonces
    // (ver docs/DECISIONS.md, M7).
    (void)scene_image;
    (void)out_rgb;
    (void)out_w;
    (void)out_h;
    log_error("rhi_capture_thumbnail: no implementado en el backend GL todavia");
    return false;
}

#endif  // SZ_RHI_BACKEND_GL
