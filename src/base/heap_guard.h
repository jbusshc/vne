#pragma once
#include "base/types.h"

// Cuenta las asignaciones de heap hechas via operator new/delete durante VN_DEBUG y
// VN_EDITOR (Debug y Dev). En Ship no existe overhead: las funciones se compilan vacias y
// no hay overload de operator new. Sirve para verificar la regla de cero asignaciones por
// frame (SPEC.md #4, skill vne-memory-model): se resetea al empezar el frame y se
// comprueba justo antes de presentar.
//
// No intercepta malloc/free hechos por librerias de terceros (SDL, etc.) en C puro: solo
// cubre el codigo C++ del motor, que es el que puede y debe pasar por las arenas.

extern u64 g_frame_alloc_count;

void heap_guard_reset_frame();
void heap_guard_check_frame();

// Excepcion puntual y documentada a la regla de cero heap por frame (SPEC.md #4, ADR de
// M5 en docs/DECISIONS.md): ejecutar un fragmento @lua via sol2 asigna heap por como
// funciona cualquier interprete de Lua, y no hay forma de evitarlo sin renunciar a Lua
// como lenguaje de logica (SPEC.md #3). Es la UNICA fuente de asignacion de heap
// permitida en el bucle de frame; todo lo demas del motor sigue en cero. Las llamadas
// deben venir siempre en pareja, envolviendo exactamente la ejecucion de sol2 en
// script/lua_bindings.cpp, nunca un ambito mas amplio.
void heap_guard_suspend();
void heap_guard_resume();
