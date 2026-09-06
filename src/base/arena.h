#pragma once
#include "base/types.h"

// Asignador lineal (bump allocator). Sin free individual: arena_reset descarta todo.
// Las tres instancias globales (g_arena_perm, g_arena_scene, g_arena_frame) se crean una
// vez en main y viven fuera de este archivo. Nunca guardes un puntero de g_arena_frame mas
// alla del frame en que se pidio.

struct Arena {
    u8*         base;
    usize       size;
    usize       used;
    const char* name;
};

Arena arena_create(usize size, const char* name);
void  arena_destroy(Arena* a);

// Devuelve nullptr y registra un error si la arena no tiene espacio. El llamante decide
// que hacer: esta funcion nunca hace crecer la arena.
void* arena_alloc(Arena* a, usize size, usize align = 16);

// En VN_DEBUG rellena la memoria liberada con 0xCD para detectar usos posteriores.
void arena_reset(Arena* a);

template <typename T>
T* arena_alloc_n(Arena* a, usize count) {
    return static_cast<T*>(arena_alloc(a, sizeof(T) * count, alignof(T)));
}
