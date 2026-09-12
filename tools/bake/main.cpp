#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "base/hash.h"
#include "game/map_format.h"
#include "script/asset_validate.h"
#include "script/hash_collisions.h"

#include <hb.h>
#include <hb-subset.h>
#include "script/compiler.h"
#include "script/map_bake.h"
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
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
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

// extension incluye el punto (".png"); nullptr o "" lista todo el directorio (M11,
// vne_bake pack, que necesita recorrer assets_baked/ entero sin filtrar por tipo).
static std::vector<std::string> list_files_in_dir(const char* dir, const char* extension) {
    std::vector<std::string> result;
    auto matches = [&](const std::string& name) {
        if (extension == nullptr || extension[0] == '\0') {
            return true;
        }
        usize ext_len = std::strlen(extension);
        return name.size() > ext_len &&
               name.compare(name.size() - ext_len, ext_len, extension) == 0;
    };
#if defined(_WIN32)
    std::string        pattern = std::string(dir) + "\\*";
    WIN32_FIND_DATAA    find_data;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return result;
    }
    do {
        std::string name = find_data.cFileName;
        if (name[0] != '.' && matches(name)) {
            result.push_back(std::string(dir) + "/" + name);
        }
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
        if (name[0] != '.' && matches(name)) {
            result.push_back(std::string(dir) + "/" + name);
        }
    }
    closedir(d);
#endif
    return result;
}

static std::vector<std::string> list_png_files(const char* dir) {
    return list_files_in_dir(dir, ".png");
}

