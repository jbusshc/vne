#pragma once
#include "base/handle.h"
#include "base/types.h"

// Cache de glifos en un atlas dinamico de paginas 1024x1024 (SPEC.md #7.2). El latin se
// rasteriza bajo demanda igual que el CJK: no hay horneado offline en M2 (ver
// docs/DECISIONS.md); la diferencia practica es que los glifos latinos de un parrafo se
// cachean todos la primera vez que aparecen y no vuelven a costar nada despues.

struct GlyphInfo {
    f32           u0, v0, u1, v1;        // UV dentro de la pagina del atlas
    f32           width, height;         // tamano del bitmap en pixeles
    f32           bearing_x, bearing_y;  // offset desde el origen de pluma; bearing_y hacia arriba
    TextureHandle atlas_page;
};

void glyph_cache_init();
void glyph_cache_shutdown();

// glyph_index es un indice de glifo ya resuelto por HarfBuzz, no un codepoint Unicode.
// Devuelve nullptr solo si todas las paginas del atlas estan llenas.
const GlyphInfo* glyph_cache_get(FontHandle font, u32 glyph_index);

// Llamar una vez al principio de cada frame, junto a gfx_begin_frame(). Habilita el
// siguiente glyph_cache_flush_dirty_pages() de este frame (ver mas abajo).
void glyph_cache_begin_frame();

// Sube a la GPU las paginas que cambiaron desde el ultimo flush. Debe llamarse como mucho
// una vez por frame (despues de todos los text_layout()/text_draw() del frame y antes de
// gfx_flush()): sg_update_image de sokol_gfx solo admite una subida por imagen y por
// frame. Si se llama mas de una vez en el mismo frame, las llamadas de mas se ignoran (se
// registra un aviso) en vez de crashear: las paginas siguen "dirty" y se suben en el
// siguiente frame (ADR-0016 / ADR-0019, docs/DECISIONS.md).
void glyph_cache_flush_dirty_pages();
