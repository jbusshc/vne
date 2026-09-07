#include "text/glyph_cache.h"

#include <cstring>

#include "base/arena.h"
#include "base/assert.h"
#include "base/log.h"
#include "gfx/texture.h"
#include "text/font_internal.h"

namespace {

constexpr i32 k_page_size            = 1024;
constexpr u32 k_max_pages            = 4;
constexpr u32 k_glyph_table_capacity = 4096;  // potencia de 2, para el modulo con &
constexpr i32 k_glyph_padding        = 1;     // evita sangrado entre glifos vecinos

struct AtlasPage {
    TextureHandle tex;
    u8*           cpu_pixels = nullptr;  // k_page_size*k_page_size*4, RGBA con cobertura replicada
    i32           cursor_x    = 0;
    i32           cursor_y    = 0;
    i32           shelf_height = 0;
    bool          dirty       = false;
};

struct GlyphTableEntry {
    u64       key  = 0;
    bool      used = false;
    GlyphInfo info{};
};

AtlasPage       g_pages[k_max_pages];
u32             g_page_count = 0;
GlyphTableEntry g_table[k_glyph_table_capacity];
bool            g_flushed_this_frame = false;

// No incluye font.gen: no hay text_unload_font todavia (M2 no lo necesita), asi que un
// slot de fuente nunca se reutiliza para una fuente distinta durante la vida del proceso.
// Si eso cambia, esta clave tiene que incluir la generacion para no servir glifos de la
// fuente vieja.
u64 glyph_key(FontHandle font, u32 glyph_index) {
    return (static_cast<u64>(font.index) << 32) | static_cast<u64>(glyph_index);
}

GlyphTableEntry* table_find_slot(u64 key) {
    u32 index = static_cast<u32>(key) & (k_glyph_table_capacity - 1);
    for (u32 probe = 0; probe < k_glyph_table_capacity; ++probe) {
        GlyphTableEntry* entry = &g_table[(index + probe) & (k_glyph_table_capacity - 1)];
        if (!entry->used || entry->key == key) {
            return entry;
        }
    }
    return nullptr;  // tabla llena: no deberia pasar con la capacidad configurada
}

bool page_alloc() {
    if (g_page_count >= k_max_pages) {
        return false;
    }
    AtlasPage& page = g_pages[g_page_count];
    page.cpu_pixels =
        arena_alloc_n<u8>(&g_arena_perm, static_cast<usize>(k_page_size) * k_page_size * 4);
    std::memset(page.cpu_pixels, 0, static_cast<usize>(k_page_size) * k_page_size * 4);
    page.tex           = texture_create_dynamic(k_page_size, k_page_size);
    page.cursor_x       = 0;
    page.cursor_y       = 0;
    page.shelf_height   = 0;
    page.dirty          = false;
    g_page_count += 1;
    return true;
}

// Intenta encajar un rectangulo de w x h en alguna pagina existente o en una nueva
// (shelf packer: sin desempaquetado, sin reflow — SPEC.md #7.2 no pide un empaquetador
// optimo, solo paginas dinamicas). Devuelve el indice de pagina y las coordenadas de
// destino, o false si el atlas esta completamente lleno.
bool pack_rect(i32 w, i32 h, u32* out_page, i32* out_x, i32* out_y) {
    for (u32 attempt = 0; attempt < 2; ++attempt) {
        for (u32 p = 0; p < g_page_count; ++p) {
            AtlasPage& page = g_pages[p];
            if (page.cursor_x + w + k_glyph_padding > k_page_size) {
                page.cursor_y += page.shelf_height + k_glyph_padding;
                page.cursor_x     = 0;
                page.shelf_height = 0;
            }
            if (page.cursor_y + h + k_glyph_padding > k_page_size) {
                continue;  // esta pagina no tiene sitio; probar la siguiente
            }
            *out_page = p;
            *out_x    = page.cursor_x;
            *out_y    = page.cursor_y;
            page.cursor_x += w + k_glyph_padding;
            page.shelf_height = page.shelf_height > h ? page.shelf_height : h;
            return true;
        }
        if (!page_alloc()) {
            return false;
        }
    }
    return false;
}

}  // namespace

void glyph_cache_init() {
    g_page_count = 0;
    for (auto& entry : g_table) {
        entry.used = false;
    }
    page_alloc();
}

void glyph_cache_shutdown() {
    g_page_count = 0;
}

void glyph_cache_begin_frame() {
    g_flushed_this_frame = false;
}

