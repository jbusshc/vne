#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "game/map_format.h"
#include "script/compiler.h"
#include "script/parser.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// qoi.h y stb_image.h son codigo de terceros: sus conversiones implicitas int/uchar no
// son un problema de nuestro codigo, pero MSVC las marca bajo /W4 (igual que con doctest,
// ver tests/CMakeLists.txt).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4456)
#endif
#define QOI_IMPLEMENTATION
#include <qoi.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "base/log.h"
#include "base/types.h"

// Sin <filesystem>: sus headers usan try/catch internamente y disparan C4530 bajo MSVC
// con las excepciones desactivadas (SPEC.md #4). Crear un directorio y listar un
// directorio son simples de sobra para no necesitar esa dependencia.

static void ensure_directory_exists(const char* path) {
#if defined(_WIN32)
    CreateDirectoryA(path, nullptr);
#else
    mkdir(path, 0755);
#endif
}

static std::vector<std::string> list_png_files(const char* dir) {
    std::vector<std::string> result;
#if defined(_WIN32)
    std::string        pattern = std::string(dir) + "\\*.png";
    WIN32_FIND_DATAA    find_data;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return result;
    }
    do {
        result.push_back(std::string(dir) + "/" + find_data.cFileName);
    } while (FindNextFileA(handle, &find_data));
    FindClose(handle);
#else
    DIR* d = opendir(dir);
    if (d == nullptr) {
        return result;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0) {
            result.push_back(std::string(dir) + "/" + name);
        }
    }
    closedir(d);
#endif
    return result;
}

namespace {

// Formato de assets_baked/atlas_00.bin (ADR-0025): version 2, un manifiesto generico de
// rectangulos, el mismo tanto si el atlas vino de PNGs reales como del placeholder
// procedural de respaldo. No es todavia el atlas.bin final de SPEC.md #11 (sin nombres,
// sin sub-paginas), pero ya no es una rejilla de celdas iguales.
struct SpriteRect {
    u16 x, y, w, h;
};

bool write_atlas_bin(const char* path, i32 atlas_w, i32 atlas_h,
                      const std::vector<SpriteRect>& sprites) {
    std::FILE* bin = std::fopen(path, "wb");
    if (bin == nullptr) {
        return false;
    }
    const u32 header[5] = {
        0x54414E56u,  // 'VNAT'
        2u,           // version
        static_cast<u32>(atlas_w),
        static_cast<u32>(atlas_h),
        static_cast<u32>(sprites.size()),
    };
    bool ok = std::fwrite(header, sizeof(header), 1, bin) == 1;
    if (!sprites.empty()) {
        ok = ok && std::fwrite(sprites.data(), sizeof(SpriteRect), sprites.size(), bin) ==
                       sprites.size();
    }
    std::fclose(bin);
    return ok;
}

bool read_whole_file(const char* path, std::string* out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        return false;
    }
    out->resize(static_cast<usize>(size));
    usize read = std::fread(out->data(), 1, static_cast<usize>(size), file);
    std::fclose(file);
    return read == static_cast<usize>(size);
}

// vne_bake script <entrada.vns> <salida.vnc> (SPEC.md #9.3, pipeline de SPEC.md #11).
int bake_script(const char* in_path, const char* out_path) {
    std::string source;
    if (!read_whole_file(in_path, &source)) {
        log_error("vne_bake: no se pudo leer '%s'", in_path);
        return 1;
    }

    ParseResult parsed = parse_script(source, in_path);
    if (!parsed.ok()) {
        for (const ParseError& err : parsed.errors) {
            log_error("%s:%u: %s", err.file.c_str(), err.line, err.message.c_str());
        }
        return 1;
    }

    CompileResult compiled = compile_instructions(parsed.instructions, in_path);
    if (!compiled.ok()) {
        for (const CompileError& err : compiled.errors) {
            log_error("%s:%u: %s", err.file.c_str(), err.line, err.message.c_str());
        }
        return 1;
    }

    if (!write_vnc(out_path, compiled.data)) {
        log_error("vne_bake: no se pudo escribir '%s'", out_path);
        return 1;
    }

    log_info("vne_bake: %s -> %s (%zu comandos, %zu bytes de strings)", in_path, out_path,
              compiled.data.cmds.size(), compiled.data.string_pool.size());
    return 0;
}

