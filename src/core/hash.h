#pragma once
#include <string_view>

#include "core/types.h"

// FNV-1a de 32 bits. Determinista y estable entre compilador/plataforma (skill
// vne-script-dsl la usa ya para nombres de etiqueta; M5 la reutiliza para resolver
// nombres de variable/flag a un indice fijo por hash, sin necesitar una tabla de
// interning adicional en el .vnc — ver docs/DECISIONS.md).
constexpr u32 fnv1a_u32(std::string_view s) {
    u32 hash = 2166136261u;
    for (char c : s) {
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }
    return hash;
}

// FNV-1a de 64 bits. M11 la necesita para el name_hash de las entradas de game.pak
// (SPEC.md #11 lo fija en u64, a diferencia de las claves de var/flag/pista de musica/
// catalogo, que solo necesitan 32 bits porque su universo de nombres es mucho mas
// pequeno y viven dentro de un solo guion).
constexpr u64 fnv1a_u64(std::string_view s) {
    u64 hash = 14695981039346656037ull;
    for (char c : s) {
        hash ^= static_cast<u8>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}
