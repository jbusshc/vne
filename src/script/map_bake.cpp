#include "script/map_bake.h"

#include <charconv>
#include <cstdio>

// Subconjunto deliberado de TMX (ADR-0044): una sola capa de tiles ("tiles"), una sola
// capa de colision ("collision", 0/1), ambas con encoding="csv" sin comprimir, y un
// objectgroup con rectangulos y una <property name="script" value="..."/>. Tiled exporta
// CSV sin comprimir por defecto, asi que un .tmx real autorado con cuidado encaja aqui:
// no es un parser XML general (sin dependencia nueva, SPEC.md #3), es lo minimo para
// este subconjunto.

namespace {

std::string_view find_tag_attr(std::string_view tag, std::string_view attr) {
    std::string needle = std::string(attr) + "=\"";
    usize        pos    = tag.find(needle);
    if (pos == std::string_view::npos) {
        return {};
    }
    usize start = pos + needle.size();
    usize end   = tag.find('"', start);
    if (end == std::string_view::npos) {
        return {};
    }
    return tag.substr(start, end - start);
}

i64 find_tag_attr_int(std::string_view tag, std::string_view attr, i64 fallback = 0) {
    std::string_view v = find_tag_attr(tag, attr);
    if (v.empty()) {
        return fallback;
    }
    i64  value = 0;
    auto res   = std::from_chars(v.data(), v.data() + v.size(), value);
    return res.ec == std::errc() ? value : fallback;
}

std::vector<u16> parse_csv_u16(std::string_view csv) {
    std::vector<u16> values;
    usize             i = 0;
    while (i < csv.size()) {
        while (i < csv.size() && (csv[i] == ',' || csv[i] == ' ' || csv[i] == '\n' ||
                                    csv[i] == '\r' || csv[i] == '\t')) {
            i += 1;
        }
        usize start = i;
        while (i < csv.size() && csv[i] >= '0' && csv[i] <= '9') {
            i += 1;
        }
        if (i > start) {
            i64 v = 0;
            std::from_chars(csv.data() + start, csv.data() + i, v);
            values.push_back(static_cast<u16>(v));
        } else if (i == start) {
            i += 1;  // caracter inesperado: se salta para no colgarse
        }
    }
    return values;
}

// Por que puede fallar la extraccion de una capa. Antes era un `bool` y todos los fallos
// acababan en el mismo mensaje ("capa ausente o de tamano incorrecto"), que para un TMX
// comprimido nombraba una causa **que no era**: el CSV comprimido pasaba por parse_csv_u16,
// que se salta lo que no sean digitos, sacaba numeros de un base64 y terminaba quejandose
// del tamano. M13 exige nombrar la compresion como causa (SPEC.md #12).
enum class LayerScan {
    Ok,
    NotFound,
    UnsupportedEncoding,   // encoding distinto de "csv" (base64, xml implicito...)
    Compressed,            // compression="zlib" | "gzip" | "zstd"
    Malformed,             // <data> sin cerrar o fuera de su <layer>
};

// Extrae el contenido de <data encoding="csv">...</data> dentro de la primera capa cuyo
// atributo name coincida, buscando desde `from`. En los casos UnsupportedEncoding y
// Compressed deja en *out_detail el valor literal que traia el TMX, para que el mensaje de
// error pueda repetirlo tal cual en vez de describirlo de memoria.
LayerScan extract_layer_csv(std::string_view xml, std::string_view layer_name, usize from,
                             std::vector<u16>* out_values, std::string* out_detail) {
    usize search_from = from;
    for (;;) {
        usize layer_start = xml.find("<layer", search_from);
        if (layer_start == std::string_view::npos) {
            return LayerScan::NotFound;
        }
        usize header_end = xml.find('>', layer_start);
        if (header_end == std::string_view::npos) {
            return LayerScan::Malformed;
        }
        std::string_view header      = xml.substr(layer_start, header_end - layer_start);
        usize             layer_close = xml.find("</layer>", header_end);
        if (layer_close == std::string_view::npos) {
            return LayerScan::Malformed;
        }

        if (find_tag_attr(header, "name") == layer_name) {
            // Cada find se valida antes de usarlo como posicion del siguiente: buscar a
            // partir de npos esta definido (devuelve npos) pero encadenarlo asi es
            // fragil, y `npos + 1` en cualquier variante de este patron se desborda a 0.
            usize data_open = xml.find("<data", header_end);
            if (data_open == std::string_view::npos || data_open > layer_close) {
                return LayerScan::Malformed;
            }
            usize data_gt = xml.find('>', data_open);
            if (data_gt == std::string_view::npos || data_gt > layer_close) {
                return LayerScan::Malformed;
            }
            std::string_view data_tag = xml.substr(data_open, data_gt - data_open);

            // La compresion se mira ANTES que el encoding: un <data encoding="base64"
            // compression="zlib"> tiene los dos problemas y la compresion es la causa mas
            // informativa de las dos para quien exporto el mapa.
            std::string_view compression = find_tag_attr(data_tag, "compression");
            if (!compression.empty()) {
                *out_detail = std::string(compression);
                return LayerScan::Compressed;
            }
            std::string_view encoding = find_tag_attr(data_tag, "encoding");
            if (encoding != "csv") {
                // encoding ausente en TMX significa XML por elemento (<tile gid="..."/>),
                // que este escaner tampoco soporta; se nombra como tal.
                *out_detail = encoding.empty() ? std::string("xml (sin atributo encoding)")
                                                : std::string(encoding);
                return LayerScan::UnsupportedEncoding;
            }

            usize data_close = xml.find("</data>", data_gt);
            if (data_close == std::string_view::npos || data_close > layer_close) {
                return LayerScan::Malformed;
            }
            *out_values = parse_csv_u16(xml.substr(data_gt + 1, data_close - (data_gt + 1)));
            return LayerScan::Ok;
        }
        search_from = layer_close + 1;
    }
}

// Mensaje de la capa `name` para un fallo que no sea Ok. Se arma aqui y no en cada llamante
// para que las dos capas ('tiles' y 'collision') digan exactamente lo mismo.
std::string layer_error_message(LayerScan scan, std::string_view name,
                                 const std::string& detail) {
    std::string prefix = "capa '" + std::string(name) + "': ";
    switch (scan) {
        case LayerScan::Ok:
            return prefix + "sin error";
        case LayerScan::NotFound:
            return prefix + "no existe ninguna <layer> con ese nombre";
        case LayerScan::Compressed:
            return prefix + "esta comprimida (compression=\"" + detail +
                   "\"). Este horneador solo lee CSV sin comprimir (ADR-0044): en Tiled, "
                   "Mapa > Propiedades > Formato de capa de tiles = CSV.";
        case LayerScan::UnsupportedEncoding:
            return prefix + "usa encoding=\"" + detail +
                   "\". Este horneador solo lee CSV (ADR-0044): en Tiled, Mapa > "
                   "Propiedades > Formato de capa de tiles = CSV.";
        case LayerScan::Malformed:
            return prefix + "su <data> esta mal formado o cae fuera de la <layer>";
    }
    return prefix + "error desconocido";
}

}  // namespace

