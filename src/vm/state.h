#pragma once
#include <type_traits>

#include "base/types.h"

// Estado serializable del motor (SPEC.md #8.2). Trivialmente copiable a proposito:
// guardar es un memcpy y el rollback es una instantanea completa (M4). Ningun puntero,
// ningun std::string, ningun contenedor de tamano dinamico — ver skill
// vne-serializable-state antes de anadir un solo campo aqui.
//
// M3 ya define el struct completo (SPEC.md lo da cerrado), pero todavia no lo guarda a
// disco ni hace rollback: eso es M4. vars/flags existen pero nada los usa hasta M5.

constexpr u32 k_max_actor_slots = 8;
constexpr u32 k_max_vars        = 512;
constexpr u32 k_max_call_depth  = 16;
constexpr u32 k_max_flags       = 2048;

struct ActorSlot {
    u16 actor_id = 0;  // 0 = vacio
    u16 pose_id  = 0;
    f32 x = 0.0f, y = 0.0f;
    f32 alpha = 0.0f;
    f32 scale = 1.0f;
};

struct VmState {
    u32 script_id = 0;
    u32 pc        = 0;
    u32 call_stack[k_max_call_depth] = {};
    u8  call_depth  = 0;
    u8  cmd_phase   = 0;  // 0 = sin iniciar, 1 = en curso
    // Relleno explicito (skill vne-serializable-state: "padding sin inicializar" esta
    // prohibido). El relleno implicito del compilador entre cmd_phase y cmd_timer no se
    // preserva de forma fiable a traves de copias/escrituras parciales: hacerlo un campo
    // real con valor por defecto evita que el mismo GameState logico produzca bytes
    // distintos en un memcmp o al guardarse a disco.
    u8  _pad0[2] = {};
    f32 cmd_timer   = 0.0f;
    u32 visible_glyphs   = 0;
    u8  waiting_for_input = 0;
    u8  _pad1[3] = {};  // relleno final explicito, mismo motivo que _pad0.
};

struct GameState {
    u32       version = 1;
    VmState   vm;
    ActorSlot actors[k_max_actor_slots];
    u16       bg_id = 0;
    u8        _pad0[2] = {};  // ver comentario de relleno en VmState.
    i32       vars[k_max_vars]           = {};
    u8        flags[k_max_flags / 8]     = {};
    u32       rng_state         = 1;
    u16       bgm_track_id      = 0;
    u8        _pad1[2] = {};  // ver comentario de relleno en VmState.
    f32       bgm_position      = 0.0f;
    f32       bus_volume[4]     = {1.0f, 1.0f, 1.0f, 1.0f};
    char      player_name[32]   = {};
    u32       playtime_seconds  = 0;

    // M9 (MapMode, SPEC.md #10): mapa activo y posicion del jugador dentro de el. 0 =
    // sin mapa activo (todavia en una escena de VN pura, nunca se piso un MapMode).
    // Anadido al final de GameState a proposito: extension aditiva (skill
    // vne-serializable-state, "amplia la constante y sube la version del formato"), asi
    // que una partida v1 solo necesita rellenar estos campos con su valor por defecto en
    // la migracion (ver save.cpp migrate_v1_to_v2), no reordenar nada existente.
    u16       map_id    = 0;
    u8        _pad2[2]  = {};
    f32       player_x  = 0.0f;
    f32       player_y  = 0.0f;
};

static_assert(std::is_trivially_copyable_v<GameState>);
