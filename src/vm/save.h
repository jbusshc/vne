#pragma once
#include "base/types.h"
#include "vm/backlog.h"
#include "vm/state.h"

// Formato .vnsave (SPEC.md #8.3). La miniatura llega en M7 (ADR-0026 la difirio desde
// M4): se codifica en QOI en vez de PNG (ADR de M7 en docs/DECISIONS.md — QOI ya esta en
// la pila cerrada desde ADR-0008 y no hace falta un codificador nuevo en runtime, cosa
// que PNG si necesitaria).

// v2 (M9): GameState anadio map_id/player_x/player_y al final (extension aditiva, ver
// state.h). load_game migra v1 -> v2 automaticamente (SPEC.md #8.3: la migracion se
// escribe en el mismo commit que rompe compatibilidad, nunca despues).
constexpr u32 k_savegame_version = 4;
constexpr i32 k_thumbnail_width  = 384;
constexpr i32 k_thumbnail_height = 216;
// Cota generosa: un QOI de 384x216 en la practica pesa unos pocos KB salvo contenido muy
// ruidoso (QOI no tiene compresion garantizada como un formato con diccionario).
constexpr u32 k_thumbnail_max_bytes = 256 * 1024;

enum class SaveResult : u8 { Ok, WriteError };
enum class LoadResult : u8 {
    Ok,
    NotFound,
    BadFormat,
    ChecksumMismatch,
    UnsupportedVersion,
};

// thumbnail_qoi/thumbnail_size son opcionales (nullptr/0 = sin miniatura, como en M4).
SaveResult save_game(const char* path, const GameState& state, const Backlog& backlog,
                      const u8* thumbnail_qoi = nullptr, u32 thumbnail_size = 0);
LoadResult load_game(const char* path, GameState* out_state, Backlog* out_backlog);

// Solo la miniatura QOI de un .vnsave, sin decodificar el resto (para listar slots en
// SaveLoadMode sin pagar el coste de cargar cada GameState completo). out_qoi debe tener
// espacio para cap bytes; *out_size queda en 0 si el archivo no tiene miniatura (no es un
// error: los .vnsave de M4-M6 nunca la tuvieron).
LoadResult load_save_thumbnail(const char* path, u8* out_qoi, u32 cap, u32* out_size);

// Reconcilia servicios que no viven en GameState (musica, texto en pantalla, sprites)
// tras cargar o hacer rollback. Vacia por ahora a proposito: en M4 todo lo que se ve
// depende directamente de GameState (los sprites de actor se leen de actors[] cada
// frame), asi que no hay nada que resincronizar todavia. M6 (audio, bgm_position) y M7
// (UI de dialogo) le anadiran trabajo real; se deja la funcion ya presente para que se
// concentre ahi, no repartida por el codigo (skill vne-serializable-state).
void vm_resync_after_state_change(GameState* state);