bool tmx_parse(std::string_view xml, ParsedMap* out, std::string* out_error) {
    *out = ParsedMap{};
    auto fail = [&](const std::string& message) {
        if (out_error != nullptr) {
            *out_error = message;
        }
        return false;
    };

    usize map_start = xml.find("<map");
    if (map_start == std::string_view::npos) {
        return fail("no tiene una etiqueta <map>");
    }
    usize map_end = xml.find('>', map_start);
    if (map_end == std::string_view::npos) {
        return fail("la etiqueta <map> no se cierra");
    }

    std::string_view map_tag   = xml.substr(map_start, map_end - map_start);
    i64               grid_w    = find_tag_attr_int(map_tag, "width");
    i64               grid_h    = find_tag_attr_int(map_tag, "height");
    i64               tile_size = find_tag_attr_int(map_tag, "tilewidth");
    if (grid_w <= 0 || grid_h <= 0 || tile_size <= 0) {
        return fail("<map> sin width/height/tilewidth validos");
    }
    usize tile_count = static_cast<usize>(grid_w) * static_cast<usize>(grid_h);

    // Un solo tileset (ADR-0044): el .vnm guarda el gid crudo, sin restar el firstgid de
    // cada tileset, asi que con dos o mas los tiles del segundo saldrian desplazados. Antes
    // no se comprobaba y el mapa salia mal en silencio.
    usize tileset_count = 0;
    for (usize p = xml.find("<tileset", map_end); p != std::string_view::npos;
         p = xml.find("<tileset", p + 1)) {
        tileset_count += 1;
    }
    if (tileset_count > 1) {
        return fail("el mapa usa " + std::to_string(tileset_count) +
                    " tilesets; este horneador solo soporta uno (ADR-0044), porque el .vnm "
                    "guarda el gid crudo y no resta el firstgid de cada tileset");
    }

    std::vector<u16> tiles;
    std::string      detail;
    LayerScan        scan = extract_layer_csv(xml, "tiles", map_end, &tiles, &detail);
    if (scan != LayerScan::Ok) {
        return fail(layer_error_message(scan, "tiles", detail));
    }
    // width/height del <map> contra las celdas que trae el CSV de verdad (M13). Antes solo
    // se comprobaba que coincidieran, sin decir con que: un TMX editado a mano producia un
    // .vnm con menos tiles de los que la rejilla declaraba.
    if (tiles.size() != tile_count) {
        return fail("capa 'tiles': el <map> declara " + std::to_string(grid_w) + "x" +
                    std::to_string(grid_h) + " = " + std::to_string(tile_count) +
                    " celdas, pero su CSV trae " + std::to_string(tiles.size()));
    }

    std::vector<u16> collision;
    scan = extract_layer_csv(xml, "collision", map_end, &collision, &detail);
    if (scan != LayerScan::Ok) {
        return fail(layer_error_message(scan, "collision", detail));
    }
    if (collision.size() != tile_count) {
        return fail("capa 'collision': el <map> declara " + std::to_string(grid_w) + "x" +
                    std::to_string(grid_h) + " = " + std::to_string(tile_count) +
                    " celdas, pero su CSV trae " + std::to_string(collision.size()));
    }

    out->grid_w    = static_cast<u32>(grid_w);
    out->grid_h    = static_cast<u32>(grid_h);
    out->tile_size = static_cast<u32>(tile_size);
    out->tiles     = tiles;
    out->collision.resize(tile_count);
    for (usize i = 0; i < tile_count; ++i) {
        out->collision[i] = collision[i] != 0 ? 1u : 0u;
    }

    usize group_start = xml.find("<objectgroup");
    if (group_start == std::string_view::npos) {
        return true;  // un mapa sin triggers es valido
    }

    usize group_close   = xml.find("</objectgroup>", group_start);
    usize group_hdr_end = xml.find('>', group_start);
    // Un TMX truncado (sin '>' de cierre de etiqueta, o sin </objectgroup>) daria npos
    // aqui, y npos+1 se desborda a 0: el escaneo empezaria desde el principio del archivo
    // y produciria triggers en posiciones inventadas en vez de fallar.
    if (group_close == std::string_view::npos || group_hdr_end == std::string_view::npos ||
        group_hdr_end > group_close) {
        return fail("<objectgroup> mal formado (sin '>' o sin </objectgroup>)");
    }

    usize object_from = group_hdr_end + 1;
    while (object_from < group_close) {
        // "<object " (con el espacio) para no volver a encontrar el propio
        // "<objectgroup": "<object" es un prefijo de esa palabra.
        usize object_start = xml.find("<object ", object_from);
        if (object_start == std::string_view::npos || object_start > group_close) {
            break;
        }
        usize object_hdr_end = xml.find('>', object_start);
        if (object_hdr_end == std::string_view::npos || object_hdr_end > group_close) {
            return fail("un <object> no cierra su etiqueta");
        }
        std::string_view obj_tag = xml.substr(object_start, object_hdr_end - object_start);

        MapTrigger trig{};
        trig.tile_x = static_cast<u16>(find_tag_attr_int(obj_tag, "x") / tile_size);
        trig.tile_y = static_cast<u16>(find_tag_attr_int(obj_tag, "y") / tile_size);
        trig.tile_w = static_cast<u16>(find_tag_attr_int(obj_tag, "width", tile_size) / tile_size);
        trig.tile_h = static_cast<u16>(find_tag_attr_int(obj_tag, "height", tile_size) / tile_size);

        // Limite de busqueda de la propiedad "script": lo que venga primero entre el
        // cierre de este objeto, el comienzo del siguiente y el final del grupo. Sin el
        // limite del objeto siguiente, un <object .../> autocerrado (lo que Tiled emite
        // cuando no tiene hijos) se quedaria con la <property> del objeto de despues,
        // porque no tiene ningun </object> propio que corte la busqueda.
        usize object_end   = xml.find("</object>", object_hdr_end);
        usize next_object  = xml.find("<object ", object_hdr_end);
        usize search_bound = group_close;
        if (object_end != std::string_view::npos && object_end < search_bound) {
            search_bound = object_end;
        }
        if (next_object != std::string_view::npos && next_object < search_bound) {
            search_bound = next_object;
        }

        usize       prop_start = xml.find("<property", object_hdr_end);
        std::string script_path;
        if (prop_start != std::string_view::npos && prop_start < search_bound) {
            usize prop_hdr_end = xml.find('>', prop_start);
            if (prop_hdr_end == std::string_view::npos || prop_hdr_end > group_close) {
                return fail("una <property> no cierra su etiqueta");
            }
            std::string_view prop_tag = xml.substr(prop_start, prop_hdr_end - prop_start);
            script_path                = std::string(find_tag_attr(prop_tag, "value"));
        }

        trig.script_path_offset = static_cast<u32>(out->string_pool.size());
        out->string_pool.insert(out->string_pool.end(), script_path.begin(), script_path.end());
        out->string_pool.push_back('\0');
        out->triggers.push_back(trig);

        // Avanzar solo hasta el final de la etiqueta de ESTE objeto, no hasta el
        // siguiente </object>: para un <object .../> autocerrado, ese </object> es el del
        // objeto de despues, y saltar hasta ahi se comia un objeto entero sin parsearlo.
        object_from = object_hdr_end + 1;
    }

    return true;
}

