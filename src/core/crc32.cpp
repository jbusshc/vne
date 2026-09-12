#include "core/crc32.h"

namespace {

// Tabla generada una vez, en la primera llamada: evita un literal de 256 entradas escrito
// a mano y no cuenta contra la regla de cero asignaciones por frame (esto no corre nunca
// dentro del bucle de frame, solo al guardar/cargar).
const u32* crc32_table() {
    static u32 table[256];
    static bool initialized = false;
    if (!initialized) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (u32 bit = 0; bit < 8; ++bit) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    return table;
}

}  // namespace

u32 crc32(const void* data, usize size) {
    const u32* table = crc32_table();
    const u8*  bytes = static_cast<const u8*>(data);
    u32        c     = 0xFFFFFFFFu;
    for (usize i = 0; i < size; ++i) {
        c = table[(c ^ bytes[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}
