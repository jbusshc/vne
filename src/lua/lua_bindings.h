#pragma once
#include "formats/state.h"
#include "vm/vm.h"

// Unicas declaraciones visibles fuera de lua_bindings.cpp: ningun tipo de sol2 se filtra
// por aqui (SPEC.md #9.4, "unica unidad de traduccion que incluye sol2").
//
// lua_init() crea el interprete persistente y liga la tabla `vn` una sola vez, fuera del
// bucle de frame (se llama desde main() junto al resto de la inicializacion). El estado
// de Lua en si (variables globales que un script pudiera crear) no se serializa
// (restriccion critica de SPEC.md #9.4): cualquier dato que deba sobrevivir a un
// guardado tiene que pasar por vn.set_var/vn.set_flag hacia GameState, que si se
// serializa. Reusar el mismo interprete entre llamadas es mas rapido que crear uno
// nuevo cada vez, y no compromete esa restriccion: nada la obliga a estar vacia entre
// llamadas, solo a que el guardado no dependa de lo que haya dentro.
void lua_init();

// Ejecuta un fragmento de codigo Lua (CmdKind::LuaCall). Compilar y correr un script
// asigna heap por como funciona cualquier interprete de Lua: heap_guard se suspende
// durante la llamada (SPEC.md #4 tiene una excepcion documentada solo para esto, ver
// docs/DECISIONS.md).
void lua_run(const char* code, GameState* state, const CompiledScript* script);
