#include "vm/symbols_load.h"

#include <cstring>

#include "vfs/pak.h"
#include "core/log.h"

namespace {

constexpr u32 k_magic      = 0x59534E56u;  // 'VNSY'
constexpr u32 k_version    = 1;
constexpr u32 k_kind_count = static_cast<u32>(SymKind::Count);

// Punteros al bloque residente, uno por nombre. No se copia ni un byte de texto: el .vnsym
// entero vive en la arena y esto solo indexa dentro de el.
//
// El array de punteros tambien sale de la arena, dimensionado a los nombres que HAY. Un
// array estatico al tope de cada clase serian 6 x 65535 punteros = mas de 3 MB de BSS para
// una tabla que hoy tiene quince entradas.
const char** g_names[k_kind_count] = {};
u32          g_counts[k_kind_count] = {};
bool         g_loaded               = false;

}  // namespace

bool symbols_load(Arena* arena) {
    g_loaded = false;
    for (u32 k = 0; k < k_kind_count; ++k) {
        g_counts[k] = 0;
    }

    const u8* bytes = nullptr;
    usize     size  = 0;
    if (!pak_resolve_into_arena("project.vnsym", arena, &bytes, &size)) {
        log_error("symbols_load: no se encontro 'project.vnsym'; ejecuta 'sz_bake symbols'");
        return false;
    }

    usize header = sizeof(u32) * (2 + k_kind_count);
    if (size < header) {
        log_error("symbols_load: 'project.vnsym' truncado (cabecera)");
        return false;
    }
    u32 magic = 0, version = 0;
    std::memcpy(&magic, bytes, sizeof(u32));
    std::memcpy(&version, bytes + sizeof(u32), sizeof(u32));
    if (magic != k_magic) {
        log_error("symbols_load: 'project.vnsym' no es una tabla valida (magic)");
        return false;
    }
    if (version != k_version) {
        // Se rechaza, no se migra: es un artefacto generado desde los guiones, como el .vnc.
        log_error("symbols_load: 'project.vnsym' es version %u, se esperaba %u; vuelve a "
                  "ejecutar 'sz_bake symbols'",
                  version, k_version);
        return false;
    }

    u32 counts[k_kind_count];
    std::memcpy(counts, bytes + 2 * sizeof(u32), sizeof(counts));

    u32 total = 0;
    for (u32 k = 0; k < k_kind_count; ++k) {
        total += counts[k];
    }
    const char** pointers = total > 0 ? arena_alloc_n<const char*>(arena, total) : nullptr;
    if (total > 0 && pointers == nullptr) {
        log_error("symbols_load: sin espacio en la arena para %u nombres", total);
        return false;
    }

    usize offset = header;
    u32   cursor = 0;
    for (u32 k = 0; k < k_kind_count; ++k) {
        g_names[k] = pointers + cursor;
        for (u32 i = 0; i < counts[k]; ++i) {
            if (offset >= size) {
                log_error("symbols_load: 'project.vnsym' truncado (nombres)");
                return false;
            }
            g_names[k][i] = reinterpret_cast<const char*>(bytes + offset);
            offset += std::strlen(g_names[k][i]) + 1;
        }
        cursor += counts[k];
        g_counts[k] = counts[k];
    }

    g_loaded = true;
    return true;
}

const char* symbols_name(SymKind kind, u16 id) {
    u32 k = static_cast<u32>(kind);
    if (!g_loaded || id == 0 || k >= k_kind_count || id > g_counts[k]) {
        return "";
    }
    return g_names[k][id - 1];  // el id 0 esta reservado para "ninguno"
}

u16 symbols_id(SymKind kind, const char* name) {
    u32 k = static_cast<u32>(kind);
    if (!g_loaded || name == nullptr || k >= k_kind_count) {
        return 0;
    }
    for (u32 i = 0; i < g_counts[k]; ++i) {
        if (std::strcmp(g_names[k][i], name) == 0) {
            return static_cast<u16>(i + 1);
        }
    }
    return 0;
}

u32 symbols_count(SymKind kind) {
    u32 k = static_cast<u32>(kind);
    return k < k_kind_count ? g_counts[k] : 0;
}
