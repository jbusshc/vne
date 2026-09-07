#include <cstdio>
#include <cstring>
#include <string>

#include "script/compiler.h"
#include "script/parser.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

// qoi.h es codigo de terceros: sus conversiones implicitas int/uchar no son un problema
// de nuestro codigo, pero MSVC las marca bajo /W4 (igual que con doctest, ver
// tests/CMakeLists.txt).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267)
#endif
#define QOI_IMPLEMENTATION
#include <qoi.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "base/log.h"
#include "base/types.h"

// Sin <filesystem>: sus headers usan try/catch internamente y disparan C4530 bajo MSVC
// con las excepciones desactivadas (SPEC.md #4). Crear un directorio es simple de sobra
// para no necesitar esa dependencia.
static void ensure_directory_exists(const char* path) {
#if defined(_WIN32)
    CreateDirectoryA(path, nullptr);
#else
    mkdir(path, 0755);
#endif
}

// Genera un atlas placeholder PROCEDURAL (rejilla de colores solidos obvios), no un
// empaquetador de sprites real: SPEC.md #6 prohibe inventar contenido de juego, y
// assets_src/png/ todavia no tiene sprites reales que empaquetar (ADR-0011). Cuando
// existan, este archivo se sustituye por un empaquetador shelf/skyline sobre PNGs reales.

namespace {

constexpr i32 k_grid_cols = 4;
constexpr i32 k_grid_rows = 4;
constexpr i32 k_cell_size = 128;
constexpr i32 k_atlas_w   = k_grid_cols * k_cell_size;
constexpr i32 k_atlas_h   = k_grid_rows * k_cell_size;

constexpr u8 k_cell_colors[k_grid_rows * k_grid_cols][3] = {
    {230, 25, 75},  {60, 180, 75},   {255, 225, 25}, {0, 130, 200},
    {245, 130, 48}, {145, 30, 180},  {70, 240, 240}, {240, 50, 230},
    {210, 245, 60}, {250, 190, 212}, {0, 128, 128},  {220, 190, 255},
    {170, 110, 40}, {255, 250, 200}, {128, 0, 0},    {170, 255, 195},
};

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

int bake_atlas() {
    ensure_directory_exists("assets_baked");

    static u8 pixels[static_cast<usize>(k_atlas_h) * k_atlas_w * 4];
    for (i32 row = 0; row < k_grid_rows; ++row) {
        for (i32 col = 0; col < k_grid_cols; ++col) {
            const u8* rgb = k_cell_colors[row * k_grid_cols + col];
            for (i32 y = 0; y < k_cell_size; ++y) {
                for (i32 x = 0; x < k_cell_size; ++x) {
                    i32 px  = col * k_cell_size + x;
                    i32 py  = row * k_cell_size + y;
                    u8* dst = &pixels[(static_cast<usize>(py) * k_atlas_w + px) * 4];
                    dst[0]  = rgb[0];
                    dst[1]  = rgb[1];
                    dst[2]  = rgb[2];
                    dst[3]  = 255;
                }
            }
        }
    }

    qoi_desc desc{};
    desc.width      = k_atlas_w;
    desc.height     = k_atlas_h;
    desc.channels   = 4;
    desc.colorspace = QOI_SRGB;
    if (!qoi_write("assets_baked/atlas_00.qoi", pixels, &desc)) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.qoi");
        return 1;
    }

    // Formato .bin de M1, deliberadamente el mas simple posible (ADR-0011): rejilla de
    // celdas iguales, no el atlas.bin final de SPEC.md #11.
    std::FILE* bin = std::fopen("assets_baked/atlas_00.bin", "wb");
    if (bin == nullptr) {
        log_error("vne_bake: fallo al escribir assets_baked/atlas_00.bin");
        return 1;
    }
    const u32 header[6] = {
        0x54414E56u,  // 'VNAT'
        1u,           // version
        static_cast<u32>(k_grid_cols),
        static_cast<u32>(k_grid_rows),
        static_cast<u32>(k_cell_size),
        static_cast<u32>(k_cell_size),
    };
    std::fwrite(header, sizeof(header), 1, bin);
    std::fclose(bin);

    log_info("vne_bake: atlas_00.qoi (%dx%d, rejilla %dx%d de %dpx) y atlas_00.bin generados",
              k_atlas_w, k_atlas_h, k_grid_cols, k_grid_rows, k_cell_size);
    return 0;
}

}  // namespace

// Uso: vne_bake [atlas | script <entrada.vns> <salida.vnc>]
// Sin argumentos (o "atlas"): genera el atlas placeholder de M1 (compatibilidad).
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
