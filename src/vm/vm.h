#pragma once
#include "base/types.h"
#include "vm/cmd.h"
#include "vm/state.h"

// Interprete del guion (SPEC.md #8.1). Un guion compilado es un array de Cmd contiguo
// mas su pool de strings, apuntado directo desde el .vnc cargado (sin copiar, sin
// parsear texto en runtime).

struct CompiledScript {
    const Cmd*  cmds       = nullptr;
    u32         cmd_count  = 0;
    const char* string_pool      = nullptr;  // bytes UTF-8 terminados en \0
    u32         string_pool_size = 0;
};

inline const char* script_string(const CompiledScript& script, u32 text_id) {
    if (text_id >= script.string_pool_size) {
        return "";
    }
    return script.string_pool + text_id;
}

// Avanza el guion todo lo que se pueda dentro de este dt: procesa comandos instantaneos
// encadenados (Label, Jump, Nop, Say/Show/Hide/Bg/Wait con fade<=0) sin gastar tiempo de
// mas, y se detiene en el primer comando que sigue en curso al terminar el frame.
// Devuelve true cuando el guion termino (llego a End o se quedo sin comandos).
bool vm_update(VmState* vm, GameState* state, const CompiledScript& script, f32 dt);

// Completa el comando actual al instante (SPEC.md #12: "skip_to_end completa cualquier
// comando de forma instantanea") y avanza el pc, sin esperar a que update() lo haga con
// el paso del tiempo. Es la base del modo skip (M7) y del --autoplay-script (M3).
void vm_skip_current(VmState* vm, GameState* state, const CompiledScript& script);
