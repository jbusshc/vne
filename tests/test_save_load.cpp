#include <doctest/doctest.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "core/hash.h"
#include "core/crc32.h"
#include "vm/symbols_load.h"
#include "vm/save.h"

TEST_CASE("save_game/load_game: round-trip preserva el estado y el backlog exactos") {
    GameState state{};
    state.bg_id     = 42;
    state.vars[0]   = 7;
    state.vm.pc     = 123;
    state.rng_state = 999;

    Backlog backlog{};
    backlog_push(&backlog, 1, 100, 0xFFFFu, 0xAAAAAAAAu);
    backlog_push(&backlog, 2, 200, 0xFFFFu, 0xBBBBBBBBu);

    const char* path = "test_save.vnsave";
    REQUIRE(save_game(path, state, backlog) == SaveResult::Ok);

    GameState loaded{};
    Backlog   loaded_backlog{};
    REQUIRE(load_game(path, &loaded, &loaded_backlog) == LoadResult::Ok);

    CHECK(std::memcmp(&state, &loaded, sizeof(GameState)) == 0);
    REQUIRE(loaded_backlog.count == 2);

    BacklogEntry ordered[2];
    backlog_get_ordered(loaded_backlog, ordered);
    CHECK(ordered[0].speaker_id == 1);
    CHECK(ordered[0].text_id == 100);
    CHECK(ordered[1].speaker_id == 2);
    CHECK(ordered[1].text_id == 200);

    std::remove(path);
}

TEST_CASE("save_game/load_save_thumbnail: la miniatura QOI hace round-trip exacto (M7)") {
    GameState state{};
    Backlog   backlog{};
    const char* path = "test_save_thumb.vnsave";

    u8 fake_qoi[16];
    for (u32 i = 0; i < sizeof(fake_qoi); ++i) {
        fake_qoi[i] = static_cast<u8>(i * 7);
    }

    REQUIRE(save_game(path, state, backlog, fake_qoi, sizeof(fake_qoi)) == SaveResult::Ok);

    u8  out[64];
    u32 out_size = 0;
    REQUIRE(load_save_thumbnail(path, out, sizeof(out), &out_size) == LoadResult::Ok);
    REQUIRE(out_size == sizeof(fake_qoi));
    CHECK(std::memcmp(out, fake_qoi, sizeof(fake_qoi)) == 0);

    // load_game (la carga completa) debe seguir funcionando exactamente igual con
    // miniatura presente: la salta, no la decodifica.
    GameState loaded{};
    Backlog   loaded_backlog{};
    REQUIRE(load_game(path, &loaded, &loaded_backlog) == LoadResult::Ok);
    CHECK(std::memcmp(&state, &loaded, sizeof(GameState)) == 0);

    std::remove(path);
}

TEST_CASE("load_save_thumbnail: un .vnsave sin miniatura (M4-M6) devuelve tamano 0, no error") {
    GameState state{};
    Backlog   backlog{};
    const char* path = "test_save_nothumb.vnsave";
    REQUIRE(save_game(path, state, backlog) == SaveResult::Ok);

    u8  out[64];
    u32 out_size = 123;
    REQUIRE(load_save_thumbnail(path, out, sizeof(out), &out_size) == LoadResult::Ok);
    CHECK(out_size == 0);

    std::remove(path);
}

