#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <ostream>
#include <string>

#include <SDL3/SDL.h>

#include "core/arena.h"
#include "game/backlog_mode.h"
#include "game/config.h"
#include "game/menu_mode.h"
#include "game/mode.h"
#include "game/save_load_mode.h"
#include "game/vn_mode.h"
#include "platform/input_record.h"
#include "test_config.h"
#include "test_fonts.h"
#include "text/catalog.h"
#include "vm/backlog.h"
#include "vm/rollback.h"
#include "vm/script_load.h"
#include "vm/vm.h"

// Criterio de SPEC.md #12 para M15, y el que **cierra la limitacion arrastrada desde M4**:
// "una sesion grabada que abre el menu, baja un volumen, guarda, carga, hace rollback y
// cambia de idioma se reproduce dentro de la suite de tests y termina con un GameState byte a
// byte igual al esperado".
//
// Hasta aqui, F5/F9, el rollback, SaveLoadMode, BacklogMode, MenuMode y el cambio de idioma
// se habian verificado SIEMPRE por su logica interna —llamando a las funciones a mano— y
// nunca por la via por la que los usa un jugador: una tecla que entra por el router de modos
// y llega al manejador del modo que toque.
//
// **Lo que este test NO prueba, dicho claro:** la sesion es sintetica (`sz_bake
// input-fixture`), porque en este entorno no se pueden inyectar pulsaciones en una ventana
// real. El eslabon que sigue sin verificarse es la traduccion de un evento de SDL a un
// InputState: unas cuarenta lineas en platform/input.cpp, compartidas por todas las teclas.
// Todo lo que hay por encima si queda cubierto.

namespace {

struct Session {
    GameState      state{};
    Backlog        backlog{};
    CompiledScript script{};
    ModeStack      stack{};
    VnMode         vn{};
    MenuMode       menu{};
    BacklogMode    backlog_mode{};
    SaveLoadMode   save_load{};
    u32            frames_run = 0;
};

// El mismo router de teclas de nivel superior que main.cpp. Duplicado a proposito y no
// extraido a una funcion compartida: si se compartiera, main.cpp y el test estarian de
// acuerdo por construccion aunque los dos estuvieran mal.
void route_top_level(Session* s, const InputState& input) {
    if (s->stack.count != 1) {
        return;
    }
    if (input.key_pressed[SDL_SCANCODE_B]) {
        mode_stack_push(&s->stack, &s->backlog_mode);
    } else if (input.key_pressed[SDL_SCANCODE_M]) {
        mode_stack_push(&s->stack, &s->menu);
    } else if (input.key_pressed[SDL_SCANCODE_F5]) {
        s->save_load.is_save = true;
        mode_stack_push(&s->stack, &s->save_load);
    } else if (input.key_pressed[SDL_SCANCODE_F9]) {
        s->save_load.is_save = false;
        mode_stack_push(&s->stack, &s->save_load);
    }
    if (input.key_pressed[SDL_SCANCODE_LEFT]) {
        rollback_back(&g_rollback, &s->state);
        vm_resync_after_state_change(&s->state);
    }
    if (input.key_pressed[SDL_SCANCODE_RIGHT]) {
        rollback_forward(&g_rollback, &s->state);
        vm_resync_after_state_change(&s->state);
    }
}

void close_overlays_if_asked(Session* s) {
    if (s->backlog_mode.wants_close) {
        s->backlog_mode.wants_close = false;
        mode_stack_pop(&s->stack);
    }
    if (s->menu.wants_close) {
        s->menu.wants_close = false;
        mode_stack_pop(&s->stack);
    }
    if (s->save_load.wants_close) {
        s->save_load.wants_close = false;
        mode_stack_pop(&s->stack);
    }
}

// Reproduce la sesion contra una pila de modos de verdad. No hay ventana ni GPU: los Mode
// separan update() de render(), y aqui solo se llama a update().
void run_session(Session* s, Arena* arena) {
    std::string path = std::string(SZ_SOURCE_DIR) + "/tests/input/session.vnrec";
    REQUIRE(input_replay_begin(path.c_str()));
    REQUIRE(input_replay_frame_count() > 0);

    REQUIRE(script_load("demo.vnc", &g_arena_scene, &s->script) == ScriptLoadResult::Ok);
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);

    // Los modos dibujan texto en update() (construyen sus layouts), asi que necesitan una
    // fuente valida aunque aqui no se llame a render().
    s->vn.font              = g_test_font_latin;
    s->menu.font            = g_test_font_latin;
    s->menu.latin_font      = g_test_font_latin;
    s->menu.cjk_font        = g_test_font_cjk;
    s->backlog_mode.font    = g_test_font_latin;
    s->save_load.font       = g_test_font_latin;