// --- Localizacion (M10, SPEC.md #9.2/#10): extraccion del catalogo y horneado a .vnl
// (ADR-0046: decision del usuario, catalogo horneado, no suelto en texto plano).
// Formato de autoria intermedio (catalog-extract escribe, catalog-compile lee): dos
// lineas por entrada (clave, luego texto) en vez de CSV con comas/comillas escapadas —
// el dialogo puede contener cualquier puntuacion, asi que evitar el escapado de CSV de
// verdad es la opcion mas simple. Debe coincidir con text/catalog.h (duplicado a
// proposito, mismo patron que el magic de .vnc entre compiler.cpp y script_load.cpp).
namespace {
constexpr u32 k_vnl_magic_local   = 0x434C4E56u;  // 'VNLC'
constexpr u32 k_vnl_version_local = 1;
}  // namespace

// vne_bake catalog-extract <salida.csv> <guion1.vns> [guion2.vns ...] (SPEC.md #10:
// "extraccion del catalogo"). No escribe ningun .vnc: solo parsea y compila en memoria
// para recolectar los textos.
int bake_catalog_extract(const char* out_path, int script_count, char** script_paths) {
    std::FILE* out = std::fopen(out_path, "wb");
    if (out == nullptr) {
        log_error("vne_bake: no se pudo escribir '%s'", out_path);
        return 1;
    }

    usize total_entries = 0;
    for (int i = 0; i < script_count; ++i) {
        std::string source;
        if (!read_whole_file(script_paths[i], &source)) {
            log_error("vne_bake: no se pudo leer '%s'", script_paths[i]);
            std::fclose(out);
            return 1;
        }
        ParseResult parsed = parse_script(source, script_paths[i]);
        if (!parsed.ok()) {
            for (const ParseError& err : parsed.errors) {
                log_error("%s:%u: %s", err.file.c_str(), err.line, err.message.c_str());
            }
            std::fclose(out);
            return 1;
        }
        CompileResult compiled = compile_instructions(parsed.instructions, script_paths[i]);
        if (!compiled.ok()) {
            for (const CompileError& err : compiled.errors) {
                log_error("%s:%u: %s", err.file.c_str(), err.line, err.message.c_str());
            }
            std::fclose(out);
            return 1;
        }
        for (const CatalogEntry& entry : compiled.data.catalog_entries) {
            std::fwrite(entry.key.data(), 1, entry.key.size(), out);
            std::fputc('\n', out);
            std::fwrite(entry.text.data(), 1, entry.text.size(), out);
            std::fputc('\n', out);
        }
        total_entries += compiled.data.catalog_entries.size();
    }

    std::fclose(out);
    log_info("vne_bake: catalogo extraido a '%s' (%zu entradas de %d guiones)", out_path,
              total_entries, script_count);
    return 0;
}

