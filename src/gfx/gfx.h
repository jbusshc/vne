#pragma once
#include "base/handle.h"
#include "base/types.h"

struct PlatformWindow;

// Batching de sprites por clave de ordenacion, con render target virtual 1920x1080 y
// letterbox (SPEC.md #7.1). gfx no sabe que es un personaje: solo maneja handles y
// rectangulos. sokol_gfx y el backend por plataforma quedan completamente detras de este
// archivo (ver gfx_backend.h, uso interno de gfx.cpp).

constexpr i32 k_virtual_width  = 1920;
constexpr i32 k_virtual_height = 1080;

// Capas fijas (SPEC.md #7.1). No anadir capas nuevas sin registrarlas aqui.
enum class GfxLayer : u16 {
    Background        = 0,
    BackgroundOverlay = 1,
    Actors            = 2,
    Foreground        = 3,
    DialogueBox       = 4,
    DialogueText      = 5,
    UI                = 6,
    Transition        = 7,
};

struct Sprite {
    TextureHandle tex;
    f32           src_x = 0.0f, src_y = 0.0f, src_w = 0.0f, src_h = 0.0f;  // pixeles en el atlas
    f32           dst_x = 0.0f, dst_y = 0.0f, dst_w = 0.0f, dst_h = 0.0f;  // espacio virtual
    f32           rotation = 0.0f;         // radianes, pivote en el centro
    u32           color    = 0xFFFFFFFFu;  // RGBA8 premultiplicado
    u16           layer    = 0;
    u16           order    = 0;
};

struct Recti {
    i32 x, y, w, h;
};

[[nodiscard]] bool gfx_init(PlatformWindow* window);
void               gfx_shutdown();

void gfx_begin_frame();
void gfx_draw_sprite(const Sprite& s);
void gfx_flush();                              // ordena y dibuja en el render target virtual
void gfx_present(i32 window_w, i32 window_h);  // letterbox + intercambio de buffer

// Funcion pura, sin estado de GPU: rectangulo centrado que preserva el aspecto de
// virtual_w x virtual_h dentro de una ventana de window_w x window_h. Expuesta para que
// los tests la verifiquen sin necesitar un contexto grafico real.
Recti gfx_letterbox_rect(i32 window_w, i32 window_h, i32 virtual_w, i32 virtual_h);

// Contador de draw calls del frame actual, para el HUD de depuracion. Se resetea en
// gfx_begin_frame (el blit de letterbox de gfx_present tambien cuenta).
extern u32 g_gfx_draw_call_count;
