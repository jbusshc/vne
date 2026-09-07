#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "vm/save.h"

TEST_CASE("save_game/load_game: round-trip preserva el estado y el backlog exactos") {
    GameState state{};
    state.bg_id     = 42;
    state.vars[0]   = 7;
    state.vm.pc     = 123;
    state.rng_state = 999;

    Backlog backlog{};
    backlog_push(&backlog, 1, 100, 0xFFFFu);
    backlog_push(&backlog, 2, 200, 0xFFFFu);

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
