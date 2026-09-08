#include "game/map_mode.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include "base/log.h"
#include "gfx/gfx.h"

bool MapMode::load(const char* vnm_path, Arena* arena) {
    std::FILE* file = std::fopen(vnm_path, "rb");
    if (file == nullptr) {
        log_error("MapMode::load: no se encontro '%s'", vnm_path);
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < static_cast<long>(7 * sizeof(u32))) {
        std::fclose(file);
        log_error("MapMode::load: '%s' demasiado pequeno para ser un .vnm", vnm_path);
        return false;
    }

    u8* bytes = arena_alloc_n<u8>(arena, static_cast<usize>(size));
    if (bytes == nullptr ||
        std::fread(bytes, 1, static_cast<usize>(size), file) != static_cast<usize>(size)) {
        std::fclose(file);
        log_error("MapMode::load: fallo leyendo '%s'", vnm_path);
        return false;
    }
    std::fclose(file);

    u32 header[7];
    std::memcpy(header, bytes, sizeof(header));
    if (header[0] != k_vnm_magic || header[1] != k_vnm_version) {
        log_error("MapMode::load: '%s' no es un .vnm valido (magic/version)", vnm_path);
        return false;
    }
    grid_w        = header[2];
    grid_h        = header[3];
    tile_size     = header[4];
    trigger_count = header[5];
    u32 string_pool_size = header[6];

    usize offset = 7 * sizeof(u32);
    tiles = reinterpret_cast<const u16*>(bytes + offset);
    offset += static_cast<usize>(grid_w) * grid_h * sizeof(u16);

    collision_bits = bytes + offset;
    offset += (static_cast<usize>(grid_w) * grid_h + 7) / 8;

    triggers = reinterpret_cast<const MapTrigger*>(bytes + offset);
    offset += static_cast<usize>(trigger_count) * sizeof(MapTrigger);

    string_pool = reinterpret_cast<const char*>(bytes + offset);
    offset += string_pool_size;

    if (offset > static_cast<usize>(size)) {
        log_error("MapMode::load: '%s' esta truncado", vnm_path);
        return false;
    }
    return true;
}

bool MapMode::tile_blocked(i32 tile_x, i32 tile_y) const {
    if (tile_x < 0 || tile_y < 0 || tile_x >= static_cast<i32>(grid_w) ||
        tile_y >= static_cast<i32>(grid_h)) {
        return true;  // fuera de la rejilla: tratado como pared, sin motor de fisicas
    }
    u32 index = static_cast<u32>(tile_y) * grid_w + static_cast<u32>(tile_x);
    return (collision_bits[index / 8] & (1u << (index % 8))) != 0;
}

i32 MapMode::trigger_at(i32 tile_x, i32 tile_y) const {
    for (u32 i = 0; i < trigger_count; ++i) {
        const MapTrigger& t = triggers[i];
        if (tile_x >= t.tile_x && tile_x < t.tile_x + t.tile_w && tile_y >= t.tile_y &&
            tile_y < t.tile_y + t.tile_h) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

void MapMode::update(const InputState& input, f32 dt) {
    if (state == nullptr || tiles == nullptr) {
        return;
    }

    f32 dx = 0.0f, dy = 0.0f;
    if (input.key_down[SDL_SCANCODE_W]) dy -= 1.0f;
    if (input.key_down[SDL_SCANCODE_S]) dy += 1.0f;
    if (input.key_down[SDL_SCANCODE_A]) dx -= 1.0f;
    if (input.key_down[SDL_SCANCODE_D]) dx += 1.0f;

    if (dx != 0.0f || dy != 0.0f) {
        f32 len = SDL_sqrtf(dx * dx + dy * dy);
        dx /= len;
        dy /= len;

        f32 new_x = state->player_x + dx * k_player_speed_px_per_s * dt;
        f32 new_y = state->player_y + dy * k_player_speed_px_per_s * dt;

        // Movimiento a nivel de pixel con snapping a la rejilla de colision (SPEC.md
        // #10): cada eje se prueba por separado para poder deslizarse a lo largo de una
        // pared en vez de bloquear el movimiento diagonal entero.
        i32 tile_size_i = static_cast<i32>(tile_size);
        if (!tile_blocked(static_cast<i32>(new_x) / tile_size_i,
                           static_cast<i32>(state->player_y) / tile_size_i)) {
            state->player_x = new_x;
        }
        if (!tile_blocked(static_cast<i32>(state->player_x) / tile_size_i,
                           static_cast<i32>(new_y) / tile_size_i)) {
            state->player_y = new_y;
        }
    }

    i32 player_tile_x = static_cast<i32>(state->player_x) / static_cast<i32>(tile_size);
    i32 player_tile_y = static_cast<i32>(state->player_y) / static_cast<i32>(tile_size);
    i32 trigger_index = trigger_at(player_tile_x, player_tile_y);

    if (trigger_index >= 0 && trigger_index != active_trigger_index) {
        active_trigger_index   = trigger_index;
        pending_trigger_script = string_pool + triggers[trigger_index].script_path_offset;
    } else if (trigger_index < 0) {
        active_trigger_index = -1;
    }
}

void MapMode::render() {
    if (tiles == nullptr) {
        return;
    }
    // Rejilla de colores placeholder (sin arte real todavia, skill vne-milestone-
    // workflow): cada gid de tile se tine con un color distinto sobre la textura blanca
    // de 1x1, no hay atlas de tiles real hasta que exista el pipeline de assets de mapas.
    static const u32 k_tile_colors[4] = {0xFF202020u, 0xFF4A7A4Au, 0xFF6A6A2Au, 0xFF2A4A7Au};
    f32              ts               = static_cast<f32>(tile_size);
    for (u32 y = 0; y < grid_h; ++y) {
        for (u32 x = 0; x < grid_w; ++x) {
            u16 gid = tiles[y * grid_w + x];
            Sprite s{};
            s.tex   = gfx_white_texture();
            s.dst_x = static_cast<f32>(x) * ts;
            s.dst_y = static_cast<f32>(y) * ts;
            s.dst_w = ts - 1.0f;
            s.dst_h = ts - 1.0f;
            s.color = k_tile_colors[gid % 4];
            s.layer = static_cast<u16>(GfxLayer::Background);
            gfx_draw_sprite(s);
        }
    }

    Sprite player{};
    player.tex   = gfx_white_texture();
    player.dst_x = state->player_x - ts * 0.3f;
    player.dst_y = state->player_y - ts * 0.3f;
    player.dst_w = ts * 0.6f;
    player.dst_h = ts * 0.6f;
    player.color = 0xFFFFFFFFu;
    player.layer = static_cast<u16>(GfxLayer::Actors);
    gfx_draw_sprite(player);
}
