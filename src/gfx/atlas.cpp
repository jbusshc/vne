#include "gfx/atlas.h"

#include <cstring>

#include "assets/pak.h"
#include "base/hash.h"
#include "base/log.h"

namespace {

constexpr u32 k_atlas_bin_magic   = 0x54414E56u;  // 'VNAT', ver tools/bake/main.cpp
constexpr u32 k_atlas_bin_version = 3;

// Mismo layout binario que AtlasEntry en tools/bake/main.cpp, sin compartir header porque
// uno es runtime y el otro una herramienta offline (mismo patron que el resto de formatos
// del proyecto). El relleno es explicito (ADR-0028).
struct AtlasEntry {
    u64 name_hash;
    u32 name_offset;
    u16 x, y, w, h;
    u8  _pad[4];
};
static_assert(sizeof(AtlasEntry) == 24);

const AtlasEntry* g_entries   = nullptr;
const char*       g_name_pool = nullptr;
u32               g_count     = 0;

}  // namespace

bool atlas_load(Arena* arena) {
    g_entries   = nullptr;
    g_name_pool = nullptr;
    g_count     = 0;

    const u8* bytes = nullptr;
    usize     size  = 0;
    if (!pak_resolve_into_arena("atlas_00.bin", arena, &bytes, &size)) {
        log_error("atlas_load: no se encontro 'atlas_00.bin'; ejecuta vne_bake primero");
        return false;
    }

    constexpr usize k_header_size = sizeof(u32) * 6;
    if (size < k_header_size) {
        log_error("atlas_load: 'atlas_00.bin' truncado (cabecera incompleta)");
        return false;
    }
    u32 header[6];
    std::memcpy(header, bytes, k_header_size);
    if (header[0] != k_atlas_bin_magic) {
        log_error("atlas_load: 'atlas_00.bin' no es un atlas valido (magic)");
        return false;
    }
    if (header[1] != k_atlas_bin_version) {
        // Se rechaza, no se migra: el atlas se regenera siempre desde assets_src/ con
        // vne_bake, igual que el .vnc desde el .vns. Solo los datos del jugador
        // (.vnsave) merecen una funcion de migracion.
        log_error("atlas_load: 'atlas_00.bin' es version %u, se esperaba %u; vuelve a "
                  "ejecutar vne_bake",
                  header[1], k_atlas_bin_version);
        return false;
    }

    u32   count          = header[4];
    u32   name_pool_size = header[5];
    usize needed = k_header_size + static_cast<usize>(count) * sizeof(AtlasEntry) + name_pool_size;
    if (size < needed) {
        log_error("atlas_load: 'atlas_00.bin' truncado (%zu bytes, se esperaban %zu)", size,
                  needed);
        return false;
    }

    g_entries   = reinterpret_cast<const AtlasEntry*>(bytes + k_header_size);
    g_name_pool = reinterpret_cast<const char*>(bytes + k_header_size +
                                                 static_cast<usize>(count) * sizeof(AtlasEntry));
    g_count     = count;
    return true;
}

bool atlas_find(const char* logical_name, AtlasSprite* out) {
    if (g_entries == nullptr || logical_name == nullptr) {
        return false;
    }
    u64 hash = fnv1a_u64(logical_name);

    // Biseccion sobre la tabla ordenada por hash (la ordena vne_bake al escribirla).
    u32 lo = 0, hi = g_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (g_entries[mid].name_hash < hash) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // Puede haber varias entradas con el mismo hash si dos nombres colisionan, asi que se
    // recorre el tramo y se compara el nombre de verdad: una colision no puede devolver el
    // sprite equivocado en silencio (es justo el fallo que M13 persigue en otros sitios).
    for (u32 i = lo; i < g_count && g_entries[i].name_hash == hash; ++i) {
        if (std::strcmp(g_name_pool + g_entries[i].name_offset, logical_name) == 0) {
            *out = AtlasSprite{g_entries[i].x, g_entries[i].y, g_entries[i].w, g_entries[i].h};
            return true;
        }
    }
    return false;
}

u32 atlas_sprite_count() {
    return g_count;
}

bool atlas_sprite_at(u32 index, AtlasSprite* out) {
    if (g_entries == nullptr || index >= g_count) {
        return false;
    }
    *out = AtlasSprite{g_entries[index].x, g_entries[index].y, g_entries[index].w,
                       g_entries[index].h};
    return true;
}
