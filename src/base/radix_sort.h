#pragma once
#include "base/arena.h"
#include "base/types.h"

// Radix sort LSD de 8 pasadas de 8 bits sobre una clave u64, estable, sin asignaciones de
// heap: el buffer temporal sale de la arena que se le pase (skill vne-memory-model). Usado
// por gfx.cpp para ordenar sprites por clave (layer<<48 | order<<32 | tex.index) antes de
// emitir draw calls (skill vne-rendering: nunca std::sort aqui).

struct RadixSortEntry {
    u64 key;
    u32 index;
};

void radix_sort_u64(RadixSortEntry* entries, u32 count, Arena* arena);