const GlyphInfo* glyph_cache_get(FontHandle font, u32 glyph_index) {
    u64              key   = glyph_key(font, glyph_index);
    GlyphTableEntry* entry = table_find_slot(key);
    if (entry == nullptr) {
        log_error("glyph_cache_get: tabla de glifos llena");
        return nullptr;
    }
    if (entry->used) {
        return &entry->info;
    }

    FontData* font_data = font_resolve(font);
    if (font_data == nullptr) {
        return nullptr;
    }

    if (FT_Load_Glyph(font_data->ft_face, glyph_index, FT_LOAD_RENDER) != 0) {
        log_error("glyph_cache_get: FT_Load_Glyph fallo para el glifo %u", glyph_index);
        return nullptr;
    }
    FT_GlyphSlot slot   = font_data->ft_face->glyph;
    FT_Bitmap&   bitmap = slot->bitmap;

    GlyphInfo info{};
    info.width     = static_cast<f32>(bitmap.width);
    info.height    = static_cast<f32>(bitmap.rows);
    info.bearing_x = static_cast<f32>(slot->bitmap_left);
    info.bearing_y = static_cast<f32>(slot->bitmap_top);

    if (bitmap.width == 0 || bitmap.rows == 0) {
        // Glifos sin tinta (p. ej. el espacio): quad vacio valido, no ocupan atlas.
        info.u0 = info.v0 = info.u1 = info.v1 = 0.0f;
        info.atlas_page                       = TextureHandle{};
        entry->key                            = key;
        entry->used                           = true;
        entry->info                           = info;
        return &entry->info;
    }

    u32 page_index = 0;
    i32 px = 0, py = 0;
    if (!pack_rect(static_cast<i32>(bitmap.width), static_cast<i32>(bitmap.rows), &page_index,
                   &px, &py)) {
        log_error("glyph_cache_get: atlas de glifos lleno (%u paginas)", k_max_pages);
        return nullptr;
    }

    AtlasPage& page = g_pages[page_index];
    VN_ASSERT(px >= 0 && py >= 0, "glyph_cache: coordenadas de empaquetado negativas");
    VN_ASSERT(px + static_cast<i32>(bitmap.width) <= k_page_size,
              "glyph_cache: glifo se sale de la pagina en X");
    VN_ASSERT(py + static_cast<i32>(bitmap.rows) <= k_page_size,
              "glyph_cache: glifo se sale de la pagina en Y");
    for (u32 row = 0; row < bitmap.rows; ++row) {
        const u8* src_row =
            bitmap.buffer + static_cast<usize>(row) * static_cast<usize>(bitmap.pitch);
        usize     dst_row_index =
            (static_cast<usize>(static_cast<u32>(py) + row) * static_cast<usize>(k_page_size) +
             static_cast<usize>(px)) *
            4;
        u8* dst_row = &page.cpu_pixels[dst_row_index];
        for (u32 col = 0; col < bitmap.width; ++col) {
            u8  coverage  = src_row[col];
            u8* dst_pixel = &dst_row[col * 4];
            dst_pixel[0] = coverage;
            dst_pixel[1] = coverage;
            dst_pixel[2] = coverage;
            dst_pixel[3] = coverage;
        }
    }
    page.dirty = true;

    f32 page_size_f = static_cast<f32>(k_page_size);
    info.u0          = static_cast<f32>(px) / page_size_f;
    info.v0          = static_cast<f32>(py) / page_size_f;
    info.u1          = static_cast<f32>(px + static_cast<i32>(bitmap.width)) / page_size_f;
    info.v1          = static_cast<f32>(py + static_cast<i32>(bitmap.rows)) / page_size_f;
    info.atlas_page  = page.tex;

    entry->key  = key;
    entry->used = true;
    entry->info = info;

    return &entry->info;
}

void glyph_cache_flush_dirty_pages() {
    // sg_update_image de sokol_gfx solo admite una subida por imagen y por frame
    // (ADR-0016). Si esta funcion ya subio algo este frame (glyph_cache_begin_frame no se
    // ha vuelto a llamar desde entonces), la segunda llamada se ignora en vez de
    // crashear: las paginas siguen "dirty" y se suben en el siguiente frame. Un aviso en
    // el log, no un fallo fatal (ADR-0019, docs/DECISIONS.md).
    if (g_flushed_this_frame) {
        log_warn("glyph_cache_flush_dirty_pages: ya se subio algo este frame, se pospone al siguiente");
        return;
    }
    g_flushed_this_frame = true;

    for (u32 p = 0; p < g_page_count; ++p) {
        if (g_pages[p].dirty) {
            texture_update_dynamic(g_pages[p].tex, g_pages[p].cpu_pixels);
            g_pages[p].dirty = false;
        }
    }
}