// vne_bake catalog-compile <entrada.csv> <salida.vnl>: hornea un catalogo (extraido o
// traducido a mano conservando las mismas claves, ver docs/DECISIONS.md ADR-0046) a
// binario. La clave real en runtime es solo el hash hexadecimal al final de la clave
// "archivo:linea:hash" (SPEC.md #9.2): el traductor nunca lo recalcula, solo conserva la
// linea de clave tal cual y traduce la linea de texto que sigue.
int bake_catalog_compile(const char* in_path, const char* out_path) {
    std::string source;
    if (!read_whole_file(in_path, &source)) {
        log_error("vne_bake: no se pudo leer '%s'", in_path);
        return 1;
    }

    struct Entry { u32 key_hash; std::string text; };
    std::vector<Entry> entries;

    usize pos = 0;
    while (pos < source.size()) {
        usize key_end = source.find('\n', pos);
        if (key_end == std::string::npos) break;
        std::string key = source.substr(pos, key_end - pos);
        pos             = key_end + 1;

        usize text_end = source.find('\n', pos);
        if (text_end == std::string::npos) text_end = source.size();
        std::string text = source.substr(pos, text_end - pos);
        pos               = text_end + 1;

        usize last_colon = key.find_last_of(':');
        if (last_colon == std::string::npos) {
            log_error("vne_bake: '%s' clave mal formada: '%s'", in_path, key.c_str());
            continue;
        }
        std::string hex = key.substr(last_colon + 1);
        u32          key_hash = 0;
        auto res = std::from_chars(hex.data(), hex.data() + hex.size(), key_hash, 16);
        if (res.ec != std::errc()) {
            log_error("vne_bake: '%s' hash invalido en clave '%s'", in_path, key.c_str());
            continue;
        }
        entries.push_back(Entry{key_hash, text});
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.key_hash < b.key_hash; });

    std::vector<u32> keys, text_offsets;
    std::string       string_pool;
    for (const Entry& e : entries) {
        keys.push_back(e.key_hash);
        text_offsets.push_back(static_cast<u32>(string_pool.size()));
        string_pool.insert(string_pool.end(), e.text.begin(), e.text.end());
        string_pool.push_back('\0');
    }

    std::FILE* out = std::fopen(out_path, "wb");
    if (out == nullptr) {
        log_error("vne_bake: no se pudo escribir '%s'", out_path);
        return 1;
    }
    u32 header[4] = {k_vnl_magic_local, k_vnl_version_local, static_cast<u32>(entries.size()),
                      static_cast<u32>(string_pool.size())};
    bool ok = std::fwrite(header, sizeof(header), 1, out) == 1;
    if (!keys.empty()) {
        ok = ok && std::fwrite(keys.data(), sizeof(u32), keys.size(), out) == keys.size();
        ok = ok && std::fwrite(text_offsets.data(), sizeof(u32), text_offsets.size(), out) ==
                       text_offsets.size();
    }
    if (!string_pool.empty()) {
        ok = ok && std::fwrite(string_pool.data(), 1, string_pool.size(), out) ==
                       string_pool.size();
    }
    std::fclose(out);
    if (!ok) {
        log_error("vne_bake: escritura incompleta de '%s'", out_path);
        return 1;
    }

    log_info("vne_bake: %s -> %s (%zu entradas)", in_path, out_path, entries.size());
    return 0;
}

// --- Mapas: TMX (Tiled) -> .vnm (ADR de M9 en docs/DECISIONS.md: SPEC.md #11 no da el
// layout binario, asi que se diseña la version mas simple). Subconjunto deliberado de
// TMX: una sola capa de tiles ("tiles"), una sola capa de colision ("collision", 0/1),
// ambas con encoding="csv" sin comprimir, y un objectgroup ("triggers") con rectangulos
// y una <property name="script" value="..."/>. Tiled exporta CSV sin comprimir por
// defecto, asi que un .tmx real autorado con cuidado encaja aqui: no es un parser XML
// general (sin dependencia nueva, SPEC.md #3), es lo minimo para este subconjunto.
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
    i64 value = 0;
    auto res = std::from_chars(v.data(), v.data() + v.size(), value);
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

