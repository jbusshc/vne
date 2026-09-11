#pragma once
#include "base/types.h"

// Cuenta las asignaciones de heap hechas via operator new/delete durante VN_DEBUG y
// VN_EDITOR (Debug y Dev). En Ship no existe overhead: las funciones se compilan vacias y
// no hay overload de operator new. Sirve para verificar la regla de cero asignaciones por
// frame (SPEC.md #4, skill vne-memory-model): se resetea al empezar el frame y se
// comprueba justo antes de presentar.
//
// Las librerias de terceros escritas en C (SDL3, sokol, FreeType, HarfBuzz, miniaudio,
// Lua, qoi) NO pasan por operator new: llaman a malloc directamente, y ese camino es
// invisible aqui. Durante mucho tiempo eso convirtio el contador en una verificacion
// parcial que se presentaba como total. La solucion NO es interceptar malloc a lo bruto
// (no hay forma portable de hacerlo, y la portabilidad manda: SPEC.md §2): cada libreria
// que asigna se inicializa con SU propio hook de asignacion, que llama aqui. Ver
// heap_guard_install_third_party_hooks() en base/heap_guard_hooks.h.
//
// Lo que sigue sin cubrirse, y conviene saberlo: cualquier libreria futura que no ofrezca
// hook de asignacion, y HarfBuzz, cuyo hook es de tiempo de compilacion (por eso
// text/layout.cpp reutiliza un hb_buffer_t persistente en vez de crear uno por llamada).
//
// thread_local a proposito (M11): el hilo de IO de assets tambien pasa por operator new/
// delete (leer un archivo a un buffer, por ejemplo), y esas asignaciones no tienen nada
// que ver con el frame del hilo principal. Sin esto, dos hilos incrementando el mismo
// contador global seria una carrera de datos real y ademas ensuciaria la cuenta que
// revisa heap_guard_check_frame() con asignaciones que no son del bucle de frame. Cada
// hilo lleva su propio contador; el hilo de IO nunca llama a reset_frame/check_frame (no
// tiene "frame"), asi que el suyo simplemente no se consulta nunca. suspend()/resume()
// siguen siendo solo para el hilo principal (Lua, primera carga de audio, subproceso del
// editor): no las llames desde el hilo de IO, no hace falta, su exencion ya es total.
extern thread_local u64 g_frame_alloc_count;

void heap_guard_reset_frame();
void heap_guard_check_frame();

// Excepcion puntual y documentada a la regla de cero heap por frame (SPEC.md #4, ADR de
// M5 en docs/DECISIONS.md): ejecutar un fragmento @lua via sol2 asigna heap por como
// funciona cualquier interprete de Lua, y no hay forma de evitarlo sin renunciar a Lua
// como lenguaje de logica (SPEC.md #3). Es la UNICA fuente de asignacion de heap
// permitida en el bucle de frame; todo lo demas del motor sigue en cero. Las llamadas
// deben venir siempre en pareja, envolviendo exactamente la ejecucion de sol2 en
// script/lua_bindings.cpp, nunca un ambito mas amplio.
//
// Se anidan: llevan una cuenta de profundidad, no un interruptor. Hace falta porque las
// excepciones se anidan de verdad (`@lua` que llama a `vn.play_sfx` y acaba en `audio_load`;
// `hot_reload_update` que lanza `vne_bake`), y con un interruptor el resume interno
// reactivaba el contador dejando al ambito externo desprotegido sin que nadie lo notara.
// Las llamadas tienen que venir emparejadas: un resume de mas dispara un assert.
void heap_guard_suspend();
void heap_guard_resume();

// De donde vino una asignacion. Saber solo "hubo 3 asignaciones" no sirve para arreglar
// nada cuando el causante puede ser cualquiera de seis librerias; con el origen, el
// mensaje del assert apunta directamente al sitio.
enum class HeapSource : u8 { Engine, Sdl, Sokol, FreeType, MiniAudio, Qoi, ImGui, Count };

// Punto de entrada para los hooks de asignacion de las librerias de terceros. Cuenta una
// asignacion igual que lo haria operator new, respetando suspend()/resume(). Existe en
// todas las configuraciones (tambien en Ship, donde no hace nada) para que los hooks se
// escriban una sola vez y no con #ifdef alrededor de cada uno.
void heap_guard_count_alloc(HeapSource source);

// Desglose del frame actual por origen, para el HUD de debug y para el mensaje del assert.
u64 heap_guard_count_by_source(HeapSource source);