bool write_vnm(const std::string& path, const ParsedMap& map) {
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
        return false;
    }

    usize tile_count = static_cast<usize>(map.grid_w) * static_cast<usize>(map.grid_h);
    u32   header[7]  = {
        k_vnm_magic,
        k_vnm_version,
        map.grid_w,
        map.grid_h,
        map.tile_size,
        static_cast<u32>(map.triggers.size()),
        static_cast<u32>(map.string_pool.size()),
    };

    bool ok = std::fwrite(header, sizeof(header), 1, out) == 1;
    if (!map.tiles.empty()) {
        ok = ok && std::fwrite(map.tiles.data(), sizeof(u16), map.tiles.size(), out) ==
                       map.tiles.size();
    }

    usize           collision_bytes = (tile_count + 7) / 8;
    std::vector<u8> collision_bits(collision_bytes, 0);
    for (usize i = 0; i < tile_count; ++i) {
        if (map.collision[i] != 0) {
            collision_bits[i / 8] = static_cast<u8>(collision_bits[i / 8] | (1u << (i % 8)));
        }
    }
    ok = ok && std::fwrite(collision_bits.data(), 1, collision_bytes, out) == collision_bytes;

    if (!map.triggers.empty()) {
        ok = ok && std::fwrite(map.triggers.data(), sizeof(MapTrigger), map.triggers.size(),
                                out) == map.triggers.size();
    }
    if (!map.string_pool.empty()) {
        ok = ok && std::fwrite(map.string_pool.data(), 1, map.string_pool.size(), out) ==
                       map.string_pool.size();
    }

    std::fclose(out);
    return ok;
}
