#pragma once
#include "core/types.h"
#include "formats/state.h"

// Buffer circular de instantaneas completas de GameState (SPEC.md #8.3). El rollback no
// reejecuta nada: retroceder es copiar una instantanea de vuelta. Semantica de
// deshacer/rehacer de tamano acotado: capturar un nuevo estado despues de haber
// retrocedido descarta el "adelante" pendiente, igual que cualquier undo/redo.
//
// vive en g_arena_perm (se reserva una unica vez en rollback_init, nunca se resetea).

constexpr u32 k_rollback_capacity = 64;

struct RollbackBuffer {
    GameState* snapshots = nullptr;  // k_rollback_capacity elementos, en g_arena_perm
    u32        count     = 0;        // instantaneas validas actualmente (<= capacidad)
    u32        start     = 0;        // indice fisico de la instantanea logica 0 (la mas vieja)
    u32        cursor    = 0;        // posicion logica actual dentro de [0, count)
};

// Unica instancia del proceso, igual que g_backlog (base/arena.h tiene el mismo patron
// para las arenas). vm.cpp la usa directamente en cmd_start(Say).
extern RollbackBuffer g_rollback;

// Reserva snapshots desde g_arena_perm. Llamar una unica vez, al arrancar.
void rollback_init(RollbackBuffer* rb);

// Guarda `state` como el nuevo presente. Si el cursor no estaba en la punta (se habia
// retrocedido antes), descarta el "adelante" pendiente. Si el buffer esta lleno, descarta
// la instantanea mas vieja.
void rollback_capture(RollbackBuffer* rb, const GameState& state);

// Mueve el cursor una posicion atras/adelante y copia esa instantanea en *out. Devuelve
// false (sin tocar *out) si no hay mas historia en esa direccion.
bool rollback_back(RollbackBuffer* rb, GameState* out);
bool rollback_forward(RollbackBuffer* rb, GameState* out);