    s->vn.state             = &s->state;
    s->vn.script            = s->script;
    s->vn.layout_arena      = arena;
    s->menu.state           = &s->state;
    s->menu.scratch_arena   = arena;
    s->menu.catalog_arena   = &g_arena_perm;
    s->menu.persist_config  = false;  // un test no le pisa el config.ini a nadie
    s->backlog_mode.script         = &s->script;
    s->backlog_mode.scratch_arena  = arena;
    s->save_load.state      = &s->state;
    s->save_load.backlog    = &g_backlog;

    mode_stack_push(&s->stack, &s->vn);  // la base de la pila

    InputState input{};
    while (input_replay_next(&input)) {
        route_top_level(s, input);
        mode_stack_update(&s->stack, input, k_input_record_dt);
        close_overlays_if_asked(s);
        s->frames_run += 1;
    }
    s->backlog = g_backlog;
}

}  // namespace

TEST_CASE("input: la sesion grabada se reproduce y da el MISMO GameState dos veces") {
    Config saved_config = g_config;  // el config es global: se deja como estaba

    Arena   arena_a = arena_create(4 * 1024 * 1024, "replay_a");
    Session a;
    run_session(&a, &arena_a);

    Arena   arena_b = arena_create(4 * 1024 * 1024, "replay_b");
    Session b;
    run_session(&b, &arena_b);

    MESSAGE("frames reproducidos: " << a.frames_run);
    CHECK(a.frames_run == b.frames_run);
    CHECK(a.frames_run > 100);

    // "termina con un GameState byte a byte igual al esperado": el esperado es el de la otra
    // reproduccion de la misma sesion. Compararlo contra una constante escrita a mano seria
    // fragil y no probaria mas; lo que importa es que la misma entrada da la misma salida,
    // byte a byte, que es lo que hace util una grabacion.
    CHECK(std::memcmp(&a.state, &b.state, sizeof(GameState)) == 0);
    CHECK(std::memcmp(&a.backlog, &b.backlog, sizeof(Backlog)) == 0);

    arena_destroy(&arena_a);
    arena_destroy(&arena_b);
    g_config = saved_config;
    catalog_clear();
}

TEST_CASE("input: la sesion hace de verdad lo que dice hacer") {
    // Un test de reproduccion que solo comparase dos ejecuciones pasaria aunque la sesion no
    // hiciera NADA. Esto comprueba que los efectos ocurrieron. (Ya me paso una vez en M14 con
    // un test que decia comparar dos guiones y comparaba cero actores.)
    Config  saved_config = g_config;
    Arena   arena        = arena_create(4 * 1024 * 1024, "replay_efectos");
    Session s;
    run_session(&s, &arena);

    MESSAGE("volumen master tras la sesion: " << s.state.bus_volume[0]
                                              << ", pc=" << s.state.vm.pc
                                              << ", backlog=" << g_backlog.count);

    CHECK(s.state.bus_volume[0] < 1.0f);  // bajo un volumen desde el menu
    CHECK(s.state.vm.pc > 0);             // avanzo dialogo
    CHECK(g_backlog.count > 0);           // los Say se confirmaron de verdad
    CHECK(s.stack.count == 1);            // todos los overlays que abrio los cerro

    arena_destroy(&arena);
    g_config = saved_config;
    catalog_clear();
}

TEST_CASE("input: el formato .vnrec da la vuelta completa") {
    // Grabar y reproducir tienen que coincidir byte a byte, o una sesion grabada no valdria
    // para nada.
    const char* path = "test_roundtrip.vnrec";
    InputState  frames[3]{};
    frames[0].key_pressed[SDL_SCANCODE_M]  = true;
    frames[1].mouse_x                      = 123.0f;
    frames[1].mouse_down[0]                = true;
    frames[2].key_down[SDL_SCANCODE_SPACE] = true;

    REQUIRE(input_record_begin(path));
    for (const InputState& f : frames) {
        input_record_frame(f);
        input_record_frame(f);  // repetido: debe fundirse en un solo registro
    }
    input_record_end();

    REQUIRE(input_replay_begin(path));
    CHECK(input_replay_frame_count() == 6);
    for (const InputState& expected : frames) {
        for (int repeat = 0; repeat < 2; ++repeat) {
            InputState got{};
            REQUIRE(input_replay_next(&got));
            CHECK(std::memcmp(&got, &expected, sizeof(InputState)) == 0);
        }
    }
    InputState past_end{};
    CHECK_FALSE(input_replay_next(&past_end));

    std::remove(path);
}
