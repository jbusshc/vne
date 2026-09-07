#include "text/font.h"

#include <hb-ft.h>

#include "base/log.h"
#include "base/pool.h"
#include "text/font_internal.h"

namespace {

constexpr u32 k_max_fonts = 32;

Pool<FontData, k_max_fonts> g_pool;
FT_Library                  g_ft_library = nullptr;

}  // namespace

FontHandle text_load_font(const char* path, u32 px_size) {
    if (g_ft_library == nullptr) {
        if (FT_Init_FreeType(&g_ft_library) != 0) {
            log_error("text_load_font: FT_Init_FreeType fallo");
            return FontHandle{};
        }
        pool_init(&g_pool);
    }

    FT_Face face = nullptr;
    if (FT_New_Face(g_ft_library, path, 0, &face) != 0) {
        log_error("text_load_font: no se pudo abrir '%s'", path);
        return FontHandle{};
    }
    if (FT_Set_Pixel_Sizes(face, 0, px_size) != 0) {
        log_error("text_load_font: FT_Set_Pixel_Sizes(%u) fallo para '%s'", px_size, path);
        FT_Done_Face(face);
        return FontHandle{};
    }

    hb_font_t* hb_font = hb_ft_font_create_referenced(face);

    FontData* data = nullptr;
    FontHandle handle = pool_acquire<struct FontTag>(&g_pool, &data);
    if (!handle.valid()) {
        log_error("text_load_font: pool de fuentes lleno (%u)", k_max_fonts);
        hb_font_destroy(hb_font);
        FT_Done_Face(face);
        return FontHandle{};
    }

    data->ft_face  = face;
    data->hb_font  = hb_font;
    data->px_size  = px_size;
    // Metricas en 26.6 fixed point: dividir por 64 para pixeles.
    data->ascender    = static_cast<f32>(face->size->metrics.ascender) / 64.0f;
    data->descender   = static_cast<f32>(-face->size->metrics.descender) / 64.0f;
    data->line_height = static_cast<f32>(face->size->metrics.height) / 64.0f;

    return handle;
}

FontData* font_resolve(FontHandle h) {
    return pool_resolve<struct FontTag>(&g_pool, h);
}