TEST_CASE("load_game: migra un .vnsave v1 (M4-M8, sin map_id/player_x/player_y) a v2 "
          "(M9, SPEC.md #8.3)") {
    // Construye a mano un .vnsave v1 (el prefijo de GameState antes de que M9 anadiera
    // map_id/player_x/player_y, ver offsetof(GameState, map_id) en save.cpp): no hay
    // ningun archivo v1 real que se pueda generar ya con este binario, asi que se
    // fabrica el formato exacto que un binario v1 habria escrito.
    GameState state{};
    state.bg_id = 7;
    // En el hueco que le habria tocado a "confianza" con el hash viejo: la cadena de
    // migraciones llega hasta v4 (M14), que recoloca los valores POR NOMBRE, asi que un
    // valor en un indice arbitrario que no corresponde a ninguna variable del proyecto se
    // descarta — y eso es lo correcto, porque era de una variable que nadie usa.
    REQUIRE(symbols_load(&g_arena_perm));
    u16 confianza_id = symbols_id(SymKind::Var, "confianza");
    REQUIRE(confianza_id != 0);
    state.vars[fnv1a_u32("confianza") % k_max_vars] = 99;

    usize v1_size = offsetof(GameState, map_id);
    u32   checksum = crc32(&state, v1_size);

    const char*  path    = "test_save_v1_migration.vnsave";
    std::FILE*   f       = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 magic       = 0x56534E56u;  // 'VNSV', ver save.cpp
    u32 old_version = 1;
    u32 state_size  = static_cast<u32>(v1_size);
    std::fwrite(&magic, sizeof(u32), 1, f);
    std::fwrite(&old_version, sizeof(u32), 1, f);
    std::fwrite(&state_size, sizeof(u32), 1, f);
    std::fwrite(&checksum, sizeof(u32), 1, f);
    std::fwrite(&state, 1, v1_size, f);  // solo el prefijo v1, no la struct v2 completa
    u32 thumbnail_size = 0;
    std::fwrite(&thumbnail_size, sizeof(u32), 1, f);
    u32 backlog_count = 0;
    std::fwrite(&backlog_count, sizeof(u32), 1, f);
    std::fclose(f);

    GameState migrated{};
    Backlog   migrated_backlog{};
    REQUIRE(load_game(path, &migrated, &migrated_backlog) == LoadResult::Ok);

    // La cadena sigue hasta v3 (M13), que limpia bg_id a proposito: su id venia de un
    // interner que ya no existe y conservarlo dibujaria el fondo equivocado (ADR-0062).
    CHECK(migrated.bg_id == 0);
    CHECK(migrated.vars[confianza_id] == 99);  // recolocado a su id nuevo, no perdido
    CHECK(migrated.map_id == 0);  // valor por defecto: "sin mapa activo"
    CHECK(migrated.player_x == doctest::Approx(0.0f));
    CHECK(migrated.player_y == doctest::Approx(0.0f));

    std::remove(path);
}

TEST_CASE("load_game: archivo inexistente devuelve NotFound") {
    GameState state{};
    Backlog   backlog{};
    CHECK(load_game("no_existe.vnsave", &state, &backlog) == LoadResult::NotFound);
}

TEST_CASE("load_game: un archivo con el checksum roto se rechaza") {
    GameState state{};
    Backlog   backlog{};
    const char* path = "test_corrupt.vnsave";
    REQUIRE(save_game(path, state, backlog) == SaveResult::Ok);

    // Corrompe un byte justo al principio del bloque de GameState (offset 16: despues de
    // magic+version+size+checksum, los primeros 4 u32 del header).
    std::FILE* f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    std::fseek(f, 16, SEEK_SET);
    u8 corrupt = 0xFFu;
    std::fwrite(&corrupt, 1, 1, f);
    std::fclose(f);

    GameState loaded{};
    Backlog   loaded_backlog{};
    CHECK(load_game(path, &loaded, &loaded_backlog) == LoadResult::ChecksumMismatch);

    std::remove(path);
}

TEST_CASE("load_game: version desconocida se rechaza en vez de cargar a medias") {
    GameState state{};
    Backlog   backlog{};
    const char* path = "test_badversion.vnsave";
    REQUIRE(save_game(path, state, backlog) == SaveResult::Ok);

    std::FILE* f = std::fopen(path, "r+b");
    REQUIRE(f != nullptr);
    u32 bad_version = 999u;
    std::fseek(f, 4, SEEK_SET);  // offset 4: campo version
    std::fwrite(&bad_version, sizeof(u32), 1, f);
    std::fclose(f);

    GameState loaded{};
    Backlog   loaded_backlog{};
    CHECK(load_game(path, &loaded, &loaded_backlog) == LoadResult::UnsupportedVersion);

    std::remove(path);
}

