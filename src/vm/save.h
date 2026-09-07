#pragma once
#include "vm/backlog.h"
#include "vm/state.h"

// Formato .vnsave (SPEC.md #8.3). La miniatura se difiere a M7 (ADR pendiente de
// registrar): el campo de tamano de miniatura existe en el formato desde ya (compatible
// hacia adelante) pero M4 siempre escribe 0 bytes de miniatura — ningun criterio de
// aceptacion de M4 depende de ella, y capturar+codificar el framebuffer solo tiene
// sentido cuando exista una pantalla de guardado que la muestre.

constexpr u32 k_savegame_version = 1;

enum class SaveResult : u8 { Ok, WriteError };
enum class LoadResult : u8 {
    Ok,
    NotFound,
    BadFormat,
    ChecksumMismatch,
    UnsupportedVersion,
};

SaveResult save_game(const char* path, const GameState& state, const Backlog& backlog);
LoadResult load_game(const char* path, GameState* out_state, Backlog* out_backlog);

// Reconcilia servicios que no viven en GameState (musica, texto en pantalla, sprites)
// tras cargar o hacer rollback. Vacia por ahora a proposito: en M4 todo lo que se ve
// depende directamente de GameState (los sprites de actor se leen de actors[] cada
// frame), asi que no hay nada que resincronizar todavia. M6 (audio, bgm_position) y M7
// (UI de dialogo) le anadiran trabajo real; se deja la funcion ya presente para que se
// concentre ahi, no repartida por el codigo (skill vne-serializable-state).
void vm_resync_after_state_change(GameState* state);
