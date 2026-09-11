#include "text/font.h"

#include <hb-ft.h>

#include <freetype/ftmodapi.h>  // FT_New_Library, FT_Add_Default_Modules

#include "assets/pak.h"
#include "base/heap_guard_hooks.h"
#include "base/log.h"
#include "base/pool.h"
#include "text/font_internal.h"

namespace {

constexpr u32 k_max_fonts = 32;

Pool<FontData, k_max_fonts> g_pool;
FT_Library                  g_ft_library = nullptr;

// Asignador de FreeType que pasa por el contador de heap_guard. Las firmas las fija
// FT_MemoryRec_ (freetype/ftsystem.h) y no coinciden con las de heap_guard_hooks.h:
// FT_Realloc recibe el tamano viejo y el nuevo, y FT_Alloc devuelve void*.
void* ft_alloc(FT_Memory /*memory*/, long size) {
    return heap_guard_malloc(static_cast<usize>(size), HeapSource::FreeType);
}

void ft_free(FT_Memory /*memory*/, void* block) {
    heap_guard_free(block);
}

void* ft_realloc(FT_Memory /*memory*/, long /*cur_size*/, long new_size, void* block) {
    return heap_guard_realloc(block, static_cast<usize>(new_size), HeapSource::FreeType);
}

FT_MemoryRec_ g_ft_memory_rec = {nullptr, ft_alloc, ft_free, ft_realloc};
FT_Memory     g_ft_memory     = &g_ft_memory_rec;

}  // namespace

FontHandle text_load_font(const char* logical_name, u32 px_size, bool bold) {
    if (g_ft_library == nullptr) {
        // FT_New_Library en vez de FT_Init_FreeType para poder pasarle un asignador que
        // cuente: FreeType asigna con malloc, que operator new no ve, y rasteriza glifos
        // CJK bajo demanda DENTRO del bucle de frame (ver glyph_cache.cpp). Sin esto esas
        // asignaciones eran invisibles para la regla de cero heap (SPEC.md #4).
        //
        // FT_New_Library no registra los modulos, a diferencia de FT_Init_FreeType: hay
        // que llamar a FT_Add_Default_Modules a mano o no habra ningun driver de fuentes.
        if (FT_New_Library(g_ft_memory, &g_ft_library) != 0) {
            log_error("text_load_font: FT_New_Library fallo");
            return FontHandle{};
        }
        FT_Add_Default_Modules(g_ft_library);
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
    data->synthetic_bold  = bold;

    return handle;
}

FontData* font_resolve(FontHandle h) {
    return pool_resolve<struct FontTag>(&g_pool, h);
}

bool text_reload_font(FontHandle handle, const char* logical_name, u32 px_size) {
    FontData* data = pool_resolve<struct FontTag>(&g_pool, handle);
    if (data == nullptr) {
        log_error("text_reload_font: handle invalido");
        return false;
    }

    // Construir la fuente nueva ANTES de tocar la vieja: si algo falla (archivo a medio
    // guardar, que es justo lo que puede pasar recargando en caliente), el juego se queda
    // con la que ya tenia en vez de sin ninguna.
    const u8* bytes = nullptr;
    usize     size  = 0;
    bool      owned = false;
    if (!pak_resolve(logical_name, &bytes, &size, &owned)) {
        log_error("text_reload_font: no se pudo abrir '%s'", logical_name);
        return false;
    }
    FT_Face new_face = nullptr;
    if (FT_New_Memory_Face(g_ft_library, bytes, static_cast<FT_Long>(size), 0, &new_face) != 0 ||
        FT_Set_Pixel_Sizes(new_face, 0, px_size) != 0) {
        log_error("text_reload_font: '%s' no se pudo releer, se conserva la anterior",
                  logical_name);
        if (new_face != nullptr) {
            FT_Done_Face(new_face);
        }
        pak_release(bytes, owned);
        return false;
    }
    hb_font_t* new_hb = hb_ft_font_create_referenced(new_face);

    // Ya hay reemplazo valido: ahora si se puede soltar lo viejo. El orden importa --
    // primero la cara (deja de referenciar el buffer), luego el buffer.
    hb_font_destroy(data->hb_font);
    FT_Done_Face(data->ft_face);
    pak_release(data->raw_bytes, data->raw_bytes_owned);

    data->ft_face         = new_face;
    data->hb_font         = new_hb;
    data->px_size         = px_size;
    data->ascender        = static_cast<f32>(new_face->size->metrics.ascender) / 64.0f;
    data->descender       = static_cast<f32>(-new_face->size->metrics.descender) / 64.0f;
    data->line_height     = static_cast<f32>(new_face->size->metrics.height) / 64.0f;
    data->raw_bytes       = bytes;
    data->raw_bytes_owned = owned;
    return true;
}
