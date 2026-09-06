#pragma once
#include <sokol_gfx.h>

#include "base/types.h"
#include "platform/window.h"

// Contexto grafico especifico de cada plataforma (SPEC.md #2). sokol_gfx no crea ni
// posee la ventana ni el dispositivo/swapchain: alguien tiene que entregarselos. Sin
// sokol_app en el stack, ese "alguien" es este modulo, implementado una vez por backend
// (gfx_backend_d3d11.cpp en Windows, gfx_backend_gl.cpp en Linux; ver ADR-0009 para el
// hueco de macOS/Metal). Uso exclusivo de gfx.cpp.

[[nodiscard]] bool gfx_backend_init(PlatformWindow* window);
void               gfx_backend_shutdown();

// Se llama una vez al arrancar para construir sg_desc.environment.
sg_environment gfx_backend_environment();

// Se llama al empezar cada frame: recrea las vistas del backbuffer si la ventana cambio
// de tamano y devuelve el swapchain listo para sg_begin_pass().
sg_swapchain gfx_backend_begin_frame(i32 window_w, i32 window_h);

// Intercambia el backbuffer. Se llama al final de cada frame, despues de sg_commit().
void gfx_backend_present();
