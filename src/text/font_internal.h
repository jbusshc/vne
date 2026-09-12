#pragma once
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include "core/handle.h"
#include "core/types.h"

// Uso exclusivo de src/text/*.cpp: resuelve un FontHandle a sus objetos FreeType/HarfBuzz.
// font.h se mantiene limpio de tipos de terceros, igual que texture.h con sokol_gfx.

struct FontData {
    FT_Face    ft_face  = nullptr;
    hb_font_t* hb_font  = nullptr;
    u32        px_size  = 0;
    f32        ascender = 0.0f;   // pixeles, desde la linea base hacia arriba
    f32        descender = 0.0f;  // pixeles, positivo, desde la linea base hacia abajo
    f32        line_height = 0.0f;
    // M11: FT_New_Memory_Face() no copia el buffer que se le pasa, lo referencia mientras
    // la cara este viva (rasteriza glifos bajo demanda, ver glyph_cache.cpp). raw_bytes
    // tiene que sobrevivir tanto como ft_face. No hay un text_system_shutdown() que libere
    // fuentes individualmente hoy (ninguno lo hacia antes de M11 tampoco: viven todo el
    // proceso, igual que ft_face/hb_font, nunca se llama FT_Done_Face salvo en el camino
    // de fallo de la propia carga) -- raw_bytes_owned solo importa si alguna vez se anade
    // esa descarga.
    const u8*  raw_bytes       = nullptr;
    bool       raw_bytes_owned = false;
    // M12: negrita sintetica. No hay ningun TTF en negrita en assets_src/ttf/ (NotoSans
    // trae una sola variante), asi que {b} se dibuja engordando el contorno de la MISMA
    // cara con FT_GlyphSlot_Embolden al rasterizar. Es un FontHandle aparte, con su
    // propia cara, para que el cache de glifos (indexado por font.index) no mezcle los
    // glifos normales con los engordados.
    bool       synthetic_bold  = false;
};

FontData* font_resolve(FontHandle h);