namespace {

// Formato de assets_baked/atlas_00.bin. v2 (ADR-0025) era un manifiesto generico de
// rectangulos sin nombres; **v3 (M13) le añade la tabla de nombres logicos**, que es lo que
// convierte al atlas en el registro de assets que SPEC.md #11 pide y que M11 dejo
// explicitamente aplazado ("no hay ningun consumidor que pida un sprite por nombre
// todavia"). Ahora si lo hay: el compilador del DSL, para validar @show/@bg (cierra
// ADR-0022), y el renderizado de fondos y actores.
//
// No hay un archivo de registro aparte a proposito: la tabla de nombres del atlas ES el
// registro. Un segundo formato que dijera lo mismo solo podria desincronizarse.
struct SpriteRect {
    u16 x, y, w, h;
};

// Relleno explicito (ADR-0028): lo escribe y lo lee codigo distinto, asi que el layout no
// puede depender de lo que el compilador decida rellenar.
struct AtlasEntry {
    u64 name_hash;    // fnv1a_u64 del nombre logico
    u32 name_offset;  // offset dentro del pool de nombres
    u16 x, y, w, h;
    u8  _pad[4];
};
static_assert(sizeof(AtlasEntry) == 24);

bool write_atlas_bin(const char* path, i32 atlas_w, i32 atlas_h,
                      const std::vector<SpriteRect>& sprites,
                      const std::vector<std::string>& names) {
    // Pool de nombres + entradas ORDENADAS POR HASH: el runtime las busca por biseccion,
    // igual que hace pak.cpp con las entradas del .pak.
    std::string             name_pool;
    std::vector<AtlasEntry> entries;
    entries.reserve(sprites.size());
    for (usize i = 0; i < sprites.size(); ++i) {
        const std::string& name = i < names.size() ? names[i] : std::string();
        AtlasEntry         e{};
        e.name_hash   = fnv1a_u64(name.c_str());
        e.name_offset = static_cast<u32>(name_pool.size());
        e.x           = sprites[i].x;
        e.y           = sprites[i].y;
        e.w           = sprites[i].w;
        e.h           = sprites[i].h;
        entries.push_back(e);
        name_pool.append(name);
        name_pool.push_back('\0');
    }
    std::sort(entries.begin(), entries.end(),
              [](const AtlasEntry& a, const AtlasEntry& b) { return a.name_hash < b.name_hash; });

    std::FILE* bin = std::fopen(path, "wb");
    if (bin == nullptr) {
        return false;
    }
    const u32 header[6] = {
        0x54414E56u,  // 'VNAT'
        3u,           // version
        static_cast<u32>(atlas_w),
        static_cast<u32>(atlas_h),
        static_cast<u32>(entries.size()),
        static_cast<u32>(name_pool.size()),
    };
    bool ok = std::fwrite(header, sizeof(header), 1, bin) == 1;
    if (!entries.empty()) {
        ok = ok && std::fwrite(entries.data(), sizeof(AtlasEntry), entries.size(), bin) ==
                       entries.size();
    }
    if (!name_pool.empty()) {
        ok = ok && std::fwrite(name_pool.data(), 1, name_pool.size(), bin) == name_pool.size();
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

    // Validacion de nombres de asset (M13, cierra ADR-0022): un actor, pose o fondo que no
    // esta en el registro es error de compilacion con archivo y linea, igual que ya lo era
    // una etiqueta desconocida.
    //
    // Si el atlas todavia no esta horneado no se valida, solo se avisa: compilar un guion en
    // un arbol recien clonado tiene que seguir funcionando, y el build ya hornea el atlas
    // antes que los guiones. Hacerlo fatal convertiria un orden de build en un error del
    // autor del guion, que no tiene nada que ver.
    AssetRegistry registry;
    if (AssetRegistry::load_from_atlas_bin("assets_baked/atlas_00.bin", &registry)) {
        validate_asset_names(parsed.instructions, in_path, registry, &parsed.errors);
    } else {
        log_warn("vne_bake: no se pudo leer 'assets_baked/atlas_00.bin'; los nombres de "
                 "actor, pose y fondo NO se validan en esta compilacion");
    }

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

// --- vne_bake font: subconjunto de glifos (M13) ----------------------------------------
//
// SPEC.md #11 lista `vne_bake font` como la herramienta que faltaba, y M13 le pone un
// criterio concreto: "el repositorio deja de contener una fuente de 9.5 MB". NotoSansJP.ttf
// pesa 9.5 MB porque cubre el japones entero, y el proyecto usaba una fraccion minuscula.
//
// Se apoya en hb-subset, que es parte de HarfBuzz — **ya esta en la lista cerrada de
// dependencias de SPEC.md #3**, no es una dependencia nueva; solo habia que enlazar su
// segundo target (harfbuzz-subset), que su propio CMake ya compila por defecto.
//
// La alternativa que describe la tabla de SPEC.md #11 (rasterizar a un atlas horneado
// `font_*.atlas` + metricas) se descarto: chocaria con el diseño de rasterizar CJK bajo
// demanda que fija el skill vne-rendering, porque obligaria a decidir por adelantado cada
// glifo y tamaño. Un TTF mas pequeño conserva ese diseño intacto y ademas sigue valiendo
// para cualquier tamaño de punto.

// Conjunto base que siempre se incluye: ASCII imprimible mas las letras acentuadas y signos
// del espanol, que es el idioma en que se autoran los guiones (ADR-0048). Sin esto, un texto
// que todavia no existe en ningun archivo (un nombre escrito por el jugador, un mensaje de
// error) saldria con cuadraditos.
void add_base_codepoints(hb_set_t* set) {
    for (u32 cp = 0x20; cp <= 0x7E; ++cp) {
        hb_set_add(set, cp);
    }
    static const u32 k_spanish[] = {
        0x00A1, 0x00BF,                                          // ¡ ¿
        0x00C1, 0x00C9, 0x00CD, 0x00D3, 0x00DA, 0x00DC, 0x00D1,  // ÁÉÍÓÚÜÑ
        0x00E1, 0x00E9, 0x00ED, 0x00F3, 0x00FA, 0x00FC, 0x00F1,  // áéíóúüñ
        0x00AB, 0x00BB, 0x2018, 0x2019, 0x201C, 0x201D,          // « » ‘ ’ “ ”
        0x2013, 0x2014, 0x2026,                                   // – — …
    };
    for (u32 cp : k_spanish) {
        hb_set_add(set, cp);
    }

    // Kana y puntuacion CJK completos. No es contenido: es la cobertura minima para que las
    // funciones CJK del motor (kinsoku en text/layout.cpp, furigana) sigan siendo usables y
    // testeables. Sin kana, un subconjunto deja esa parte del motor muerta aunque el codigo
    // siga ahi — que es exactamente lo que paso la primera vez que se subseteo y fallaron
    // test_kinsoku y test_ruby. Son unos 300 glifos, calderilla frente a los 9.5 MB.
    //
    // Los KANJI no entran aqui: son miles y dependen del contenido, asi que salen de los
    // archivos de texto que se le pasen al comando.
    for (u32 cp = 0x3000; cp <= 0x30FF; ++cp) {  // puntuacion CJK + hiragana + katakana
        hb_set_add(set, cp);
    }
    for (u32 cp = 0xFF01; cp <= 0xFF65; ++cp) {  // formas de ancho completo (（ ） ！ ？...)
        hb_set_add(set, cp);
    }
}

// Añade cada codepoint que aparezca en un archivo de texto UTF-8 (un .vns, un .csv de
// catalogo). Asi el subconjunto cubre exactamente lo que el proyecto escribe, incluido el
// japones real el dia que lo haya.
bool add_codepoints_from_file(const char* path, hb_set_t* set) {
    std::string content;
    if (!read_whole_file(path, &content)) {
        log_error("vne_bake font: no se pudo leer '%s'", path);
        return false;
    }
    usize i = 0;
    while (i < content.size()) {
        u8  c0  = static_cast<u8>(content[i]);
        u32 cp  = c0;
        usize len = 1;
        if ((c0 & 0xE0) == 0xC0 && i + 1 < content.size()) {
            len = 2;
            cp  = (static_cast<u32>(c0 & 0x1F) << 6) | (static_cast<u8>(content[i + 1]) & 0x3F);
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < content.size()) {
            len = 3;
            cp  = (static_cast<u32>(c0 & 0x0F) << 12) |
                 (static_cast<u32>(static_cast<u8>(content[i + 1]) & 0x3F) << 6) |
                 (static_cast<u8>(content[i + 2]) & 0x3F);
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < content.size()) {
            len = 4;
            cp  = (static_cast<u32>(c0 & 0x07) << 18) |
                 (static_cast<u32>(static_cast<u8>(content[i + 1]) & 0x3F) << 12) |
                 (static_cast<u32>(static_cast<u8>(content[i + 2]) & 0x3F) << 6) |
                 (static_cast<u8>(content[i + 3]) & 0x3F);
        }
        if (cp >= 0x20) {  // los de control no tienen glifo
            hb_set_add(set, cp);
        }
        i += len;
    }
    return true;
}

// vne_bake font <entrada.ttf> <salida.ttf> [archivo_de_texto ...]
int bake_font(const char* in_path, const char* out_path, int extra_count, char** extra_paths) {
    std::string ttf;
    if (!read_whole_file(in_path, &ttf)) {
        log_error("vne_bake font: no se pudo leer '%s'", in_path);
        return 1;
    }

    hb_blob_t* blob = hb_blob_create(ttf.data(), static_cast<unsigned>(ttf.size()),
                                      HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    if (hb_face_get_glyph_count(face) == 0) {
        hb_face_destroy(face);
        log_error("vne_bake font: '%s' no parece un TTF valido (0 glifos)", in_path);
        return 1;
    }

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (input == nullptr) {
        hb_face_destroy(face);
        log_error("vne_bake font: hb_subset_input_create_or_fail fallo");
        return 1;
    }
    hb_set_t* unicodes = hb_subset_input_unicode_set(input);
    add_base_codepoints(unicodes);
    for (int i = 0; i < extra_count; ++i) {
        if (!add_codepoints_from_file(extra_paths[i], unicodes)) {
            hb_subset_input_destroy(input);
            hb_face_destroy(face);
            return 1;
        }
    }
    unsigned requested = hb_set_get_population(unicodes);

    hb_face_t* subset = hb_subset_or_fail(face, input);
    hb_subset_input_destroy(input);
    if (subset == nullptr) {
        hb_face_destroy(face);
        log_error("vne_bake font: hb_subset_or_fail fallo sobre '%s'", in_path);
        return 1;
    }

    hb_blob_t*  out_blob = hb_face_reference_blob(subset);
    unsigned    out_size = 0;
    const char* out_data = hb_blob_get_data(out_blob, &out_size);
    bool        ok       = false;
    if (out_data != nullptr && out_size > 0) {
        std::FILE* f = std::fopen(out_path, "wb");
        if (f != nullptr) {
            ok = std::fwrite(out_data, 1, out_size, f) == out_size;
            std::fclose(f);
        }
    }
    hb_blob_destroy(out_blob);
    hb_face_destroy(subset);
    hb_face_destroy(face);

    if (!ok) {
        log_error("vne_bake font: no se pudo escribir '%s'", out_path);
        return 1;
    }
    log_info("vne_bake font: %s -> %s (%zu KB -> %u KB, %u codepoints pedidos)", in_path,
              out_path, ttf.size() / 1024, out_size / 1024, requested);
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

    HashCollisionCheck catalog_keys;
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
            // Colision de clave de catalogo (M13): ADR-0047 decidio que en runtime solo
            // cuenta el hash, no archivo:linea. Dos textos distintos con el mismo hash se
            // resolverian a la MISMA traduccion, y el sintoma —"esta linea sale traducida
            // como otra"— no apunta a ningun sitio. Es improbabilisimo con 32 bits, pero
            // detectarlo cuesta un diccionario y no detectarlo cuesta una tarde.
            std::string previous_text;
            if (!catalog_keys.add(entry.text, fnv1a_u32(entry.text.c_str()), &previous_text)) {
                log_error("vne_bake catalog-extract: colision de hash entre dos textos "
                          "distintos, que compartirian traduccion: '%s' y '%s'",
                          entry.text.c_str(), previous_text.c_str());
                std::fclose(out);
                return 1;
            }
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

// vne_bake map <entrada.tmx> <salida.vnm> (SPEC.md #11). El escaner de TMX vive en
// script/map_bake.cpp (vne_script_tools) y no aqui, para que los tests puedan
// ejercitarlo con un TMX literal sin archivos ni subprocesos: esta funcion es solo la
// capa de E/S y de mensajes.
int bake_map(const char* in_path, const char* out_path) {
    std::string xml;
    if (!read_whole_file(in_path, &xml)) {
        log_error("vne_bake: no se pudo leer '%s'", in_path);
        return 1;
    }

    ParsedMap   map;
    std::string error;
    if (!tmx_parse(xml, &map, &error)) {
        log_error("vne_bake: '%s': %s", in_path, error.c_str());
        return 1;
    }

    if (!write_vnm(out_path, map)) {
        log_error("vne_bake: no se pudo escribir '%s'", out_path);
        return 1;
    }

    log_info("vne_bake: %s -> %s (%ux%u tiles, %zu triggers)", in_path, out_path, map.grid_w,
              map.grid_h, map.triggers.size());
    return 0;
}

// --- Atlas: empaquetador shelf real sobre PNGs de assets_src/png/ (ADR-0025), con
// respaldo procedural si el directorio no tiene ninguno (ADR-0011: un checkout limpio
// debe seguir compilando y ejecutandose sin assets reales).

constexpr i32 k_atlas_w = 1024;
constexpr i32 k_atlas_h = 1024;
constexpr i32 k_padding = 1;

// "assets_src/png/actor_marta_neutral.png" -> "actor_marta_neutral". Sin directorio ni
// extension: el nombre logico es lo que el guion escribe, no una ruta de archivo (misma
// idea que los nombres logicos del backend de assets, M11).
std::string logical_name_from_path(const std::string& path) {
    usize slash = path.find_last_of("/\\");
    usize start = (slash == std::string::npos) ? 0 : slash + 1;
    usize dot   = path.find_last_of('.');
    usize end   = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}

struct DecodedImage {
    i32 w = 0, h = 0;
    u8* pixels = nullptr;  // RGBA8, propiedad de stb_image (stbi_image_free al final)
    // Nombre logico del sprite (M13): el del archivo sin directorio ni extension, que es
    // lo que el guion escribe y lo que valida el compilador del DSL.
    std::string name;
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
        images.push_back(DecodedImage{w, h, pixels, logical_name_from_path(path)});
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
    std::vector<std::string> names;
    names.reserve(images.size());
    for (const DecodedImage& img : images) {
        names.push_back(img.name);
    }
    if (!write_atlas_bin("assets_baked/atlas_00.bin", k_atlas_w, k_atlas_h, placements, names)) {
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
    std::vector<SpriteRect>  sprites;
    std::vector<std::string> names;
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
            // Nombre sintetico para el respaldo procedural (M13): sin el, un checkout sin
            // los PNG produciria un atlas sin registro y @show/@bg fallarian a compilar por
            // una razon que no es la del autor del guion. Se llaman "placeholder_NN" a
            // proposito, para que se vea en cualquier mensaje de error que no son assets
            // de verdad.
            char placeholder_name[32];
            std::snprintf(placeholder_name, sizeof(placeholder_name), "placeholder_%02zu",
                          sprites.size() - 1);
            names.push_back(placeholder_name);
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
    if (!write_atlas_bin("assets_baked/atlas_00.bin", k_w, k_h, sprites, names)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.bin");
        return 1;
    }

    log_info("vne_bake: assets_src/png/ vacio, atlas_00.qoi procedural (%dx%d, rejilla "
              "%dx%d) generado como respaldo",
              k_w, k_h, k_grid_cols, k_grid_rows);
    return 0;
}

// --- Placeholders de actores y fondos (M13) --------------------------------------------
//
// Los guiones de demo usan actores y fondos que no existen como asset, asi que validar
// nombres en compilacion (que es el criterio de M13) los rompia a todos. Se generan
// placeholders **obvios** para ellos: rectangulos de color plano con borde y un aspa, el
// aspecto clasico de "falta el asset" (CLAUDE.md regla 6 pide exactamente esto y dice que
// hay que decirlo en voz alta: no son arte, no son contenido de juego).
//
// Se escriben como PNG normales en assets_src/png/, asi que sustituirlos por arte de verdad
// es dejar caer un archivo con el mismo nombre; no hay nada especial en ellos.
//
// LIMITACION CONOCIDA: los fondos se generan a 256x144 y no a 1920x1080 porque van dentro
// del atlas de 1024x1024, donde un fondo a resolucion completa no cabe. Al dibujarlos se
// escalan a pantalla completa y se ven toscos, que para un placeholder es una ventaja. Un
// fondo de verdad necesitara una textura suelta (SPEC.md #11), que no existe todavia.

struct PlaceholderSpec {
    const char* name;
    i32         w, h;
    u8          r, g, b;
};

void draw_placeholder(std::vector<u8>* px, i32 w, i32 h, u8 r, u8 g, u8 b) {
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            u8* p = &(*px)[(static_cast<usize>(y) * w + x) * 4];
            // Borde de 4 px y aspa de esquina a esquina, en un tono mas claro: se lee como
            // "esto falta" a cualquier tamaño y desde cualquier distancia.
            bool border = x < 4 || y < 4 || x >= w - 4 || y >= h - 4;
            bool cross   = (x * h / w == y) || ((w - 1 - x) * h / w == y);
            u8   shade   = (border || cross) ? 255 : 0;
            p[0] = static_cast<u8>(r + (255 - r) * shade / 255);
            p[1] = static_cast<u8>(g + (255 - g) * shade / 255);
            p[2] = static_cast<u8>(b + (255 - b) * shade / 255);
            p[3] = 255;
        }
    }
}

int bake_placeholders() {
    // Los que usan los tres guiones de demo, mas nada. La lista es explicita y no se deriva
    // de los guiones a proposito: si se generara un placeholder por cada nombre que aparece
    // en un .vns, la validacion de M13 no podria detectar ni un solo typo — se generaria el
    // asset del nombre mal escrito y compilaria igual.
    const PlaceholderSpec specs[] = {
        {"bg_fondo_dia", 256, 144, 90, 130, 170},
        {"bg_fondo_noche", 256, 144, 30, 35, 70},
        {"actor_marta_neutral", 128, 256, 150, 80, 90},
        {"actor_personaje_a_pose_neutral", 128, 256, 80, 130, 90},
        {"actor_personaje_a_pose_feliz", 128, 256, 110, 160, 90},
        {"actor_personaje_b_pose_neutral", 128, 256, 120, 100, 160},
    };

    ensure_directory_exists("assets_src");
    ensure_directory_exists("assets_src/png");

    for (const PlaceholderSpec& spec : specs) {
        std::vector<u8> pixels(static_cast<usize>(spec.w) * spec.h * 4);
        draw_placeholder(&pixels, spec.w, spec.h, spec.r, spec.g, spec.b);

        char path[256];
        std::snprintf(path, sizeof(path), "assets_src/png/%s.png", spec.name);
        if (stbi_write_png(path, spec.w, spec.h, 4, pixels.data(), spec.w * 4) == 0) {
            log_error("vne_bake: fallo al escribir '%s'", path);
            return 1;
        }
    }

    log_info("vne_bake: %zu placeholders de actor/fondo generados en assets_src/png/ "
              "(rectangulos de color con aspa, NO son arte: sustituyelos dejando caer un "
              "PNG con el mismo nombre)",
              sizeof(specs) / sizeof(specs[0]));
    return 0;
}

int bake_atlas() {
    std::vector<std::string> png_paths = list_png_files("assets_src/png");
    if (!png_paths.empty()) {
        return bake_atlas_from_png(png_paths);
    }
    return bake_atlas_procedural_fallback();
}

// --- Empaquetado (M11, SPEC.md #11): junta todo lo que el juego lee en runtime en un
// unico game.pak. Debe coincidir con assets/pak.cpp (duplicado a proposito, ver el
// comentario alli: mismo patron que el magic de .vnc entre compiler.cpp y
// script_load.cpp).

constexpr u32 k_pak_magic   = 0x4B504E56u;  // 'VNPK'
constexpr u32 k_pak_version = 1;

enum class PakEntryType : u8 { Texture, Font, Sound, Script, Map, Locale, Other };

struct PakEntry {
    u64          name_hash;
    u64          offset;
    u32          size;
    PakEntryType type;
    u8           _pad[3];
};
static_assert(sizeof(PakEntry) == 24);

PakEntryType pak_type_for_extension(const std::string& logical_name) {
    auto ends_with = [&](const char* suffix) {
        usize len = std::strlen(suffix);
        return logical_name.size() >= len &&
               logical_name.compare(logical_name.size() - len, len, suffix) == 0;
    };
    if (ends_with(".qoi") || ends_with(".bin")) return PakEntryType::Texture;
    if (ends_with(".ttf")) return PakEntryType::Font;
    if (ends_with(".ogg") || ends_with(".wav")) return PakEntryType::Sound;
    if (ends_with(".vnc")) return PakEntryType::Script;
    if (ends_with(".vnm")) return PakEntryType::Map;
    if (ends_with(".vnl")) return PakEntryType::Locale;
    return PakEntryType::Other;
}

// vne_bake pack <salida.pak> <assets_baked_dir> <assets_src_dir>. assets_baked_dir se
// recorre entero y plano (atlas, .vnc, .vnm, .vnl: todo ya horneado con nombres finales).
// De assets_src_dir solo entran ttf/ y ogg/, con ese prefijo en su nombre logico dentro
// del pak -- son los dos unicos tipos que SPEC.md #11 todavia no hornea a un formato
// propio (vne_bake font no existe hasta M13; audio es copia directa desde M6), asi que
// esta es la unica forma de que Ship no dependa de leerlos sueltos de assets_src/.
int bake_pack(const char* out_path, const char* baked_dir, const char* src_dir) {
    struct PendingEntry { std::string logical_name; std::string bytes; PakEntryType type; };
    std::vector<PendingEntry> pending;

    auto collect = [&](const std::vector<std::string>& paths, const char* prefix) {
        for (const std::string& path : paths) {
            std::string content;
            if (!read_whole_file(path.c_str(), &content)) {
                log_error("vne_bake pack: no se pudo leer '%s'", path.c_str());
                continue;
            }
            usize       slash    = path.find_last_of("/\\");
            std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
            std::string logical  = prefix != nullptr ? std::string(prefix) + filename : filename;
            pending.push_back(PendingEntry{logical, std::move(content),
                                            pak_type_for_extension(logical)});
        }
    };

    collect(list_files_in_dir(baked_dir, nullptr), nullptr);
    collect(list_files_in_dir((std::string(src_dir) + "/ttf").c_str(), nullptr), "ttf/");
    std::vector<std::string> ogg_paths = list_files_in_dir((std::string(src_dir) + "/ogg").c_str(),
                                                             nullptr);
    collect(ogg_paths, "ogg/");

    // Catalogo de musica horneado dentro del propio pak (M11): en backend suelto,
    // audio.cpp sigue escaneando assets_src/ogg/ en runtime (dir_list_by_extension), pero
    // en Ship solo existe game.pak -- no hay directorio que escanear. "ogg_catalog.bin" es
    // el mismo {track_id -> nombre logico} que antes se armaba en runtime, generado aqui
    // en su lugar. Formato: registros de tamano fijo, sin cabecera (el conteo sale de
    // dividir el tamano de la entrada del pak entre sizeof(record), mismo patron que
    // atlas_00.bin con sus SpriteRect). Debe coincidir con audio.cpp (duplicado a
    // proposito, mismo patron que el resto de formatos de este proyecto).
    struct OggCatalogRecord {
        u16  track_id;
        u8   _pad[2];
        char logical_name[64];
    };
    static_assert(sizeof(OggCatalogRecord) == 68);
    if (!ogg_paths.empty()) {
        HashCollisionCheck track_ids;
        std::string catalog_bytes;
        for (const std::string& path : ogg_paths) {
            usize       slash    = path.find_last_of("/\\");
            std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
            std::string logical  = "ogg/" + filename;
            std::string name_no_ext = filename;
            usize       dot         = name_no_ext.find_last_of('.');
            if (dot != std::string::npos) {
                name_no_ext.resize(dot);
            }
            OggCatalogRecord record{};
            record.track_id = static_cast<u16>(fnv1a_u32(name_no_ext) % 65536u);
            // Colision de track_id (M13): ADR-0034 acepto el riesgo sin deteccion. Dos
            // pistas que compartan id se confundirian al restaurar bgm_track_id de una
            // partida guardada, y el sintoma —"suena la musica de otra escena al cargar"—
            // no apunta a ningun sitio.
            std::string previous_track;
            if (!track_ids.add(name_no_ext, record.track_id, &previous_track)) {
                log_error("vne_bake pack: colision de hash entre las pistas '%s' y '%s' "
                          "(las dos dan track_id %u). Renombra una de las dos.",
                          name_no_ext.c_str(), previous_track.c_str(), record.track_id);
                return 1;
            }
            std::snprintf(record.logical_name, sizeof(record.logical_name), "%s",
                          logical.c_str());
            catalog_bytes.append(reinterpret_cast<const char*>(&record), sizeof(record));
        }
        pending.push_back(
            PendingEntry{"ogg_catalog.bin", std::move(catalog_bytes), PakEntryType::Other});
    }

    if (pending.empty()) {
        log_error("vne_bake pack: no se encontro ningun asset en '%s' ni en '%s'", baked_dir,
                   src_dir);
        return 1;
    }

    // Orden por hash ascendente: assets/pak.cpp resuelve con busqueda binaria, igual que
    // text/catalog.cpp con las claves de un .vnl.
    std::sort(pending.begin(), pending.end(), [](const PendingEntry& a, const PendingEntry& b) {
        return fnv1a_u64(a.logical_name) < fnv1a_u64(b.logical_name);
    });

    std::FILE* out = std::fopen(out_path, "wb");
    if (out == nullptr) {
        log_error("vne_bake pack: no se pudo escribir '%s'", out_path);
        return 1;
    }

    u32 header[3] = {k_pak_magic, k_pak_version, static_cast<u32>(pending.size())};
    std::fwrite(header, sizeof(u32), 3, out);

    usize data_offset = 3 * sizeof(u32) + pending.size() * sizeof(PakEntry);
    std::vector<PakEntry> entries;
    entries.reserve(pending.size());
    for (const PendingEntry& e : pending) {
        entries.push_back(PakEntry{fnv1a_u64(e.logical_name), data_offset,
                                    static_cast<u32>(e.bytes.size()), e.type, {}});
        data_offset += e.bytes.size();
    }
    std::fwrite(entries.data(), sizeof(PakEntry), entries.size(), out);
    for (const PendingEntry& e : pending) {
        std::fwrite(e.bytes.data(), 1, e.bytes.size(), out);
    }
    std::fclose(out);

    log_info("vne_bake: %zu assets empaquetados en '%s'", pending.size(), out_path);
    return 0;
}

}  // namespace

// Uso: vne_bake [atlas | script <in.vns> <out.vnc> | map <in.tmx> <out.vnm> |
//                catalog-extract <out.csv> <guion.vns...> | catalog-compile <in.csv> <out.vnl> |
//                pack <out.pak> <assets_baked_dir> <assets_src_dir>]
// Sin argumentos (o "atlas"): empaqueta assets_src/png/*.png si hay alguno (ADR-0025), o
// genera el placeholder procedural de respaldo si no.
int main(int argc, char** argv) {
    if (argc >= 5 && std::strcmp(argv[1], "font") == 0) {
        return bake_font(argv[2], argv[3], argc - 4, argv + 4);
    }
    if (argc == 4 && std::strcmp(argv[1], "font") == 0) {
        return bake_font(argv[2], argv[3], 0, nullptr);
    }
    if (argc >= 2 && std::strcmp(argv[1], "placeholders") == 0) {
        return bake_placeholders();
    }
    if (argc >= 2 && std::strcmp(argv[1], "pack") == 0) {
        if (argc < 5) {
            log_error("uso: vne_bake pack <salida.pak> <assets_baked_dir> <assets_src_dir>");
            return 1;
        }
        return bake_pack(argv[2], argv[3], argv[4]);
    }
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
