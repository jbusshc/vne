#pragma once
#include "core/arena.h"
#include "formats/map_format.h"
#include "game/mode.h"
#include "formats/state.h"

// MapMode (SPEC.md #10, hito M9): tilemaps de Tiled horneados a .vnm, colision por
// rejilla de bits (sin motor de fisicas), movimiento del jugador a nivel de pixel con
// snapping a la rejilla, triggers que apilan una VnMode nueva. La posicion del jugador y
// el mapa activo viven en GameState (state->map_id/player_x/player_y, M9) para que
// guardar y cargar dentro del mapa funcione (criterio de M9).

constexpr f32 k_player_speed_px_per_s = 220.0f;

// Media extension de la caja del jugador, en fracciones de tile. Tiene que coincidir con el
// tamano con el que render() dibuja al jugador (0.6 tiles de lado, centrado en player_x/y):
// si no coinciden, el jugador choca donde no se le ve o atraviesa lo que si se le ve.
constexpr f32 k_player_half_extent_tiles = 0.3f;

struct MapMode : Mode {
    GameState* state = nullptr;

    const u16*         tiles          = nullptr;
    const u8*          collision_bits = nullptr;
    const MapTrigger*  triggers       = nullptr;
    const char*        string_pool    = nullptr;
    u32                grid_w = 0, grid_h = 0, tile_size = 0, trigger_count = 0;

    // Guion pendiente de apilar como VnMode (SPEC.md #10: "al pisarlos se apila VnMode
    // con ese guion"): main.cpp lo consulta despues de mode_stack_update() y lo apila,
    // igual que wants_close en los modos de M7 — Mode no tiene un mecanismo generico
    // para pedirle cosas a la pila que lo contiene.
    const char* pending_trigger_script = nullptr;
    // Evita volver a disparar el mismo trigger en el frame siguiente con solo salir y
    // volver a entrar de inmediato: se limpia cuando el jugador sale del tile.
    i32 active_trigger_index = -1;

    // Destino de "camina hasta ahi", puesto con un clic (M15). Necesario para el criterio
    // de SPEC.md #12 "una partida completa se juega de principio a fin solo con el raton":
    // la partida EMPIEZA en el mapa, asi que sin esto no se llega ni a la primera escena.
    //
    // No vive en GameState a proposito, igual que active_trigger_index: es intencion de
    // entrada, no estado de partida. Guardar en medio de un paseo y cargar deja al jugador
    // quieto donde estaba, que es un resultado correcto y no obliga a subir la version del
    // .vnsave por un dato que no se echa de menos.
    f32  move_target_x   = 0.0f;
    f32  move_target_y   = 0.0f;
    bool has_move_target = false;

    // logical_name se resuelve contra el backend de assets activo (directorio suelto o
    // .pak, ver vfs/pak.h -- p.ej. "demo_map.vnm"), no una ruta de archivo literal
    // (M11).
    bool load(const char* logical_name, Arena* arena);

    bool tile_blocked(i32 tile_x, i32 tile_y) const;

    // Colision AABB (M12): true si la caja del jugador centrada en (center_x, center_y)
    // solapa ALGUN tile bloqueado. Hasta M12 la colision era por punto, asi que el cuerpo
    // del jugador se metia media caja dentro de las paredes antes de pararse.
    bool box_blocked(f32 center_x, f32 center_y) const;

    i32 trigger_at(i32 tile_x, i32 tile_y) const;

    void update(const InputState& input, f32 dt) override;
    void render() override;
};