// Extrae el contenido de <data encoding="csv">...</data> dentro de la primera capa cuyo
// atributo name coincida, buscando desde `from`. Devuelve el offset justo despues de
// </layer> en *out_layer_end, para que el llamante siga buscando la siguiente capa a
// partir de ahi.
bool extract_layer_csv(std::string_view xml, std::string_view layer_name, usize from,
                        std::vector<u16>* out_values, usize* out_layer_end) {
    usize search_from = from;
    for (;;) {
        usize layer_start = xml.find("<layer", search_from);
        if (layer_start == std::string_view::npos) {
            return false;
        }
        usize header_end = xml.find('>', layer_start);
        if (header_end == std::string_view::npos) {
            return false;
        }
        std::string_view header = xml.substr(layer_start, header_end - layer_start);
        usize             layer_close = xml.find("</layer>", header_end);
        if (layer_close == std::string_view::npos) {
            return false;
        }
        *out_layer_end = layer_close + std::string_view("</layer>").size();

        if (find_tag_attr(header, "name") == layer_name) {
            usize data_open = xml.find("<data", header_end);
            usize data_gt    = xml.find('>', data_open);
            usize data_close = xml.find("</data>", data_gt);
            if (data_open == std::string_view::npos || data_open > layer_close ||
                data_gt == std::string_view::npos || data_close == std::string_view::npos) {
                return false;
            }
            *out_values =
                parse_csv_u16(xml.substr(data_gt + 1, data_close - (data_gt + 1)));
            return true;
        }
        search_from = layer_close + 1;
    }
}

}  // namespace

