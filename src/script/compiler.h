#pragma once
#include <string>
#include <vector>

#include "script/parser.h"
#include "vm/cmd.h"

// Compilador: resuelve etiquetas a pc, interna nombres de actor/pose/fondo a IDs
// numericos, y arma el Cmd[] + pool de strings final (SPEC.md #9.3). Uso exclusivo de
// vne_bake: el runtime nunca ve este codigo (script_load.h lee el .vnc ya compilado).

struct CompiledLabel {
    u32 name_hash;
    u32 pc;
};

struct CompiledScriptData {
    std::vector<Cmd>           cmds;
    std::string                string_pool;  // bytes UTF-8 terminados en '\0'
    std::vector<CompiledLabel> labels;
    // Opciones de los comandos Choice del guion, en el orden en que se van generando;
    // Cmd::choice.first_option/option_count indexan un tramo contiguo aqui. Extension del
    // formato .vnc de SPEC.md #9.3 (que solo documenta Cmd[]/string_pool/Label[]): ver
    // ADR de M5 en docs/DECISIONS.md.
    std::vector<ChoiceOption>  choice_options;
};

struct CompileError {
    std::string file;
    u32         line;
    std::string message;
};

struct CompileResult {
    CompiledScriptData        data;
    std::vector<CompileError> errors;
    bool                      ok() const { return errors.empty(); }
};

// Asume que `instructions` ya paso la validacion de parse_script (identificadores
// desconocidos ya reportados ahi). Aqui solo puede fallar por errores internos.
CompileResult compile_instructions(const std::vector<ParsedInstr>& instructions,
                                    const std::string&              file_name);

// Formato .vnc (SPEC.md #9.3, extendido en M5 con la tabla de ChoiceOption). Devuelve
// false si no se pudo escribir el archivo.
bool write_vnc(const std::string& path, const CompiledScriptData& data);
