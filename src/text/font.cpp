#include "text/font.h"

#include <hb-ft.h>

#include "assets/pak.h"
#include "base/log.h"
#include "base/pool.h"
#include "text/font_internal.h"

namespace {

constexpr u32 k_max_fonts = 32;

Pool<FontData, k_max_fonts> g_pool;
FT_Library                  g_ft_library = nullptr;

}  // namespace

FontHandle text_load_font(const char* logical_name, u32 px_size) {
    if (g_ft_library == nullptr) {
        if (FT_Init_FreeType(&g_ft_library) != 0) {
            log_error("text_load_font: FT_Init_FreeType fallo");
            return FontHandle{};
        }
        pool_init(&g_pool);
    }

    // M11: ya no se abre directamente por ruta de archivo -- se resuelve a traves del
    // backend activo (directorio suelto o .pak, ver assets/pak.h), asi que una build Ship
    // funciona igual con los assets sueltos o empaquetados. Sincrono a proposito (no pasa
    // por el hilo de IO): a diferencia de una textura, no hay concepto de "fuente
    // placeholder" en este proyecto (ver el comentario de text_load_font en font.h,
    // "sin fuente no hay texto que dibujar"), y parsear metricas de un TTF es barato
    // comparado con decodificar y subir una textura a la GPU -- no hay tirón que evitar
    // diferiendo esto a otro hilo.
    const u8* bytes = nullptr;
    usize     size  = 0;
    bool      owned = false;
    if (!pak_resolve(logical_name, &bytes, &size, &owned)) {
        log_error("text_load_font: no se pudo abrir '%s'", logical_name);
        return FontHandle{};
    }

    FT_Face face = nullptr;
    if (FT_New_Memory_Face(g_ft_library, bytes, static_cast<FT_Long>(size), 0, &face) != 0) {
        log_error("text_load_font: '%s' no es un TTF valido", logical_name);
        pak_release(bytes, owned);
        return FontHandle{};
    }
    if (FT_Set_Pixel_Sizes(face, 0, px_size) != 0) {
        log_error("text_load_font: FT_Set_Pixel_Sizes(%u) fallo para '%s'", px_size,
                  logical_name);
        FT_Done_Face(face);
        pak_release(bytes, owned);
        return FontHandle{};
    }

    hb_font_t* hb_font = hb_ft_font_create_referenced(face);

    FontData* data = nullptr;
    FontHandle handle = pool_acquire<struct FontTag>(&g_pool, &data);
    if (!handle.valid()) {
        log_error("text_load_font: pool de fuentes lleno (%u)", k_max_fonts);
        hb_font_destroy(hb_font);
        FT_Done_Face(face);
        pak_release(bytes, owned);
        return FontHandle{};
    }

    data->ft_face  = face;
    data->hb_font  = hb_font;
    data->px_size  = px_size;
    // Metricas en 26.6 fixed point: dividir por 64 para pixeles.
    data->ascender    = static_cast<f32>(face->size->metrics.ascender) / 64.0f;
    data->descender   = static_cast<f32>(-face->size->metrics.descender) / 64.0f;
    data->line_height = static_cast<f32>(face->size->metrics.height) / 64.0f;
    // raw_bytes tiene que sobrevivir tanto como face (ver font_internal.h): NO se libera
    // aqui, a diferencia del resto de callers de pak_resolve.
    data->raw_bytes       = bytes;
    data->raw_bytes_owned = owned;

    return handle;
}

FontData* font_resolve(FontHandle h) {
    return pool_resolve<struct FontTag>(&g_pool, h);
}