// vne_bake map <entrada.tmx> <salida.vnm> (SPEC.md #11).
int bake_map(const char* in_path, const char* out_path) {
    std::string xml;
    if (!read_whole_file(in_path, &xml)) {
        log_error("vne_bake: no se pudo leer '%s'", in_path);
        return 1;
    }

    usize map_start = xml.find("<map");
    usize map_end    = map_start == std::string::npos ? std::string::npos : xml.find('>', map_start);
    if (map_start == std::string::npos || map_end == std::string::npos) {
        log_error("vne_bake: '%s' no tiene una etiqueta <map>", in_path);
        return 1;
    }
    std::string_view map_tag = std::string_view(xml).substr(map_start, map_end - map_start);
    i64               grid_w   = find_tag_attr_int(map_tag, "width");
    i64               grid_h   = find_tag_attr_int(map_tag, "height");
    i64               tile_size = find_tag_attr_int(map_tag, "tilewidth");
    if (grid_w <= 0 || grid_h <= 0 || tile_size <= 0) {
        log_error("vne_bake: '%s' <map> sin width/height/tilewidth validos", in_path);
        return 1;
    }
    usize tile_count = static_cast<usize>(grid_w * grid_h);

    std::vector<u16> tiles, collision;
    usize             layer_end = map_end;
    if (!extract_layer_csv(xml, "tiles", map_end, &tiles, &layer_end) ||
        tiles.size() != tile_count) {
        log_error("vne_bake: '%s' capa 'tiles' ausente o de tamano incorrecto", in_path);
        return 1;
    }
    usize collision_end = 0;
    if (!extract_layer_csv(xml, "collision", map_end, &collision, &collision_end) ||
        collision.size() != tile_count) {
        log_error("vne_bake: '%s' capa 'collision' ausente o de tamano incorrecto", in_path);
        return 1;
    }

    std::vector<MapTrigger> triggers;
    std::string              string_pool;
    usize                    group_start = xml.find("<objectgroup");
    if (group_start != std::string::npos) {
        usize group_close = xml.find("</objectgroup>", group_start);
        usize object_from  = xml.find('>', group_start) + 1;  // tras el <objectgroup ...>
        while (object_from < group_close) {
            // "<object " (con el espacio) para no volver a encontrar el propio
            // "<objectgroup": "<object" es un prefijo de esa palabra.
            usize object_start = xml.find("<object ", object_from);
            if (object_start == std::string::npos || object_start > group_close) {
                break;
            }
            usize object_hdr_end = xml.find('>', object_start);
            std::string_view obj_tag =
                std::string_view(xml).substr(object_start, object_hdr_end - object_start);

            MapTrigger trig{};
            trig.tile_x = static_cast<u16>(find_tag_attr_int(obj_tag, "x") / tile_size);
            trig.tile_y = static_cast<u16>(find_tag_attr_int(obj_tag, "y") / tile_size);
            trig.tile_w = static_cast<u16>(find_tag_attr_int(obj_tag, "width", tile_size) / tile_size);
            trig.tile_h = static_cast<u16>(find_tag_attr_int(obj_tag, "height", tile_size) / tile_size);

            usize object_end = xml.find("</object>", object_hdr_end);
            usize search_bound = object_end == std::string::npos ? group_close : object_end;
            usize prop_start   = xml.find("<property", object_hdr_end);
            std::string script_path;
            if (prop_start != std::string::npos && prop_start < search_bound) {
                usize prop_hdr_end = xml.find('>', prop_start);
                std::string_view prop_tag =
                    std::string_view(xml).substr(prop_start, prop_hdr_end - prop_start);
                script_path = std::string(find_tag_attr(prop_tag, "value"));
            }
            trig.script_path_offset = static_cast<u32>(string_pool.size());
            string_pool.insert(string_pool.end(), script_path.begin(), script_path.end());
            string_pool.push_back('\0');
            triggers.push_back(trig);

            object_from = (object_end == std::string::npos ? group_close : object_end + 1);
        }
    }

    std::FILE* out = std::fopen(out_path, "wb");
    if (out == nullptr) {
        log_error("vne_bake: no se pudo escribir '%s'", out_path);
        return 1;
    }
    u32 header[7] = {
        k_vnm_magic, k_vnm_version, static_cast<u32>(grid_w), static_cast<u32>(grid_h),
        static_cast<u32>(tile_size), static_cast<u32>(triggers.size()),
        static_cast<u32>(string_pool.size()),
    };
    bool ok = std::fwrite(header, sizeof(header), 1, out) == 1;
    ok = ok && std::fwrite(tiles.data(), sizeof(u16), tiles.size(), out) == tiles.size();

    usize collision_bytes = (tile_count + 7) / 8;
    std::vector<u8> collision_bits(collision_bytes, 0);
    for (usize i = 0; i < tile_count; ++i) {
        if (collision[i] != 0) {
            collision_bits[i / 8] = static_cast<u8>(collision_bits[i / 8] | (1u << (i % 8)));
        }
    }
    ok = ok && std::fwrite(collision_bits.data(), 1, collision_bytes, out) == collision_bytes;
    if (!triggers.empty()) {
        ok = ok && std::fwrite(triggers.data(), sizeof(MapTrigger), triggers.size(), out) ==
                       triggers.size();
    }
    if (!string_pool.empty()) {
        ok = ok && std::fwrite(string_pool.data(), 1, string_pool.size(), out) ==
                       string_pool.size();
    }
    std::fclose(out);
    if (!ok) {
        log_error("vne_bake: escritura incompleta de '%s'", out_path);
        return 1;
    }

    log_info("vne_bake: %s -> %s (%lldx%lld tiles, %zu triggers)", in_path, out_path, grid_w,
              grid_h, triggers.size());
    return 0;
}

// --- Atlas: empaquetador shelf real sobre PNGs de assets_src/png/ (ADR-0025), con
// respaldo procedural si el directorio no tiene ninguno (ADR-0011: un checkout limpio
// debe seguir compilando y ejecutandose sin assets reales).

constexpr i32 k_atlas_w = 1024;
constexpr i32 k_atlas_h = 1024;
constexpr i32 k_padding = 1;

struct DecodedImage {
    i32 w = 0, h = 0;
    u8* pixels = nullptr;  // RGBA8, propiedad de stb_image (stbi_image_free al final)
};

