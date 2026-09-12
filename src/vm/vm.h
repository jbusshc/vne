#pragma once
#include "base/types.h"
#include "vm/cmd.h"
#include "vm/state.h"

// Interprete del guion (SPEC.md #8.1). Un guion compilado es un array de Cmd contiguo
// mas su pool de strings, apuntado directo desde el .vnc cargado (sin copiar, sin
// parsear texto en runtime).

// Entrada de la tabla de etiquetas del .vnc (SPEC.md #9.3). Ignorada por el runtime desde
// M3 (los saltos del DSL ya son pc directos), pero M5 la necesita de verdad para
// `vn.jump(nombre)` desde Lua, que solo tiene el nombre en tiempo de ejecucion.
struct ScriptLabel {
    u32 name_hash;
    u32 pc;
};

struct CompiledScript {
    const Cmd*  cmds       = nullptr;
    u32         cmd_count  = 0;
    const char* string_pool      = nullptr;  // bytes UTF-8 terminados en \0
    u32         string_pool_size = 0;
    const ScriptLabel* labels      = nullptr;
    u32                label_count = 0;
    // Opciones de los comandos Choice (SPEC.md #8.1 solo reserva first_option/
    // option_count en Cmd; la tabla en si es una extension del .vnc, ver ADR de M5).
    const ChoiceOption* choice_options       = nullptr;
    u32                 choice_option_count = 0;
    // Tablas de nombres de actor, pose y fondo (.vnc v5, M13). Cada u32 es un offset
    // dentro de string_pool; se indexan con id-1 porque el id 0 significa "vacio".
    // Existen porque el id es un indice secuencial de un interner local al guion, no un
    // hash: sin esta tabla no hay forma de saber que sprite corresponde a un actor_id, y
    // por tanto no se puede dibujar.
    const u32* actor_names      = nullptr;
    u32        actor_name_count = 0;
    const u32* pose_names       = nullptr;
    u32        pose_name_count  = 0;
    const u32* bg_names         = nullptr;
    u32        bg_name_count    = 0;
};

inline const char* script_string(const CompiledScript& script, u32 text_id) {
    if (text_id >= script.string_pool_size) {
        return "";
    }
    return script.string_pool + text_id;
}

// Nombre de un actor, pose o fondo a partir de su id (.vnc v5, M13). Devuelve "" si el id
// es 0 ("vacio") o esta fuera de la tabla, nunca un puntero invalido: el llamante dibuja
// menos, no revienta (SPEC.md #4).
inline const char* script_name_from_table(const CompiledScript& script, const u32* table,
                                           u32 count, u16 id) {
    if (id == 0 || table == nullptr || id > count) {
        return "";
    }
    return script_string(script, table[id - 1]);
}

inline const char* script_actor_name(const CompiledScript& s, u16 id) {
    return script_name_from_table(s, s.actor_names, s.actor_name_count, id);
}
inline const char* script_pose_name(const CompiledScript& s, u16 id) {
    return script_name_from_table(s, s.pose_names, s.pose_name_count, id);
}
inline const char* script_bg_name(const CompiledScript& s, u16 id) {
    return script_name_from_table(s, s.bg_names, s.bg_name_count, id);
}

// Busqueda lineal por hash de nombre (SPEC.md #9.4, vn.jump): el numero de etiquetas de
// un guion es pequeno, no hace falta una tabla hash real.
inline bool vm_find_label(const CompiledScript& script, u32 name_hash, u32* out_pc) {
    for (u32 i = 0; i < script.label_count; ++i) {
        if (script.labels[i].name_hash == name_hash) {
            *out_pc = script.labels[i].pc;
            return true;
        }
    }
    return false;
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

// Resuelve un comando Choice pendiente (SPEC.md #9.1): el pc debe estar parado en un
// CmdKind::Choice (vm_update habra devuelto false repetidamente hasta que se llame esto,
// no hay timeout ni avance automatico salvo via vm_skip_current). Devuelve false sin
// tocar el estado si el pc no esta en un Choice, si option_index esta fuera de rango, o
// si la opcion tiene una condicion que no se cumple.
bool vm_select_choice(VmState* vm, GameState* state, const CompiledScript& script,
                       u8 option_index);

// Confirma la linea de dialogo actual (SPEC.md #10, VnMode): sin efecto si no hay ningun
// Say esperando input. Cierra ADR-0023.
inline void vm_confirm_say(GameState* state) {
    state->vm.waiting_for_input = 0;
}
