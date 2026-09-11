#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "assets/pak.h"
#include "base/hash.h"

// Tests del formato .pak y de la resolucion de ruta logica -> bytes (SPEC.md #7.4/#11,
// M11). Los fixtures se escriben a mano, mismo patron que test_save_load.cpp con
// .vnsave: no hay un .pak real generado por vne_bake todavia en el arbol de tests, asi
// que se fabrica el formato exacto byte a byte.
//
// pak_resolve() es seguro desde cualquier hilo (no toma Arena*, ver pak.h): cada test
// libera lo que resuelve con pak_release() antes de terminar, emparejado siempre.
//
// IMPORTANTE: el backend montado es estado GLOBAL del proceso, y test_main.cpp monta "."
// una vez para todo el binario. Por eso estos tests terminan con pak_mount(".") y no con
// pak_unmount(): dejarlo desmontado rompe cualquier test posterior que cargue un asset
// (asi fallo la fuente en negrita de test_typewriter_timing.cpp la primera vez).

namespace {

// Debe coincidir con assets/pak.cpp (duplicado a proposito, ver el comentario alli).
constexpr u32 k_pak_magic   = 0x4B504E56u;  // 'VNPK'
constexpr u32 k_pak_version = 1;

struct PakEntry {
    u64 name_hash;
    u64 offset;
    u32 size;
    u8  type;
    u8  _pad[3];
};
static_assert(sizeof(PakEntry) == 24);

// Escribe un .pak valido con dos entradas ("uno.txt" -> "contenido uno",
// "dos.txt" -> "otro contenido") en `path`. Las entradas se escriben ya ordenadas por
// hash ascendente, como exige la busqueda binaria de pak_resolve().
void write_test_pak(const char* path) {
    struct RawEntry { u64 hash; std::string text; };
    RawEntry entries[2] = {
        {fnv1a_u64("uno.txt"), "contenido uno"},
        {fnv1a_u64("dos.txt"), "otro contenido"},
    };
    if (entries[0].hash > entries[1].hash) {
        std::swap(entries[0], entries[1]);
    }

    std::FILE* f = std::fopen(path, "wb");
    REQUIRE(f != nullptr);

    u32 header[3] = {k_pak_magic, k_pak_version, 2};
    std::fwrite(header, sizeof(u32), 3, f);

    usize data_offset = 3 * sizeof(u32) + 2 * sizeof(PakEntry);
    PakEntry raw_entries[2];
    for (int i = 0; i < 2; ++i) {
        raw_entries[i] = PakEntry{entries[i].hash, data_offset, // NOLINT
                                   static_cast<u32>(entries[i].text.size()), 0, {}};
        data_offset += entries[i].text.size();
    }
    std::fwrite(raw_entries, sizeof(PakEntry), 2, f);
    for (const auto& e : entries) {
        std::fwrite(e.text.data(), 1, e.text.size(), f);
    }
    std::fclose(f);
}

}  // namespace

TEST_CASE("pak: backend suelto lee un archivo por su ruta logica (via assets_baked/)") {
    // Backend suelto: assets_baked/ y assets_src/{ttf,ogg}/ son directorios hermanos
    // distintos (ver el comentario de loose_full_path en pak.cpp), asi que el "root" que
    // se monta es el que los contiene a ambos, y una ruta logica normal (sin prefijo
    // ttf/ u ogg/) se busca dentro de assets_baked/.
    SDL_CreateDirectory("assets_baked");
    const char* path = "assets_baked/test_pak_loose.txt";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    std::fwrite("hola pak", 1, 8, f);
    std::fclose(f);

    pak_mount(".");
    CHECK_FALSE(pak_is_packed());

    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = false;
    REQUIRE(pak_resolve("test_pak_loose.txt", &data, &size, &owned));
    REQUIRE(size == 8);
    CHECK(owned);  // backend suelto: el llamante es responsable de liberarlo
    CHECK(std::memcmp(data, "hola pak", 8) == 0);

    pak_release(data, owned);
    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(path);
}

TEST_CASE("pak: backend suelto enruta ttf/ y ogg/ a assets_src/, el resto a assets_baked/") {
    SDL_CreateDirectory("assets_src");
    SDL_CreateDirectory("assets_src/ttf");
    const char* path = "assets_src/ttf/test_pak_font.txt";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    std::fwrite("no es un ttf de verdad", 1, 23, f);
    std::fclose(f);

    pak_mount(".");
    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = false;
    REQUIRE(pak_resolve("ttf/test_pak_font.txt", &data, &size, &owned));
    REQUIRE(size == 23);
    CHECK(std::memcmp(data, "no es un ttf de verdad", 23) == 0);

    pak_release(data, owned);
    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(path);
}

TEST_CASE("pak: backend suelto devuelve false para una ruta que no existe") {
    pak_mount(".");
    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = true;  // valor centinela para confirmar que se resetea a false
    CHECK_FALSE(pak_resolve("no_existe_de_verdad.bin", &data, &size, &owned));
    CHECK(data == nullptr);
    CHECK(size == 0);
    CHECK_FALSE(owned);
    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
}

TEST_CASE("pak: pak_resolve_loose_path da la ruta real en suelto, false en empaquetado") {
    pak_mount(".");
    char path[256] = {};
    CHECK(pak_resolve_loose_path("ogg/tema_a.wav", path, sizeof(path)));
    CHECK(std::string(path) == "./assets_src/ogg/tema_a.wav");
    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)

    const char* pak_path = "test_pak_loosepath.pak";
    write_test_pak(pak_path);
    pak_mount(pak_path);
    CHECK_FALSE(pak_resolve_loose_path("uno.txt", path, sizeof(path)));
    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(pak_path);
}

TEST_CASE("pak: backend empaquetado resuelve por nombre logico") {
    const char* path = "test_pak_file.pak";
    write_test_pak(path);

    pak_mount(path);
    CHECK(pak_is_packed());

    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = true;
    REQUIRE(pak_resolve("uno.txt", &data, &size, &owned));
    REQUIRE(size == 13);
    CHECK_FALSE(owned);  // backend empaquetado: apunta al bloque residente, no se libera
    CHECK(std::memcmp(data, "contenido uno", 13) == 0);
    pak_release(data, owned);  // no-op, pero se llama igual: mismo patron en todo caller

    REQUIRE(pak_resolve("dos.txt", &data, &size, &owned));
    REQUIRE(size == 14);
    CHECK(std::memcmp(data, "otro contenido", 14) == 0);
    pak_release(data, owned);

    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(path);
}

TEST_CASE("pak: backend empaquetado devuelve false para un nombre fuera de la tabla") {
    const char* path = "test_pak_file2.pak";
    write_test_pak(path);

    pak_mount(path);
    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = true;
    CHECK_FALSE(pak_resolve("tres.txt", &data, &size, &owned));
    CHECK(data == nullptr);
    CHECK_FALSE(owned);

    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(path);
}

TEST_CASE("pak: un .pak con magic invalido no se monta (pak_is_packed queda en false)") {
    const char* path = "test_pak_bad.pak";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 garbage[3] = {0xDEADBEEFu, 1, 0};
    std::fwrite(garbage, sizeof(u32), 3, f);
    std::fclose(f);

    pak_mount(path);
    CHECK_FALSE(pak_is_packed());

    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver nota arriba)
    std::remove(path);
}
