#pragma once
#include <string>
#include <vector>

#include "script/parser.h"
#include "script/symbols.h"
#include "vm/cmd.h"

// Compilador: resuelve etiquetas a pc, interna nombres de actor/pose/fondo a IDs
// numericos, y arma el Cmd[] + pool de strings final (SPEC.md #9.3). Uso exclusivo de
// vne_bake: el runtime nunca ve este codigo (script_load.h lee el .vnc ya compilado).

struct CompiledLabel {
    u32 name_hash;
    u32 pc;
};

// Una entrada del catalogo de localizacion (SPEC.md #9.2: "clave estable
// archivo:linea:hash"). No es parte del .vnc: vne_bake la escribe aparte, al catalogo de
// extraccion (M10, ver ADR-0046 en docs/DECISIONS.md).
struct CatalogEntry {
    std::string key;   // "archivo:linea:hash_hex"
    std::string text;  // texto original (espanol, el idioma en que se autoran los guiones)
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
    // Todo texto de dialogo (Say y opciones de Choice), para la extraccion de catalogo
    // de M10. No se escribe en el .vnc: tools/bake/main.cpp lo vuelca aparte.
    std::vector<CatalogEntry>  catalog_entries;
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
// `symbols` es la tabla de simbolos del proyecto (M14): de ahi salen los ids de variable,
// bandera, actor, pose, fondo y hablante. Es obligatoria y no opcional a proposito — un modo
// implicito "sin tabla" haria que los tests compilaran con unos ids y el juego con otros, que
// es exactamente la clase de divergencia que la tabla existe para eliminar. Para compilar un
// guion suelto (tests), `symbols_for_single_script` da una tabla valida en una linea.
CompileResult compile_instructions(const std::vector<ParsedInstr>& instructions,
                                    const std::string&              file_name,
                                    const SymbolTable&              symbols);

// Formato .vnc (SPEC.md #9.3, extendido en M5 con la tabla de ChoiceOption). Devuelve
// false si no se pudo escribir el archivo.
bool write_vnc(const std::string& path, const CompiledScriptData& data);