// Shelf packer: ordena por alto descendente y coloca de izquierda a derecha, saltando de
// estante cuando no cabe (skill vne-rendering: mismo principio que el shelf packer de
// glyph_cache.cpp en M2, aqui para rects de sprite en vez de glifos).
bool pack_shelf(const std::vector<DecodedImage>& images, std::vector<SpriteRect>* out) {
    std::vector<u32> order(images.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
        return images[a].h > images[b].h;
    });

    out->assign(images.size(), SpriteRect{});
    i32 cursor_x = 0, cursor_y = 0, shelf_height = 0;
    for (u32 idx : order) {
        const DecodedImage& img = images[idx];
        if (cursor_x + img.w + k_padding > k_atlas_w) {
            cursor_y += shelf_height + k_padding;
            cursor_x     = 0;
            shelf_height = 0;
        }
        if (cursor_y + img.h + k_padding > k_atlas_h) {
            return false;  // el atlas de 1024x1024 no tiene sitio para todo
        }
        (*out)[idx] = SpriteRect{static_cast<u16>(cursor_x), static_cast<u16>(cursor_y),
                                  static_cast<u16>(img.w), static_cast<u16>(img.h)};
        cursor_x += img.w + k_padding;
        shelf_height = shelf_height > img.h ? shelf_height : img.h;
    }
    return true;
}

int bake_atlas_from_png(const std::vector<std::string>& png_paths) {
    std::vector<DecodedImage> images;
    images.reserve(png_paths.size());
    for (const std::string& path : png_paths) {
        int w = 0, h = 0, channels = 0;
        u8* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (pixels == nullptr) {
            log_error("vne_bake: no se pudo decodificar '%s'", path.c_str());
            continue;
        }
        images.push_back(DecodedImage{w, h, pixels});
    }
    if (images.empty()) {
        log_error("vne_bake: ningun PNG valido en assets_src/png/");
        return 1;
    }

    std::vector<SpriteRect> placements;
    if (!pack_shelf(images, &placements)) {
        for (const DecodedImage& img : images) {
            stbi_image_free(img.pixels);
        }
        log_error("vne_bake: %zu sprites no caben en un atlas de %dx%d", images.size(),
                  k_atlas_w, k_atlas_h);
        return 1;
    }

    std::vector<u8> atlas(static_cast<usize>(k_atlas_w) * k_atlas_h * 4, 0);
    for (usize i = 0; i < images.size(); ++i) {
        const DecodedImage& img  = images[i];
        const SpriteRect&   rect = placements[i];
        for (i32 row = 0; row < img.h; ++row) {
            u8*       dst = &atlas[(static_cast<usize>(rect.y + row) * k_atlas_w + rect.x) * 4];
            const u8* src = &img.pixels[static_cast<usize>(row) * img.w * 4];
            std::memcpy(dst, src, static_cast<usize>(img.w) * 4);
        }
        stbi_image_free(img.pixels);
    }

    ensure_directory_exists("assets_baked");

    qoi_desc desc{};
    desc.width      = k_atlas_w;
    desc.height     = k_atlas_h;
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;
    if (!qoi_write("assets_baked/atlas_00.qoi", atlas.data(), &desc)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.qoi");
        return 1;
    }
    if (!write_atlas_bin("assets_baked/atlas_00.bin", k_atlas_w, k_atlas_h, placements)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.bin");
        return 1;
    }

    log_info("vne_bake: atlas_00.qoi (%dx%d, %zu sprites reales de assets_src/png/, CC0 "
              "Kenney UI Pack) y atlas_00.bin generados",
              k_atlas_w, k_atlas_h, images.size());
    return 0;
}

