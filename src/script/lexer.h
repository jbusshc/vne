#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

// Uso exclusivo de herramientas offline (sz_bake): nunca se linka en sz_runtime (SPEC.md
// #9.3, "cero parsing en release"). std::string/std::vector son aceptables aqui porque
// este codigo jamas corre en el bucle de frame del juego (skill vne-memory-model).

struct SourceLine {
    u32              number;    // 1-based, para mensajes de error con SPEC.md #9.2
    u32              indent;    // nivel de indentacion (multiplos de 4 espacios), 0 en la raiz
    // Espacios al principio de la linea, sin dividir. Se guarda aparte del nivel para poder
    // distinguir "no indentaste" de "indentaste con 2 espacios en vez de 4" (M13): los dos
    // dan indent == 0 y antes producian el mismo mensaje generico, que no apuntaba a la
    // causa real.
    u32              leading_spaces;
    std::string_view text;      // sin comentario, sin espacios al principio/final ni de indentacion
};

// Divide `source` en lineas logicas: quita comentarios ('#' hasta fin de linea) y recorta
// espacios sobrantes. Las lineas vacias resultantes se omiten. `source` debe seguir vivo
// mientras se usen los string_view devueltos.
std::vector<SourceLine> lex_lines(std::string_view source);
