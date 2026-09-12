#include "vfs/pak.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/arena.h"
#include "core/hash.h"
#include "core/log.h"
#include "platform/files.h"  // pak_mount: solo hilo principal, arena esta bien aqui

namespace {

// Debe coincidir con tools/bake/main.cpp (duplicado a proposito: uno es runtime, el otro
// una herramienta offline -- mismo patron que el magic de .vnc entre compiler.cpp y
// script_load.cpp, o el de .vnl entre sz_bake y text/catalog.h).
constexpr u32 k_pak_magic   = 0x4B504E56u;  // 'VNPK'
constexpr u32 k_pak_version = 1;

struct PakEntry {
    u64          name_hash;
    u64          offset;
    u32          size;
    PakEntryType type;
    u8           _pad[3];
};
static_assert(sizeof(PakEntry) == 24);

bool          g_packed = false;
char          g_root[512] = {};  // backend suelto: directorio raiz

const u8*     g_pak_data     = nullptr;  // backend empaquetado: bloque residente entero
const PakEntry* g_entries    = nullptr;  // ordenado por name_hash ascendente
u32           g_entry_count  = 0;

bool ends_with(const char* s, const char* suffix) {
    usize len = std::strlen(s), suf_len = std::strlen(suffix);
    return len >= suf_len && std::strcmp(s + (len - suf_len), suffix) == 0;
}

bool starts_with(const char* s, const char* prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// Backend suelto: assets_baked/ y assets_src/{ttf,ogg}/ son directorios hermanos
// distintos (assets_baked/ es lo que hornea sz_bake; los .ttf/.ogg no se hornean
// todavia, M13 para fuentes, copia directa desde M6 para audio -- ver SPEC.md #11), pero
// el espacio de nombres logico tiene que ser el MISMO tanto si esta montado un .pak como
// si esta montado un directorio (el resto del motor no sabe cual de los dos hay). Este
// mapeo replica exactamente el que hace bake_pack() en tools/bake/main.cpp: todo lo que
// no empiece por "ttf/" u "ogg/" viene de assets_baked/ tal cual; lo que empiece por esos
// dos prefijos viene de assets_src/ con el mismo prefijo.
void loose_full_path(const char* logical_path, char* out_path, usize out_path_cap) {
    if (starts_with(logical_path, "ttf/") || starts_with(logical_path, "ogg/")) {
        std::snprintf(out_path, out_path_cap, "%s/assets_src/%s", g_root, logical_path);
    } else {
        std::snprintf(out_path, out_path_cap, "%s/assets_baked/%s", g_root, logical_path);
    }
}

bool mount_packed(const char* pak_path) {
    u8*   data = nullptr;
    usize size = 0;
    if (!file_read_all(pak_path, &g_arena_perm, &data, &size)) {
        log_error("pak_mount: no se pudo leer '%s'", pak_path);
        return false;
    }
    if (size < 3 * sizeof(u32)) {
        log_error("pak_mount: '%s' demasiado pequeno para ser un .pak", pak_path);
        return false;
    }

    u32 header[3];
    std::memcpy(header, data, sizeof(header));
    if (header[0] != k_pak_magic || header[1] != k_pak_version) {
        log_error("pak_mount: '%s' no es un .pak valido (magic/version)", pak_path);
        return false;
    }
    u32   entry_count = header[2];
    usize entries_off = 3 * sizeof(u32);
    usize entries_size = static_cast<usize>(entry_count) * sizeof(PakEntry);
    if (entries_off + entries_size > size) {
        log_error("pak_mount: '%s' esta truncado (tabla de entradas)", pak_path);
        return false;
    }

    g_pak_data    = data;
    g_entries     = reinterpret_cast<const PakEntry*>(data + entries_off);
    g_entry_count = entry_count;
    g_packed      = true;
    log_info("pak_mount: '%s' montado (%u entradas)", pak_path, entry_count);
    return true;
}

}  // namespace

void pak_mount(const char* root) {
    pak_unmount();
    if (ends_with(root, ".pak")) {
        if (mount_packed(root)) {
            return;
        }
        // Si el .pak no se pudo montar, no hay backend suelto de respaldo razonable
        // (SPEC.md #4: un fallo de asset nunca es fatal, pero un backend entero sin
        // montar significa que TODO pak_resolve() posterior fallara y cada llamante
        // caera a su propio placeholder -- ya es el comportamiento correcto sin mas
        // logica aqui).
        return;
    }
    std::snprintf(g_root, sizeof(g_root), "%s", root);
    g_packed = false;
}

void pak_unmount() {
    g_packed      = false;
    g_pak_data    = nullptr;
    g_entries     = nullptr;
    g_entry_count = 0;
    g_root[0]     = '\0';
}

bool pak_is_packed() {
    return g_packed;
}

bool pak_resolve(const char* logical_path, const u8** out_data, usize* out_size,
                  bool* out_owned) {
    *out_data  = nullptr;
    *out_size  = 0;
    *out_owned = false;

    if (g_packed) {
        u64 hash = fnv1a_u64(logical_path);
        const PakEntry* end = g_entries + g_entry_count;
        const PakEntry* it  = std::lower_bound(
            g_entries, end, hash,
            [](const PakEntry& e, u64 h) { return e.name_hash < h; });
        if (it == end || it->name_hash != hash) {
            return false;
        }
        *out_data = g_pak_data + it->offset;
        *out_size = it->size;
        return true;
    }

    // SDL_LoadFile, no platform/files.h::file_read_all: esta funcion puede llamarse
    // desde el hilo de IO (M11), y las arenas del proyecto no son thread-safe (skill
    // vne-memory-model). SDL_LoadFile usa su propio allocador interno, ajeno por completo
    // a las arenas y a heap_guard, igual que hace miniaudio (base/heap_guard.h).
    char full_path[1024];
    loose_full_path(logical_path, full_path, sizeof(full_path));
    usize size    = 0;
    void* sdl_buf = SDL_LoadFile(full_path, &size);
    if (sdl_buf == nullptr) {
        return false;
    }
    *out_data  = static_cast<const u8*>(sdl_buf);
    *out_size  = size;
    *out_owned = true;
    return true;
}

bool pak_resolve_into_arena(const char* logical_path, Arena* arena, const u8** out_data,
                             usize* out_size) {
    const u8* bytes = nullptr;
    usize     size  = 0;
    bool      owned = false;
    if (!pak_resolve(logical_path, &bytes, &size, &owned)) {
        *out_data = nullptr;
        *out_size = 0;
        return false;
    }
    if (!owned) {
        // Backend empaquetado: ya es residente para siempre, no hay nada que copiar.
        *out_data = bytes;
        *out_size = size;
        return true;
    }
    u8* copy = arena_alloc_n<u8>(arena, size);
    if (copy == nullptr) {
        pak_release(bytes, owned);
        *out_data = nullptr;
        *out_size = 0;
        return false;
    }
    std::memcpy(copy, bytes, size);
    pak_release(bytes, owned);  // el buffer intermedio ya no hace falta, ya esta copiado
    *out_data = copy;
    *out_size = size;
    return true;
}

void pak_release(const u8* data, bool owned) {
    if (owned && data != nullptr) {
        SDL_free(const_cast<u8*>(data));
    }
}

bool pak_resolve_loose_path(const char* logical_path, char* out_path, usize out_path_cap) {
    if (g_packed) {
        return false;
    }
    loose_full_path(logical_path, out_path, out_path_cap);
    return true;
}
