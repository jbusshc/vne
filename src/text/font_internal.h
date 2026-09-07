#pragma once
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include "base/handle.h"
#include "base/types.h"

// Uso exclusivo de src/text/*.cpp: resuelve un FontHandle a sus objetos FreeType/HarfBuzz.
// font.h se mantiene limpio de tipos de terceros, igual que texture.h con sokol_gfx.

struct FontData {
    FT_Face    ft_face  = nullptr;
    hb_font_t* hb_font  = nullptr;
    u32        px_size  = 0;
    f32        ascender = 0.0f;   // pixeles, desde la linea base hacia arriba
    f32        descender = 0.0f;  // pixeles, positivo, desde la linea base hacia abajo
    f32        line_height = 0.0f;
};

FontData* font_resolve(FontHandle h);
