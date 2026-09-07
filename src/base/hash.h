#pragma once
#include <string_view>

#include "base/types.h"

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
