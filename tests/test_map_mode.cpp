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

TEST_CASE("MapMode::box_blocked: la caja del jugador choca antes que su centro (M12)") {
    Arena   a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode m;
    REQUIRE(m.load("demo_map.vnm", &a));

    // demo_map: pared en el borde, interior abierto. El tile (1,1) esta abierto y el
    // (0,1) es pared. Con tile_size 64 y media extension 0.3*64 = 19.2 px:
    //
    //   x = 84  ->  caja [64.8, 103.2]  entera dentro del tile 1: libre
    //   x = 70  ->  caja [50.8,  89.2]  asoma al tile 0 (pared):  bloqueada
    //
    // Y el centro (x=70) sigue estando en el tile 1, que esta abierto: esa es exactamente
    // la diferencia entre AABB y punto, y es lo que este test fija.
    const f32 y_open = 1.0f * 64.0f + 32.0f;

    CHECK_FALSE(m.box_blocked(84.0f, y_open));
    CHECK(m.box_blocked(70.0f, y_open));
    CHECK_FALSE(m.tile_blocked(1, 1));  // el centro de x=70 cae aqui, y esta abierto

    // Lo mismo en vertical contra la pared de arriba.
    const f32 x_open = 3.0f * 64.0f + 32.0f;
    CHECK_FALSE(m.box_blocked(x_open, 84.0f));
    CHECK(m.box_blocked(x_open, 70.0f));

    arena_destroy(&a);
}

TEST_CASE("MapMode::update: el jugador no se mete dentro de la pared al empujarla (M12)") {
    Arena   a = arena_create(1 * 1024 * 1024, "test_map");
    MapMode m;
    REQUIRE(m.load("demo_map.vnm", &a));

    GameState state{};
    state.player_x = 3.0f * 64.0f + 32.0f;
    state.player_y = 3.0f * 64.0f + 32.0f;
    m.state        = &state;

    // Empujar contra la pared de la izquierda durante dos segundos enteros.
    InputState input{};
    input.key_down[SDL_SCANCODE_A] = true;
    for (u32 i = 0; i < 120; ++i) {
        m.update(input, 1.0f / 60.0f);
    }

    // La pared ocupa el tile 0, o sea x < 64. El borde izquierdo de la caja es
    // player_x - 19.2 y no puede haber entrado: antes de M12 el centro llegaba hasta
    // x ~= 64 y el cuerpo se hundia 19.2 px dentro de la pared.
    const f32 half = 64.0f * k_player_half_extent_tiles;
    CHECK(state.player_x - half >= 64.0f - 0.5f);
    MESSAGE("borde izquierdo del jugador tras empujar: " << (state.player_x - half));

    // Y no se ha quedado clavado: sigue pudiendo moverse en el otro eje.
    f32        y_before = state.player_y;
    InputState down{};
    down.key_down[SDL_SCANCODE_S] = true;
    m.update(down, 1.0f / 60.0f);
    CHECK(state.player_y > y_before);

    arena_destroy(&a);
}
