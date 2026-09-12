#pragma once
#include <sokol_gfx.h>

#include "core/types.h"
#include "platform/window.h"

// Contexto grafico especifico de cada plataforma (SPEC.md #2). sokol_gfx no crea ni
// posee la ventana ni el dispositivo/swapchain: alguien tiene que entregarselos. Sin
// sokol_app en el stack, ese "alguien" es este modulo, implementado una vez por backend
// (rhi_d3d11.cpp en Windows, rhi_gl.cpp en Linux; ver ADR-0009 para el
// hueco de macOS/Metal). Uso exclusivo de render.cpp.

[[nodiscard]] bool rhi_init(PlatformWindow* window);
void               rhi_shutdown();

// Se llama una vez al arrancar para construir sg_desc.environment.
sg_environment rhi_environment();

// Se llama al empezar cada frame: recrea las vistas del backbuffer si la ventana cambio
// de tamano y devuelve el swapchain listo para sg_begin_pass().
sg_swapchain rhi_begin_frame(i32 window_w, i32 window_h);

// Intercambia el backbuffer. Se llama al final de cada frame, despues de sg_commit().
void rhi_present();

// Lee de vuelta el render target de escena (k_virtual_width x k_virtual_height, RGBA8) y
// lo reduce a out_w x out_h RGB8 (sin canal alfa: la miniatura del guardado no lo
// necesita) con muestreo por vecino mas cercano, escribiendo out_w*out_h*3 bytes en
// out_rgb. Para la miniatura de la pantalla de guardado (SPEC.md #8.3, M7). sokol_gfx
// clasico (la version pineada, ver ADR de M1) no tiene una API de lectura portable, asi
// que cada backend lo implementa con su propio mecanismo (D3D11: textura de staging +
// CopyResource + Map). Devuelve false si el backend no lo soporta todavia (GL, ADR-0009).
bool rhi_capture_thumbnail(sg_image scene_image, u8* out_rgb, i32 out_w, i32 out_h);
