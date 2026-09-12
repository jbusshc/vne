#include "game/map_catalog.h"

#include <cstdio>
#include <cstring>

#include "assets/pak.h"
#include "base/hash.h"
#include "base/log.h"
#include "platform/files.h"

namespace {

constexpr u32 k_max_maps = 64;
constexpr u32 k_max_name = 64;

struct MapEntry {
    u16  map_id = 0;
    char logical_name[k_max_name] = {};
};

MapEntry g_maps[k_max_maps];
u32      g_count = 0;

// Debe coincidir con tools/bake/main.cpp (duplicado a proposito, mismo patron que el resto
// de formatos de este proyecto: lo escribe una herramienta offline y lo lee el juego).
struct MapCatalogRecord {
    u16  map_id;
    u8   _pad[2];
    char logical_name[k_max_name];
};
static_assert(sizeof(MapCatalogRecord) == 68);

void add_entry(u16 map_id, const char* logical_name) {
    if (g_count >= k_max_maps) {
        return;
    }
    g_maps[g_count].map_id = map_id;
    std::snprintf(g_maps[g_count].logical_name, k_max_name, "%s", logical_name);
    g_count += 1;
}

void dir_callback(void* /*userdata*/, const char* name_no_ext, const char* full_name) {
    add_entry(map_catalog_id_of(name_no_ext), full_name);
}

}  // namespace

u16 map_catalog_id_of(const char* name_no_ext) {
    return static_cast<u16>(fnv1a_u32(name_no_ext) % 65536u);
}

void map_catalog_init() {
    g_count = 0;

    if (pak_is_packed()) {
        const u8* bytes = nullptr;
        usize     size  = 0;
        bool      owned = false;
        if (!pak_resolve("map_catalog.bin", &bytes, &size, &owned)) {
            log_error("map_catalog_init: 'map_catalog.bin' no esta en el pak montado");
            return;
        }
        u32                     count   = static_cast<u32>(size / sizeof(MapCatalogRecord));
        const MapCatalogRecord* records = reinterpret_cast<const MapCatalogRecord*>(bytes);
        for (u32 i = 0; i < count; ++i) {
            add_entry(records[i].map_id, records[i].logical_name);
        }
        pak_release(bytes, owned);
        return;
    }

    dir_list_by_extension("assets_baked", ".vnm", dir_callback, nullptr);
}

const char* map_catalog_resolve(u16 map_id) {
    for (u32 i = 0; i < g_count; ++i) {
        if (g_maps[i].map_id == map_id) {
            return g_maps[i].logical_name;
        }
    }
    return nullptr;
}

u32 map_catalog_count() {
    return g_count;
}
