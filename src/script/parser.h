#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "base/types.h"
#include "vm/cmd.h"

// Parser del DSL (SPEC.md #9, skill vne-script-dsl). M5 anade bloques con indentacion
// significativa (@if/@else/@end, @choice/@end) sobre el subconjunto lineal de M3
// (etiquetas, dialogo, @bg, @show, @hide, @wait, @jump, @end), mas @set/@add/@call/
// @return/@lua. @if se traduce aqui mismo a JumpIf+Jump+etiquetas sinteticas (no existe
// un CmdKind::If: SPEC.md #8.1 no lo tiene), asi que el compilador sigue viendo una lista
// plana de instrucciones 1:1 con los Cmd finales, igual que en M3.

enum class InstrKind : u8 {
    Label,
    Say,
    Show,
    Hide,
    Bg,
    Wait,
    Jump,
    End,
    SetVar,
    AddVar,
    JumpIf,
    Choice,
    ChoiceEnd,
    Call,
    Return,
    Lua,
    Sfx,
    Bgm,
    StopBgm,
};

// Una condicion simple var-OP-valor (SPEC.md #9.1: "confianza >= 3", "valor > 2"). Usada
// tanto por @if (obligatoria) como por una opcion de @choice (opcional).
struct ParsedCondition {
    std::string var;
    CmpOp       op = CmpOp::Eq;
    i32         rhs = 0;
};

// Una opcion dentro de un bloque @choice: `"texto" [if var OP valor] -> etiqueta`.
struct ParsedChoiceOption {
    std::string      text;
    std::string      target;  // etiqueta destino
    bool             has_condition = false;
    ParsedCondition   condition;
};

struct ParsedInstr {
    InstrKind kind;
    u32         line = 0;
    std::string name;     // Label: nombre declarado; Jump/Call: etiqueta destino
    std::string speaker;  // Say: hablante (vacio = sin hablante)
    std::string text;     // Say: texto de dialogo; Lua: codigo fuente crudo
    std::string actor;    // Show: nombre de actor
    std::string pose;     // Show: nombre de pose
    std::string bg;       // Bg: nombre de fondo
    std::string sound;    // Sfx/Bgm: nombre logico del sonido/pista
    u8          slot    = 0;
    f32         fade    = 0.0f;
    f32         seconds = 0.0f;

    std::string     var;         // SetVar/AddVar: nombre de variable
    i32             value = 0;   // SetVar/AddVar: valor
    ParsedCondition condition;   // JumpIf: la condicion ya resuelta (var/op/rhs)
    bool            invert_condition = false;  // JumpIf: true = saltar si la condicion NO se cumple

    std::vector<ParsedChoiceOption> choice_options;  // Choice
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
