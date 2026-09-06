#include "base/arena.h"

#include <cstdlib>
#include <cstring>

#include "base/log.h"

Arena g_arena_perm;
Arena g_arena_scene;
Arena g_arena_frame;

Arena arena_create(usize size, const char* name) {
    Arena a{};
    a.base = static_cast<u8*>(std::malloc(size));
    a.size = size;
    a.used = 0;
    a.name = name;
    if (a.base == nullptr) {
        log_error("arena '%s': fallo al reservar %zu bytes", name, size);
    }
    return a;
}

void arena_destroy(Arena* a) {
    std::free(a->base);
    a->base = nullptr;
    a->size = 0;
    a->used = 0;
}

void* arena_alloc(Arena* a, usize size, usize align) {
    usize aligned_used = (a->used + (align - 1)) & ~(align - 1);
    if (aligned_used + size > a->size) {
        log_error("arena '%s': sin espacio (pedido %zu, disponible %zu)", a->name, size,
                  a->size - a->used);
        return nullptr;
    }
    u8* ptr = a->base + aligned_used;
    a->used = aligned_used + size;
    return ptr;
}

void arena_reset(Arena* a) {
#if defined(VN_DEBUG)
    std::memset(a->base, 0xCD, a->used);
#endif
    a->used = 0;
}
