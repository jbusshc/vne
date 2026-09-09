#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir un
// std::string_view en un mensaje de fallo de CHECK.
#include <ostream>
#include <string_view>

#include <SDL3/SDL.h>

#include "base/arena.h"
#include "game/map_mode.h"

// assets_baked/demo_map.vnm ya esta horneado por el propio build (CMakeLists.txt: regla
// vne_bake_map), igual que assets_baked/demo.vnc para los tests de VM — mismo patron que
// test_audio.cpp con assets_src/ogg/.

TEST_CASE("MapMode::load: lee el .vnm horneado y expone la rejilla correcta") {
    Arena     a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode   m;
    REQUIRE(m.load("demo_map.vnm", &a));
    CHECK(m.grid_w == 8);
    CHECK(m.grid_h == 6);
    CHECK(m.tile_size == 64);
    arena_destroy(&a);
}

TEST_CASE("MapMode: la colision de borde bloquea, el interior esta libre (demo_map.tmx)") {
    Arena   a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode m;
    REQUIRE(m.load("demo_map.vnm", &a));

    CHECK(m.tile_blocked(0, 0));   // esquina, pared de borde
    CHECK(m.tile_blocked(7, 5));   // esquina opuesta
    CHECK_FALSE(m.tile_blocked(3, 2));  // interior abierto
    CHECK_FALSE(m.tile_blocked(4, 3));  // interior abierto (spawn del jugador)
    CHECK(m.tile_blocked(-1, 0));  // fuera de la rejilla: tratado como pared
    CHECK(m.tile_blocked(100, 100));

    arena_destroy(&a);
}

TEST_CASE("MapMode: el trigger de demo_map.tmx se detecta en su tile y no fuera de el") {
    Arena   a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode m;
    REQUIRE(m.load("demo_map.vnm", &a));

    REQUIRE(m.trigger_count == 1);
    CHECK(m.trigger_at(3, 2) == 0);   // tile del trigger (192/64, 128/64)
    CHECK(m.trigger_at(4, 3) == -1);  // tile de spawn, sin trigger
    CHECK(m.trigger_at(0, 0) == -1);

    arena_destroy(&a);
}

TEST_CASE("MapMode::update: caminar hasta el trigger dispara pending_trigger_script una "
          "sola vez") {
    Arena     a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode   m;
    REQUIRE(m.load("demo_map.vnm", &a));

    GameState state{};
    state.player_x = 3.0f * 64.0f + 32.0f;  // tile (3,3): abierto, junto al trigger
    state.player_y = 3.0f * 64.0f + 32.0f;
    m.state        = &state;

    InputState input{};
    input.key_down[SDL_SCANCODE_W] = true;  // hacia arriba: tile (3,2), el trigger

    // Varios pasos hasta cruzar de tile (3,3) a (3,2): un solo paso de dt grande podria
    // saltarse la comprobacion si el tile no cambia entre frames, asi que se avanza en
    // pasos pequeños como haria el juego real.
    bool fired = false;
    for (u32 i = 0; i < 60 && !fired; ++i) {
        m.update(input, 1.0f / 60.0f);
        if (m.pending_trigger_script != nullptr) {
            fired = true;
        }
    }
    REQUIRE(fired);
    if (!fired || m.pending_trigger_script == nullptr) {
        arena_destroy(&a);
        return;
    }
    CHECK(std::string_view(m.pending_trigger_script) == "demo_branching.vnc");

    // Consumir el trigger (lo que haria main.cpp) y seguir de pie en el mismo tile: no
    // debe volver a dispararse hasta salir y volver a entrar.
    m.pending_trigger_script = nullptr;
    m.update(input, 1.0f / 60.0f);
    CHECK(m.pending_trigger_script == nullptr);

    arena_destroy(&a);
}
