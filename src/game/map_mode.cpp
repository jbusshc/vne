#include "game/map_mode.h"

#include <cstring>

#include <SDL3/SDL.h>

#include "assets/pak.h"
#include "base/log.h"
#include "gfx/gfx.h"

bool MapMode::load(const char* logical_name, Arena* arena) {
    // M11: ya no abre directamente por ruta de archivo -- se resuelve a traves del
    // backend activo (directorio suelto o .pak, ver assets/pak.h), mismo patron que
    // vm/script_load.cpp.
    const u8* bytes = nullptr;
    usize     size  = 0;
    if (!pak_resolve_into_arena(logical_name, arena, &bytes, &size)) {
        log_error("MapMode::load: no se encontro '%s'", logical_name);
        return false;
    }
    if (size < 7 * sizeof(u32)) {
        log_error("MapMode::load: '%s' demasiado pequeno para ser un .vnm", logical_name);
        return false;
    }

    u32 header[7];
    std::memcpy(header, bytes, sizeof(header));
    if (header[0] != k_vnm_magic || header[1] != k_vnm_version) {
        log_error("MapMode::load: '%s' no es un .vnm valido (magic/version)", logical_name);
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

    if (offset > size) {
        log_error("MapMode::load: '%s' esta truncado", logical_name);
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

namespace {

// Division entera con redondeo hacia ABAJO. static_cast<i32>(px) / tile_size trunca hacia
// cero, que para una coordenada negativa da el tile 0 en vez del -1: el jugador podia
// asomar por el borde izquierdo o superior del mapa sin que tile_blocked lo viera fuera de
// la rejilla.
i32 tile_index_floor(f32 px, f32 tile_size) {
    f32 t = px / tile_size;
    i32 i = static_cast<i32>(t);
    return (t < 0.0f && static_cast<f32>(i) != t) ? i - 1 : i;
}

}  // namespace

bool MapMode::box_blocked(f32 center_x, f32 center_y) const {
    f32 ts   = static_cast<f32>(tile_size);
    f32 half = ts * k_player_half_extent_tiles;

    // El borde maximo se prueba un pelo por dentro. Sin esto, un jugador pegado de forma
    // exacta a una pared —que es justo el estado en el que acaba al deslizarse por ella—
    // tocaria tambien el primer tile del otro lado y se quedaria clavado.
    constexpr f32 k_edge_epsilon = 0.01f;

    i32 min_tx = tile_index_floor(center_x - half, ts);
    i32 max_tx = tile_index_floor(center_x + half - k_edge_epsilon, ts);
    i32 min_ty = tile_index_floor(center_y - half, ts);
    i32 max_ty = tile_index_floor(center_y + half - k_edge_epsilon, ts);

    // La caja mide 0.6 tiles, asi que solapa como mucho 2x2 tiles: el bucle es de 4
    // iteraciones en el peor caso, no hace falta nada mas listo (sin motor de fisicas,
    // SPEC.md #10).
    for (i32 ty = min_ty; ty <= max_ty; ++ty) {
        for (i32 tx = min_tx; tx <= max_tx; ++tx) {
            if (tile_blocked(tx, ty)) {
                return true;
            }
        }
    }
    return false;
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

        // Movimiento a nivel de pixel contra la rejilla de colision (SPEC.md #10): cada
        // eje se prueba por separado para poder deslizarse a lo largo de una pared en vez
        // de bloquear el movimiento diagonal entero.
        //
        // M12: la prueba es de caja (AABB) y ya no de punto. Con la de punto solo contaba
        // el centro del jugador, asi que el cuerpo se metia media caja dentro de la pared
        // antes de detenerse y por un hueco de un tile de ancho cabia un jugador de 0.6
        // tiles sin rozar.
        if (!box_blocked(new_x, state->player_y)) {
            state->player_x = new_x;
        }
        if (!box_blocked(state->player_x, new_y)) {
            state->player_y = new_y;
        }
    }

    // Los triggers siguen siendo por el tile del CENTRO, no por la caja, y es deliberado:
    // con AABB bastaria rozar una esquina para disparar la escena, y en un mapa de novela
    // visual eso se dispararia solo al pasar de largo por delante de una puerta. La
    // colision quiere ser generosa (que nada atraviese paredes); un trigger, lo contrario.
    i32 player_tile_x = tile_index_floor(state->player_x, static_cast<f32>(tile_size));
    i32 player_tile_y = tile_index_floor(state->player_y, static_cast<f32>(tile_size));
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
