#include "text/catalog.h"

#include <algorithm>
#include <cstring>

#include "vfs/pak.h"
#include "core/heap_guard.h"
#include "core/log.h"

namespace {
const u32*  g_keys          = nullptr;
const u32*  g_text_offsets  = nullptr;
u32         g_count         = 0;
const char* g_string_pool   = nullptr;
u32         g_generation    = 0;
}  // namespace

u32 catalog_generation() {
    return g_generation;
}

CatalogLoadResult catalog_load(const char* logical_name, Arena* arena) {
    catalog_clear();

    // M11: ya no abre directamente por ruta de archivo -- se resuelve a traves del
    // backend activo (directorio suelto o .pak, ver vfs/pak.h), mismo patron que
    // vm/script_load.cpp y MapMode::load.
    // Leer el archivo pasa por SDL, que asigna (13 veces, medido en M15 al reproducir la
    // sesion grabada que cambia de idioma). Misma excepcion acotada que ADR-0035: cargar un
    // asset bajo demanda, aqui disparado por el jugador al cambiar de idioma desde el menu.
    // Va aqui dentro y no en el llamante para que valga para todos por igual.
    const u8* bytes = nullptr;
    usize     size  = 0;
    heap_guard_suspend();
    bool found = pak_resolve_into_arena(logical_name, arena, &bytes, &size);
    heap_guard_resume();
    if (!found) {
        log_error("catalog_load: no se encontro '%s'", logical_name);
        return CatalogLoadResult::NotFound;
    }
    if (size < 4 * sizeof(u32)) {
        log_error("catalog_load: '%s' demasiado pequeno para ser un .vnl", logical_name);
        return CatalogLoadResult::BadFormat;
    }

    u32 header[4];
    std::memcpy(header, bytes, sizeof(header));
    if (header[0] != k_vnl_magic || header[1] != k_vnl_version) {
        log_error("catalog_load: '%s' no es un .vnl valido (magic/version)", logical_name);
        return CatalogLoadResult::BadFormat;
    }
    u32 count            = header[2];
    u32 string_pool_size = header[3];

    usize offset = 4 * sizeof(u32);
    g_keys = reinterpret_cast<const u32*>(bytes + offset);
    offset += static_cast<usize>(count) * sizeof(u32);

    g_text_offsets = reinterpret_cast<const u32*>(bytes + offset);
    offset += static_cast<usize>(count) * sizeof(u32);

    g_string_pool = reinterpret_cast<const char*>(bytes + offset);
    offset += string_pool_size;

    if (offset > size) {
        log_error("catalog_load: '%s' esta truncado", logical_name);
        catalog_clear();
        return CatalogLoadResult::BadFormat;
    }
    g_count = count;

    log_info("catalog_load: '%s' cargado (%u entradas)", logical_name, g_count);
    return CatalogLoadResult::Ok;
}

void catalog_clear() {
    g_keys         = nullptr;
    g_text_offsets = nullptr;
    g_count        = 0;
    g_string_pool  = nullptr;
    g_generation += 1;
}

const char* catalog_find(u32 key_hash) {
    if (g_count == 0) {
        return nullptr;
    }
    const u32* end = g_keys + g_count;
    const u32* it  = std::lower_bound(g_keys, end, key_hash);
    if (it == end || *it != key_hash) {
        return nullptr;
    }
    u32 index = static_cast<u32>(it - g_keys);
    return g_string_pool + g_text_offsets[index];
}
