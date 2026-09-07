#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

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

// Uso: vne_bake [atlas | script <entrada.vns> <salida.vnc>]
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
    return bake_atlas();
}
