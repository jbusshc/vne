#pragma once
#include "core/types.h"

// Formato .vnm (SPEC.md #11 lista el pipeline "maps/*.tmx -> sz_bake map -> *.vnm" pero
// no da el layout, a diferencia de .vnc/.vnsave: se diseña aqui la version mas simple
// que cubre el criterio de M9 (rejilla + colision + triggers), registrado como ADR en
// docs/DECISIONS.md. Compartido entre tools/bake/main.cpp (escritor) y
// game/map_mode.cpp (lector), igual que formats/cmd.h comparte el layout de Cmd entre
// compiler.cpp y vm.cpp — son datos, no logica, así que no hay problema en que ambos lo
// incluyan.
//
// Formato en disco:
//   magic (4) version (4) grid_w (4) grid_h (4) tile_size_px (4)
//   trigger_count (4) string_pool_size (4)
//   u16 tile_gid[grid_w*grid_h]              (0 = vacio, para render)
//   u8  collision_bits[(grid_w*grid_h+7)/8]  (1 bit por tile, 1 = bloqueado)
//   MapTrigger[trigger_count]
//   string_pool (rutas .vnc de cada trigger, UTF-8 terminadas en '\0')

constexpr u32 k_vnm_magic   = 0x504D4E56u;  // 'VNMP'
constexpr u32 k_vnm_version = 1;

struct MapTrigger {
    u16 tile_x = 0, tile_y = 0, tile_w = 0, tile_h = 0;
    u32 script_path_offset = 0;  // indice en el string_pool
};
