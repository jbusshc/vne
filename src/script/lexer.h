#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "base/types.h"

// Uso exclusivo de herramientas offline (vne_bake): nunca se linka en vne_game (SPEC.md
// #9.3, "cero parsing en release"). std::string/std::vector son aceptables aqui porque
// este codigo jamas corre en el bucle de frame del juego (skill vne-memory-model).

struct SourceLine {
    u32              number;  // 1-based, para mensajes de error con SPEC.md #9.2
    std::string_view text;    // sin comentario, sin espacios al principio/final
};

// Divide `source` en lineas logicas: quita comentarios ('#' hasta fin de linea) y recorta
// espacios sobrantes. Las lineas vacias resultantes se omiten. `source` debe seguir vivo
// mientras se usen los string_view devueltos.
std::vector<SourceLine> lex_lines(std::string_view source);
