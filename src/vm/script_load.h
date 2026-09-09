#pragma once
#include "base/arena.h"
#include "vm/vm.h"

// Carga un .vnc con una unica lectura y apunta CompiledScript directo al buffer (SPEC.md
// #9.3: "cero parsing en release"). El buffer vive en `arena` mientras esta no se
// resetee; nunca se copian los Cmd ni el pool de strings a otro sitio.
//
// logical_name se resuelve contra el backend de assets activo (directorio suelto o .pak,
// ver assets/pak.h -- p.ej. "demo.vnc"), no una ruta de archivo literal (M11).

enum class ScriptLoadResult : u8 { Ok, NotFound, BadFormat };

ScriptLoadResult script_load(const char* logical_name, Arena* arena, CompiledScript* out);
