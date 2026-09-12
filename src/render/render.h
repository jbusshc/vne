#pragma once
#include "core/handle.h"
#include "core/types.h"

struct PlatformWindow;

// Batching de sprites por clave de ordenacion, con render target virtual 1920x1080 y
// letterbox (SPEC.md #7.1). gfx no sabe que es un personaje: solo maneja handles y
// rectangulos. sokol_gfx y el backend por plataforma quedan completamente detras de este
// archivo (ver rhi/rhi.h, uso interno de render.cpp).

constexpr i32 k_virtual_width  = 1920;
constexpr i32 k_virtual_height = 1080;

// Capas fijas (SPEC.md #7.1). No anadir capas nuevas sin registrarlas aqui.
enum class RenderLayer : u16 {
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

// Transiciones de pantalla completa (M12, capa Transition = 7). Enum propio de gfx y no
// el TransitionKind de formats/cmd.h a proposito: gfx es una capa POR DEBAJO de vm y no puede
// incluir nada suyo (SPEC.md #5). Quien traduce entre los dos es la capa de aplicacion,
// que ve ambas.
enum class RenderTransitionMask : u8 { Fade, Wipe, Dissolve };

// Pide dibujar una transicion sobre TODO lo demas al final del frame. Es inmediata como
// render_draw_sprite: hay que volver a llamarla cada frame que la transicion siga activa
// (render_begin_frame la limpia). threshold va de 0 (nada cubierto) a 1 (todo cubierto);
// color es el tinte RGBA8 premultiplicado con el que se cubre (normalmente negro opaco).
//
// Cuesta exactamente una draw call extra, sea cual sea la mascara: las tres comparten
// shader y solo cambian la textura de mascara y el sharpness (ver render/shaders.h).
void render_draw_transition(RenderTransitionMask mask, f32 threshold, u32 color);

[[nodiscard]] bool render_init(PlatformWindow* window);
void               render_shutdown();

void render_begin_frame();
void render_draw_sprite(const Sprite& s);
void render_flush();                              // ordena y dibuja en el render target virtual
void render_present(i32 window_w, i32 window_h);  // letterbox + intercambio de buffer

// Funcion pura, sin estado de GPU: rectangulo centrado que preserva el aspecto de
// virtual_w x virtual_h dentro de una ventana de window_w x window_h. Expuesta para que
// los tests la verifiquen sin necesitar un contexto grafico real.
Recti render_letterbox_rect(i32 window_w, i32 window_h, i32 virtual_w, i32 virtual_h);

// Convierte un punto en pixeles de ventana a coordenadas de la resolucion virtual
// 1920x1080, deshaciendo el letterbox. Es la OTRA de las dos unicas funciones que conocen
// el tamano real de la ventana (la primera es render_present, skill vne-rendering): ninguna
// logica de juego lo conoce, y por eso la conversion se hace una vez en main.cpp y los
// modos reciben ya coordenadas virtuales.
//
// Un punto sobre las barras negras cae FUERA de [0,1920]x[0,1080], deliberadamente: asi
// ningun rectangulo de UI lo contiene y no hace falta un booleano "esta dentro" aparte.
void render_window_to_virtual(i32 window_w, i32 window_h, f32 window_x, f32 window_y,
                            f32* out_virtual_x, f32* out_virtual_y);

// Contador de draw calls del frame actual, para el HUD de depuracion. Se resetea en
// render_begin_frame (el blit de letterbox de render_present tambien cuenta).
extern u32 g_render_draw_call_count;

// Sprites encolados en el frame que se acaba de dibujar. Hasta M13 el HUD imprimia en su
// lugar la constante del banco de pruebas, asi que la etiqueta "sprites" decia siempre 5000
// pasara lo que pasara: un contador falso es peor que ninguno.
extern u32 g_render_sprite_count_last_frame;

// Numero de frame, incrementado por render_begin_frame. sokol_gfx solo admite UNA llamada a
// sg_update_image por imagen y por frame: pasarse dispara su propio assert y aborta el
// proceso. El cache de glifos ya respetaba esa regla desde M2 posponiendo la subida;
// texture_update_dynamic no, y por eso abrir dos veces el panel de guardado dentro del mismo
// frame mataba el juego (M15).
extern u32 g_render_frame_index;

// Punto de enganche opcional para el editor (SPEC.md #6.5: "editor_render() -> solo si
// SZ_EDITOR", entre el blit de letterbox y el intercambio de buffer). render.cpp (sz_engine,
// siempre compilado) no puede llamar a editor_render() directamente: src/editor/ ni
// siquiera se compila en Ship (ver CMakeLists.txt). main.cpp asigna este puntero a
// editor_render solo quien lo tenga disponible, dentro de su propio `#if defined
// (SZ_EDITOR)`; si queda en nullptr (Debug/Ship), render_present() simplemente no lo llama.
extern void (*g_editor_render_hook)();

// Textura 1x1 blanca, creada una sola vez (perezosamente) y cacheada: para paneles de UI
// solidos (M7, cuadro de dialogo/menus/backlog) dibujados como un Sprite con esta
// textura y el color deseado como tinte, sin necesitar un atlas real todavia. Un handle
// invalido (por defecto) resolveria al placeholder magenta (SPEC.md #7.4), que es para
// assets rotos, no para UI solida — por eso existe esto en vez de dejar Sprite::tex sin
// poner.
TextureHandle render_white_texture();

// Miniatura del render target de escena actual, reducida a out_w x out_h RGB8 (para la
// pantalla de guardado, SPEC.md #8.3, M7). Solo disponible en el backend D3D11 por ahora
// (ADR-0009): devuelve false en GL, dejando *out_rgb sin tocar.
bool render_capture_thumbnail(u8* out_rgb, i32 out_w, i32 out_h);