TEST_CASE("load_game: un .vnsave v2 se migra a v3 limpiando actores y fondo") {
    // v2 -> v3 (M13, ADR-0062). El layout no cambia ni un byte: lo que cambia es el
    // SIGNIFICADO de actor_id/pose_id/bg_id, que pasaron de empezar en 0 a empezar en 1.
    //
    // Conservarlos seria peor que perderlos: el viejo id 1 resolveria ahora al nombre del
    // actor 0 y dibujaria el personaje equivocado sin ningun aviso. Este test fija que se
    // limpian, y que lo que NO depende del interner sobrevive.
    GameState state{};
    state.actors[0].actor_id = 1;
    state.actors[0].pose_id  = 2;
    state.actors[0].alpha    = 1.0f;
    state.actors[3].actor_id = 5;
    state.bg_id              = 4;
    REQUIRE(symbols_load(&g_arena_perm));
    u16 confianza_id = symbols_id(SymKind::Var, "confianza");
    REQUIRE(confianza_id != 0);
    state.vars[fnv1a_u32("confianza") % k_max_vars] = 1234;
    state.vm.pc              = 77;
    state.bgm_track_id       = 900;
    state.player_x           = 640.0f;

    u32 checksum = crc32(&state, sizeof(GameState));

    const char* path = "test_save_v2_migration.vnsave";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 magic       = 0x56534E56u;  // 'VNSV', ver save.cpp
    u32 old_version = 2;
    u32 state_size  = static_cast<u32>(sizeof(GameState));
    std::fwrite(&magic, sizeof(u32), 1, f);
    std::fwrite(&old_version, sizeof(u32), 1, f);
    std::fwrite(&state_size, sizeof(u32), 1, f);
    std::fwrite(&checksum, sizeof(u32), 1, f);
    std::fwrite(&state, sizeof(GameState), 1, f);
    u32 thumbnail_size = 0;
    std::fwrite(&thumbnail_size, sizeof(u32), 1, f);
    u32 backlog_count = 0;
    std::fwrite(&backlog_count, sizeof(u32), 1, f);
    std::fclose(f);

    GameState migrated{};
    Backlog   migrated_backlog{};
    REQUIRE(load_game(path, &migrated, &migrated_backlog) == LoadResult::Ok);

    // Lo que dependia del interner se limpia.
    for (u32 i = 0; i < k_max_actor_slots; ++i) {
        CHECK(migrated.actors[i].actor_id == 0);
        CHECK(migrated.actors[i].pose_id == 0);
    }
    CHECK(migrated.bg_id == 0);

    // Todo lo demas sobrevive: perder la partida entera por esto seria desproporcionado.
    CHECK(migrated.vars[confianza_id] == 1234);
    CHECK(migrated.vm.pc == 77);
    CHECK(migrated.bgm_track_id == 900);
    CHECK(migrated.player_x == doctest::Approx(640.0f));

    std::remove(path);
}

TEST_CASE("load_game: un .vnsave v3 se migra a v4 recolocando variables y banderas") {
    // v3 -> v4 (M14, ADR-0067). A diferencia de v2 -> v3, aqui SI se puede migrar de verdad:
    // los ids viejos eran `fnv1a(nombre) % capacidad`, la tabla de simbolos tiene todos los
    // nombres, asi que para cada uno se puede recalcular donde estaba y copiarlo a donde va.
    REQUIRE(symbols_load(&g_arena_perm));
    u16 id = symbols_id(SymKind::Var, "confianza");
    REQUIRE(id != 0);  // la usa demo_branching.vns

    GameState state{};
    // Se escribe donde lo habria dejado un binario v3: en el hueco del hash viejo.
    u32 old_slot         = fnv1a_u32("confianza") % k_max_vars;
    state.vars[old_slot] = 1234;
    state.vm.pc          = 55;

    u32 checksum = crc32(&state, sizeof(GameState));

    const char* path = "test_save_v3_migration.vnsave";
    std::FILE*  f    = std::fopen(path, "wb");
    REQUIRE(f != nullptr);
    u32 magic       = 0x56534E56u;  // 'VNSV'
    u32 old_version = 3;
    u32 state_size  = static_cast<u32>(sizeof(GameState));
    std::fwrite(&magic, sizeof(u32), 1, f);
    std::fwrite(&old_version, sizeof(u32), 1, f);
    std::fwrite(&state_size, sizeof(u32), 1, f);
    std::fwrite(&checksum, sizeof(u32), 1, f);
    std::fwrite(&state, sizeof(GameState), 1, f);
    u32 zero = 0;
    std::fwrite(&zero, sizeof(u32), 1, f);  // miniatura
    std::fwrite(&zero, sizeof(u32), 1, f);  // backlog
    std::fclose(f);

    GameState migrated{};
    Backlog   migrated_backlog{};
    REQUIRE(load_game(path, &migrated, &migrated_backlog) == LoadResult::Ok);

    // El valor sigue ahi, pero en el hueco que le toca ahora. Esto es lo que v2 -> v3 NO
    // podia hacer con los actores: alli los ids no eran reconstruibles, aqui si.
    CHECK(migrated.vars[id] == 1234);
    CHECK(migrated.vm.pc == 55);

    // Y si el id nuevo es distinto del viejo, el hueco viejo queda limpio: la migracion
    // mueve, no duplica.
    if (id != old_slot) {
        CHECK(migrated.vars[old_slot] == 0);
    }

    std::remove(path);
}
