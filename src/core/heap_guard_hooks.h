#pragma once
#include "core/heap_guard.h"
#include "core/types.h"

// Asignadores que cuentan (base/heap_guard.h) y delegan en el del sistema. Existen para
// que cada libreria de terceros escrita en C se inicialice con SU propio hook de
// asignacion y sus mallocs dejen de ser invisibles para la regla de cero heap por frame
// (SPEC.md #4).
//
// Es el camino portable, y por eso es el elegido: no hay forma de interceptar malloc de
// forma portable (en MSVC solo con el CRT de depuracion via _CrtSetAllocHook, en glibc
// sobrescribiendo el simbolo), y la portabilidad condiciona cada linea que se escribe hoy
// (SPEC.md §2). Aqui no hay ni un solo #if de plataforma: son las APIs de las propias
// librerias, iguales en los tres sistemas.
//
// Donde se instala cada uno:
//
//   SDL3       heap_guard_install_sdl_hooks(), antes de SDL_Init      main.cpp
//   sokol_gfx  sg_desc.allocator                                      render/render.cpp
//   FreeType   FT_MemoryRec_ pasado a FT_New_Library                  text/font.cpp
//   miniaudio  ma_engine_config.allocationCallbacks                   audio/audio.cpp
//   qoi        macros QOI_MALLOC / QOI_FREE                           render/texture.cpp
//
// Lua queda fuera a proposito: sus asignaciones ya estan exceptuadas de la regla
// (ADR-0032) y contarlas solo serviria para restarlas otra vez. HarfBuzz tambien, porque
// su hook es de tiempo de compilacion; ver el comentario de text/layout.cpp.

void* heap_guard_malloc(usize size, HeapSource source);
void* heap_guard_calloc(usize count, usize size, HeapSource source);
void* heap_guard_realloc(void* p, usize size, HeapSource source);
void  heap_guard_free(void* p);

// Instala los contadores en SDL3. Debe llamarse ANTES de SDL_Init: SDL prohibe cambiar
// sus funciones de memoria una vez ha asignado algo.
void heap_guard_install_sdl_hooks();

// Asignaciones acumuladas de un origen desde que arranco el proceso. A diferencia del
// contador por frame, **cuenta tambien lo que ocurre con el guard suspendido**: sirve para
// poner un presupuesto de por vida a la inicializacion diferida de una libreria (ver
// platform/input.cpp) en vez de cegar un camino por frame para siempre.
u64 heap_guard_lifetime_count(HeapSource source);
