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

// Formato .vnc (SPEC.md #9.3). Devuelve false si no se pudo escribir el archivo.
bool write_vnc(const std::string& path, const CompiledScriptData& data);
