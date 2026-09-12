#pragma once
#include "core/assert.h"
#include "core/handle.h"
#include "core/types.h"

// Pool de recursos: array contiguo de tamano fijo N mas una lista de indices libres.
// Resolver un handle es una comprobacion de rango, una comparacion de generacion y una
// indexacion. Sin hash maps, sin asignacion dinamica. El puntero devuelto por
// pool_resolve es valido solo dentro del ambito actual: nunca se guarda en un struct.

template <typename T, u32 N>
struct Pool {
    T   items[N];
    u32 gens[N];
    u32 free_list[N];
    u32 free_count;
};

template <typename T, u32 N>
void pool_init(Pool<T, N>* p) {
    p->free_count = N;
    for (u32 i = 0; i < N; ++i) {
        p->free_list[i] = N - 1 - i;
        p->gens[i]      = 1;
    }
}

template <typename Tag, typename T, u32 N>
Handle<Tag> pool_acquire(Pool<T, N>* p, T** out_item = nullptr) {
    if (p->free_count == 0) {
        return Handle<Tag>{};
    }
    p->free_count -= 1;
    u32 index = p->free_list[p->free_count];
    if (out_item != nullptr) {
        *out_item = &p->items[index];
    }
    return Handle<Tag>{index, p->gens[index]};
}

template <typename Tag, typename T, u32 N>
void pool_release(Pool<T, N>* p, Handle<Tag> h) {
    if (h.index >= N || p->gens[h.index] != h.gen) {
        return;
    }
    SZ_ASSERT(p->free_count < N, "pool: liberando mas slots de los que existen");
    p->gens[h.index] += 1;
    if (p->gens[h.index] == 0) {
        p->gens[h.index] = 1;  // gen 0 esta reservado para el handle nulo
    }
    p->free_list[p->free_count] = h.index;
    p->free_count += 1;
}

template <typename Tag, typename T, u32 N>
T* pool_resolve(Pool<T, N>* p, Handle<Tag> h) {
    if (h.index >= N || p->gens[h.index] != h.gen) {
        return nullptr;
    }
    return &p->items[h.index];
}