// Respaldo si assets_src/png/ no tiene ningun PNG (checkout sin los assets de
// ADR-0025): rejilla de colores solidos, mismo formato de salida que el empaquetador
// real para que main.cpp no necesite dos rutas de lectura distintas.
int bake_atlas_procedural_fallback() {
    constexpr i32 k_grid_cols = 4;
    constexpr i32 k_grid_rows = 4;
    constexpr i32 k_cell_size = 128;
    constexpr i32 k_w         = k_grid_cols * k_cell_size;
    constexpr i32 k_h         = k_grid_rows * k_cell_size;
    constexpr u8  k_colors[k_grid_rows * k_grid_cols][3] = {
        {230, 25, 75},  {60, 180, 75},   {255, 225, 25}, {0, 130, 200},
        {245, 130, 48}, {145, 30, 180},  {70, 240, 240}, {240, 50, 230},
        {210, 245, 60}, {250, 190, 212}, {0, 128, 128},  {220, 190, 255},
        {170, 110, 40}, {255, 250, 200}, {128, 0, 0},    {170, 255, 195},
    };

    ensure_directory_exists("assets_baked");

    std::vector<u8> pixels(static_cast<usize>(k_h) * k_w * 4);
    std::vector<SpriteRect> sprites;
    for (i32 row = 0; row < k_grid_rows; ++row) {
        for (i32 col = 0; col < k_grid_cols; ++col) {
            const u8* rgb = k_colors[row * k_grid_cols + col];
            for (i32 y = 0; y < k_cell_size; ++y) {
                for (i32 x = 0; x < k_cell_size; ++x) {
                    i32 px  = col * k_cell_size + x;
                    i32 py  = row * k_cell_size + y;
                    u8* dst = &pixels[(static_cast<usize>(py) * k_w + px) * 4];
                    dst[0] = rgb[0];
                    dst[1] = rgb[1];
                    dst[2] = rgb[2];
                    dst[3] = 255;
                }
            }
            sprites.push_back(SpriteRect{static_cast<u16>(col * k_cell_size),
                                          static_cast<u16>(row * k_cell_size), k_cell_size,
                                          k_cell_size});
        }
    }

    qoi_desc desc{};
    desc.width      = k_w;
    desc.height     = k_h;
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;
    if (!qoi_write("assets_baked/atlas_00.qoi", pixels.data(), &desc)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.qoi");
        return 1;
    }
    if (!write_atlas_bin("assets_baked/atlas_00.bin", k_w, k_h, sprites)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.bin");
        return 1;
    }

    log_info("vne_bake: assets_src/png/ vacio, atlas_00.qoi procedural (%dx%d, rejilla "
              "%dx%d) generado como respaldo",
              k_w, k_h, k_grid_cols, k_grid_rows);
    return 0;
}

int bake_atlas() {
    std::vector<std::string> png_paths = list_png_files("assets_src/png");
    if (!png_paths.empty()) {
        return bake_atlas_from_png(png_paths);
    }
    return bake_atlas_procedural_fallback();
}

}  // namespace

// Uso: vne_bake [atlas | script <in.vns> <out.vnc> | map <in.tmx> <out.vnm> |
//                catalog-extract <out.csv> <guion.vns...> | catalog-compile <in.csv> <out.vnl>]
// Sin argumentos (o "atlas"): empaqueta assets_src/png/*.png si hay alguno (ADR-0025), o
// genera el placeholder procedural de respaldo si no.
int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "script") == 0) {
        if (argc < 4) {
            log_error("uso: vne_bake script <entrada.vns> <salida.vnc>");
            return 1;
        }
        return bake_script(argv[2], argv[3]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "map") == 0) {
        if (argc < 4) {
            log_error("uso: vne_bake map <entrada.tmx> <salida.vnm>");
            return 1;
        }
        return bake_map(argv[2], argv[3]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "catalog-extract") == 0) {
        if (argc < 4) {
            log_error("uso: vne_bake catalog-extract <salida.csv> <guion1.vns> [guion2.vns ...]");
            return 1;
        }
        return bake_catalog_extract(argv[2], argc - 3, argv + 3);
    }
    if (argc >= 2 && std::strcmp(argv[1], "catalog-compile") == 0) {
        if (argc < 4) {
            log_error("uso: vne_bake catalog-compile <entrada.csv> <salida.vnl>");
            return 1;
        }
        return bake_catalog_compile(argv[2], argv[3]);
    }
    return bake_atlas();
}
