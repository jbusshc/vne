#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "base/types.h"

// Parser del subconjunto de M3 del DSL (SPEC.md #9, skill vne-script-dsl): etiquetas,
// dialogo, @bg, @show, @hide, @wait, @jump, @end. Sin @if/@choice todavia (M5): la
// indentacion de 4 espacios no importa hasta entonces, asi que este parser trabaja
// linea a linea, sin bloques.

enum class InstrKind : u8 { Label, Say, Show, Hide, Bg, Wait, Jump, End };

struct ParsedInstr {
    InstrKind kind;
    u32         line = 0;
    std::string name;     // Label: nombre declarado; Jump: etiqueta destino
    std::string speaker;  // Say: hablante (vacio = sin hablante)
    std::string text;     // Say: texto de dialogo, con su marcado inline sin interpretar
    std::string actor;    // Show: nombre de actor
    std::string pose;     // Show: nombre de pose
    std::string bg;       // Bg: nombre de fondo
    u8          slot    = 0;
    f32         fade    = 0.0f;
    f32         seconds = 0.0f;
};

struct ParseError {
    std::string file;
    u32         line;
    std::string message;
};

struct ParseResult {
    std::vector<ParsedInstr> instructions;
    std::vector<ParseError>  errors;
    bool                     ok() const { return errors.empty(); }
};

// file_name solo se usa para los mensajes de error (SPEC.md #9.2: "archivo y linea").
ParseResult parse_script(std::string_view source, const std::string& file_name);
