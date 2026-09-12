#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "formats/map_format.h"

// Horneado de mapas TMX (Tiled) a .vnm (SPEC.md #11, ADR-0043/ADR-0044). Uso exclusivo de
// herramientas offline, igual que el lexer/parser/compilador del DSL: el juego solo lee
// el .vnm ya horneado (game/map_mode.cpp).
//
// El parseo esta separado de la E/S a proposito (tmx_parse trabaja sobre una cadena en
// memoria): asi los tests pueden ejercitar el escaner con un TMX literal, sin archivos ni
// subprocesos. Antes esto vivia dentro del main.cpp de sz_bake y no habia forma de
// testearlo — tres bugs reales del escaner (npos+1 desbordando a 0, la <property> de un
// objeto autocerrado leyendose del objeto siguiente, y ese mismo objeto siguiente
// saltandose entero) se colaron precisamente por eso.

struct ParsedMap {
    u32                     grid_w    = 0;
    u32                     grid_h    = 0;
    u32                     tile_size = 0;
    std::vector<u16>        tiles;      // grid_w*grid_h gids, para render
    std::vector<u8>         collision;  // grid_w*grid_h, 0/1 (sin empaquetar todavia)
    std::vector<MapTrigger> triggers;
    std::string             string_pool;  // rutas .vnc de los triggers, terminadas en '\0'
};

// Devuelve false y llena *out_error con un mensaje concreto si el TMX no encaja en el
// subconjunto soportado (ADR-0044) o esta mal formado. Un horneado falla fuerte, nunca
// produce un mapa a medias en silencio (mismo principio que SPEC.md #9.2).
bool tmx_parse(std::string_view xml, ParsedMap* out, std::string* out_error);

// Serializa un ParsedMap al formato .vnm (formats/map_format.h).
bool write_vnm(const std::string& path, const ParsedMap& map);
