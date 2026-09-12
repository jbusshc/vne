#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "test_config.h"
#include "vm/backlog.h"
#include "vm/save.h"
#include "vm/state.h"

// Criterio de SPEC.md #12 para M14: "las tres versiones de guardado cargan correctamente
// desde tests/saves/ en un test". Son cuatro versiones historicas ya (v1..v4) mas la actual.
//
// Estos archivos NO se fabrican aqui: son fijos, estan en el repositorio y los escribio
// `vne_bake save-fixtures` byte a byte como lo habria hecho el binario de cada epoca. La
// diferencia importa: un test que se fabrica su propio archivo prueba la migracion contra lo
// que el test CREE que escribia un binario viejo, no contra lo que escribia. Ver
// tests/saves/README.md.

namespace {

std::string fixture_path(const char* name) {
    return std::string(VNE_SOURCE_DIR) + "/tests/saves/" + name;
}

}  // namespace

TEST_CASE("saves historicos: todas las versiones cargan desde tests/saves/ (criterio M14)") {
    for (u32 version = 1; version <= 4; ++version) {
        std::string path = fixture_path(("v" + std::to_string(version) + ".vnsave").c_str());
        CAPTURE(path);

        GameState state{};
        Backlog   backlog{};
        LoadResult result = load_game(path.c_str(), &state, &backlog);
        REQUIRE(result == LoadResult::Ok);

        // Cada fixture lleva valores distintos para poder distinguirlos: si la migracion
        // devolviera un estado por defecto, esto lo pillaria.
        CHECK(state.vm.pc == 40 + version);
        CHECK(state.rng_state == 0xC0FFEEu);
        CHECK(state.bgm_track_id == 77);

        // El backlog sobrevive a todas, incluida la v4 -> v5 que ENSANCHA cada entrada de
        // 12 a 16 bytes (M14, key_hash).
        CHECK(backlog.count == 2);

        // Lo que cada version no tenia, queda en su valor por defecto y no en basura.
        if (version == 1) {
            CHECK(state.map_id == 0);  // v1 es anterior a los mapas (M9)
        } else {
            CHECK(state.map_id == 5);
        }

        // Los ids de actor de v1/v2/v3 venian de un interner que ya no existe: se limpian a
        // proposito (ADR-0062), porque conservarlos dibujaria el personaje equivocado.
        if (version <= 3) {
            CHECK(state.actors[0].actor_id == 0);
            CHECK(state.bg_id == 0);
        }
    }
}

TEST_CASE("saves historicos: un .vnsave de una version FUTURA se rechaza") {
    // Lo unico que no se puede cargar es una partida escrita por una version del juego mas
    // nueva: ahi no hay migracion posible porque no se sabe que trae.
    GameState state{};
    Backlog   backlog{};

    const char* path = "test_save_futuro.vnsave";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 header[4] = {0x56534E56u, k_savegame_version + 1, sizeof(GameState), 0};
    std::fwrite(header, sizeof(u32), 4, f);
    std::fclose(f);

    CHECK(load_game(path, &state, &backlog) == LoadResult::UnsupportedVersion);
    std::remove(path);
}
