#pragma once
#include "base/arena.h"
#include "game/map_format.h"
#include "game/mode.h"
#include "vm/state.h"

// MapMode (SPEC.md #10, hito M9): tilemaps de Tiled horneados a .vnm, colision por
// rejilla de bits (sin motor de fisicas), movimiento del jugador a nivel de pixel con
// snapping a la rejilla, triggers que apilan una VnMode nueva. La posicion del jugador y
// el mapa activo viven en GameState (state->map_id/player_x/player_y, M9) para que
// guardar y cargar dentro del mapa funcione (criterio de M9).

constexpr f32 k_player_speed_px_per_s = 220.0f;

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

    bool load(const char* vnm_path, Arena* arena);

    bool tile_blocked(i32 tile_x, i32 tile_y) const;
    i32  trigger_at(i32 tile_x, i32 tile_y) const;

    void update(const InputState& input, f32 dt) override;
    void render() override;
};
