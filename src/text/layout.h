#pragma once
#include <string_view>

#include "base/arena.h"
#include "base/handle.h"
#include "base/types.h"

// Layout de texto: shaping con HarfBuzz, word-wrap latino (por espacios) y CJK (kinsoku
// basico), marcado inline y furigana (SPEC.md #7.2). text_layout() es la unica funcion
// cara de este modulo; text_draw() solo recorre quads ya calculados (regla de la maquina
// de escribir: nunca relayoutear por frame).
//
// Extiende el GlyphQuad ilustrativo de SPEC.md #7.2 con `atlas_page` (el atlas tiene
// varias paginas, SPEC.md #7.2) y `color`/`is_ruby` (marcado inline {color=...} y
// furigana). Ver docs/DECISIONS.md, hito M2.
struct GlyphQuad {
    f32           x, y, w, h;      // espacio virtual, origen arriba-izquierda
    f32           u0, v0, u1, v1;  // UV dentro de atlas_page
    TextureHandle atlas_page;
    u32           color;
    bool          is_ruby;
};

struct TextLayout {
    GlyphQuad* quads      = nullptr;
    u32        count      = 0;
    f32        width      = 0.0f;
    f32        height     = 0.0f;
    u32        line_count = 0;
};

// Marcado soportado en utf8: {b}/{/b}, {color=#rrggbb}/{/color}, {ruby=..}/{/ruby}.
// {w=n} y {speed=n} se reconocen y se descartan sin afectar al layout: son ordenes de
// temporizacion para el efecto de maquina de escribir, que conduce la VM (M7), no este
// modulo (ver docs/DECISIONS.md).
TextLayout text_layout(FontHandle font, std::string_view utf8, f32 max_width, Arena* arena,
                        u32 base_color = 0xFFFFFFFFu);

// Dibuja los primeros visible_glyphs quads de l (capa DialogueText). No relayoutea nada:
// es seguro llamarlo todos los frames mientras avanza el efecto de maquina de escribir.
void text_draw(const TextLayout& l, f32 x, f32 y, u32 visible_glyphs);

// Contador de llamadas a text_layout, para verificar en tests/HUD que el efecto de
// maquina de escribir (que solo cambia visible_glyphs) nunca relayoutea (SPEC.md #12).
extern u32 g_text_layout_call_count;

// Tabla de kinsoku basico (SPEC.md #7.2), expuesta para poder testearla directamente sin
// depender de agrupar quads por linea.
bool text_is_kinsoku_forbidden_start(u32 codepoint);
bool text_is_kinsoku_forbidden_end(u32 codepoint);
