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
