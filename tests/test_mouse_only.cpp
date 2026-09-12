#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <ostream>
#include <string>

#include <SDL3/SDL.h>

#include "core/arena.h"
#include "game/backlog_mode.h"
#include "game/config.h"
#include "game/map_catalog.h"
#include "game/map_mode.h"
#include "game/menu_mode.h"
#include "game/mode.h"
#include "game/save_load_mode.h"
#include "game/ui.h"
#include "game/vn_mode.h"
#include "render/atlas.h"
#include "platform/input_record.h"
#include "test_config.h"
#include "test_fonts.h"
#include "text/catalog.h"
#include "vm/backlog.h"
#include "vm/rollback.h"
#include "vm/save.h"
#include "vm/script_load.h"
#include "vm/vm.h"

// Criterio 1 de SPEC.md #12 para M15: "una partida completa se juega de principio a fin solo
// con el raton".
//
// La sesion se construye AQUI y no en `sz_bake` (a diferencia de la de teclado, que vive en
// tests/input/session.vnrec) por una razon concreta: los clics tienen que caer sobre
// rectangulos de UI, y esos rectangulos los definen vn_button_rect / menu_slider_rect /
// save_slot_rect. Duplicar sus coordenadas en una herramienta offline significaria que al
// mover un boton la sesion seguiria pinchando donde ya no hay nada, y el test fallaria
// diciendo algo que no tiene que ver con la causa. Llamando a las funciones de verdad, un
// boton que se mueve arrastra la sesion con el.
//
// El riesgo de que un test que fabrica su propia entrada pase sin hacer nada se cubre
// comprobando EFECTOS concretos al final: que el guion llego a su comando End, que la rama
// elegida es la que se pincho (confianza == 6 solo sale por "Intentar ganar mas confianza"),
// que el backlog tiene lineas, que el volumen bajo y que el archivo de guardado existe.
//
// Lo que NO prueba, dicho claro: la traduccion de un evento de raton de SDL a un InputState
// (unas diez lineas de platform/input.cpp) y la conversion de pixeles de ventana a
// coordenadas virtuales, que aqui se da por hecha porque la sesion ya se escribe en
// coordenadas virtuales — igual que hace main.cpp antes de grabar.

namespace {

// Secuencia de entrada en construccion. Se escribe a un .vnrec de verdad y se reproduce
// desde el archivo, para que el camino que recorre sea el mismo que el de una grabacion.
struct MouseScript {
    std::string path;
};

void frame_idle(u32 count) {
    InputState in{};
    for (u32 i = 0; i < count; ++i) {
        input_record_frame(in);
    }
}

// Un clic completo: un frame con el flanco de pulsacion, uno mas con el boton sostenido
// (que es lo que necesita un arrastre de slider) y luego frames en reposo para que el
// cambio de modo se procese.
void frame_click(f32 x, f32 y, u32 idle) {
    InputState down{};
    down.mouse_x            = x;
    down.mouse_y            = y;
    down.mouse_pressed[0]   = true;
    down.mouse_down[0]      = true;
    input_record_frame(down);

    InputState held = down;
    held.mouse_pressed[0]   = false;
    input_record_frame(held);

    InputState up{};
    up.mouse_x = x;
    up.mouse_y = y;
    for (u32 i = 0; i < idle; ++i) {
        input_record_frame(up);
    }
}

void frame_click_rect(const UiRect& r, u32 idle) {
    frame_click(r.x + r.w * 0.5f, r.y + r.h * 0.5f, idle);
}

// Punto libre de UI: por encima del bloque de opciones (que empieza hacia y=398 con tres
// opciones) y muy por encima de la botonera y del cuadro de dialogo. Un clic aqui es
// "avanzar el dialogo" y nada mas.
constexpr f32 k_advance_x = 960.0f;
constexpr f32 k_advance_y = 160.0f;

void frame_advance_dialogue(u32 times) {
    for (u32 i = 0; i < times; ++i) {
        frame_click(k_advance_x, k_advance_y, 6);
    }
}

// Escribe la sesion completa. Solo raton: ni una tecla.
void write_mouse_session(const char* path) {
    REQUIRE(input_record_begin(path));

    frame_idle(4);

    // 1) Caminar hasta el trigger del mapa pinchando en el. El trigger de demo_map.tmx
    // ocupa el tile (3,2) de 64 px, asi que su centro esta en (224,160) del espacio virtual,
    // y el mapa se dibuja 1:1 desde el origen (ver MapMode::render).
    frame_click(224.0f, 160.0f, 60);

    // 2) Tres lineas de dialogo antes del @choice. Dos clics cada una: el primero completa
    // el efecto de maquina de escribir, el segundo confirma (convencion de novela visual).
    frame_advance_dialogue(6);

    // 3) La botonera: historial, menu con arrastre de volumen, guardar y cargar. Va aqui,
    // con un Say en pantalla, porque es cuando la botonera esta visible.
    frame_click_rect(vn_button_rect(VnButton::Backlog), 10);
    frame_click_rect(backlog_close_rect(), 10);

    frame_click_rect(vn_button_rect(VnButton::Menu), 10);
    {
        // Arrastre del slider del bus Master hasta la mitad del carril.
        UiRect track = menu_slider_rect(0);
        frame_click(track.x + track.w * 0.5f, track.y + track.h * 0.5f, 6);
    }
    frame_click_rect(menu_close_rect(), 10);

    frame_click_rect(vn_button_rect(VnButton::Save), 12);
    frame_click_rect(save_slot_rect(0), 12);
    frame_click_rect(save_close_rect(), 10);

    frame_click_rect(vn_button_rect(VnButton::Load), 12);
    frame_click_rect(save_slot_rect(0), 20);

    // 4) Terminar las lineas que queden y elegir la primera opcion del @choice. Sobran
    // clics de avance a proposito: un clic de avance sobre un @choice no hace nada (cae
    // fuera de las opciones), asi que llegar con holgura no rompe la secuencia.
    frame_advance_dialogue(8);
    frame_click_rect(vn_choice_rect(0, 3), 12);

    // 5) La linea del final y el @wait de cierre.
    frame_advance_dialogue(6);
    frame_idle(30);

    input_record_end();
}

// Pila de modos completa, con MapMode en la base como en el juego real (main.cpp): la
// partida empieza caminando por un mapa, asi que un criterio de "partida completa" que
// arrancara en VnMode se saltaria justo el primer tramo.
struct Session {
    GameState      state{};
    CompiledScript script{};
    ModeStack      stack{};
    MapMode        map{};
    VnMode         vn{};
    MenuMode       menu{};
    BacklogMode    backlog_mode{};
    SaveLoadMode   save_load{};
    u32            frames_run    = 0;
    bool           reached_vn    = false;
    bool           script_ended  = false;
};

void run_session(Session* s, Arena* arena, const char* record_path) {
    REQUIRE(input_replay_begin(record_path));

    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);

    REQUIRE(s->map.load("demo_map.vnm", arena));
    s->map.state      = &s->state;
    s->state.map_id   = map_catalog_id_of("demo_map");
    s->state.player_x = 4.0f * 64.0f + 32.0f;
    s->state.player_y = 3.0f * 64.0f + 32.0f;
    for (u32 i = 0; i < 4; ++i) {
        s->state.bus_volume[i] = 1.0f;
    }

    s->vn.font             = g_test_font_latin;
    s->vn.state            = &s->state;
    s->vn.layout_arena     = arena;
    s->menu.font           = g_test_font_latin;
    s->menu.latin_font     = g_test_font_latin;
    s->menu.cjk_font       = g_test_font_cjk;
    s->menu.state          = &s->state;
    s->menu.scratch_arena  = arena;
    s->menu.catalog_arena  = &g_arena_perm;
    s->menu.persist_config = false;  // un test no le pisa el config.ini a nadie
    s->backlog_mode.font          = g_test_font_latin;
    s->backlog_mode.script        = &s->script;
    s->backlog_mode.scratch_arena = arena;
    s->save_load.font          = g_test_font_latin;
    s->save_load.scratch_arena = arena;
    s->save_load.state         = &s->state;
    s->save_load.backlog       = &g_backlog;

    mode_stack_push(&s->stack, &s->map);

    InputState input{};
    while (input_replay_next(&input)) {
        mode_stack_update(&s->stack, input, k_input_record_dt);

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

        // Mismo router que main.cpp para las peticiones de la botonera.
        if (s->stack.count == 2 && s->vn.ui_request != VnUiRequest::None) {
            switch (s->vn.ui_request) {
                case VnUiRequest::Backlog:
                    mode_stack_push(&s->stack, &s->backlog_mode);
                    break;
                case VnUiRequest::Menu:
                    mode_stack_push(&s->stack, &s->menu);
                    break;
                case VnUiRequest::Save:
                    s->save_load.is_save = true;
                    mode_stack_push(&s->stack, &s->save_load);
                    break;
                case VnUiRequest::Load:
                    s->save_load.is_save = false;
                    mode_stack_push(&s->stack, &s->save_load);
                    break;
                case VnUiRequest::RollbackBack:
                    rollback_back(&g_rollback, &s->state);
                    vm_resync_after_state_change(&s->state);
                    break;
                case VnUiRequest::RollbackForward:
                    rollback_forward(&g_rollback, &s->state);
                    vm_resync_after_state_change(&s->state);
                    break;
                case VnUiRequest::None:
                    break;
            }
        }
        s->vn.ui_request = VnUiRequest::None;

        if (s->map.pending_trigger_script != nullptr) {
            REQUIRE(script_load(s->map.pending_trigger_script, &g_arena_scene, &s->script) ==
                    ScriptLoadResult::Ok);
            s->vn.script            = s->script;
            s->vn.finished          = false;
            s->vn.layout_pc         = 0xFFFFFFFFu;
            s->state.vm.pc          = 0;
            s->state.vm.cmd_phase   = 0;
            mode_stack_push(&s->stack, &s->vn);
            s->map.pending_trigger_script = nullptr;
            s->reached_vn                 = true;
        }
        if (s->vn.finished) {
            s->script_ended = true;
        }

        s->frames_run += 1;
    }
}

}  // namespace

TEST_CASE("raton: una partida completa se juega de principio a fin sin tocar una tecla") {
    Config saved_config = g_config;

    std::string path = "mouse_session.vnrec";
    write_mouse_session(path.c_str());

    Arena   arena = arena_create(8 * 1024 * 1024, "mouse_only");
    Session s;
    run_session(&s, &arena, path.c_str());

    MESSAGE("frames: " << s.frames_run << ", pc=" << s.state.vm.pc
                       << ", confianza=" << s.state.vars[1]
                       << ", backlog=" << g_backlog.count
                       << ", master=" << s.state.bus_volume[0]);

    // 1) El clic en el mapa hizo caminar al jugador hasta el trigger y la escena arranco.
    CHECK(s.reached_vn);
    // 2) El guion llego a su final. Sin esto, "partida completa" no significaria nada.
    CHECK(s.script_ended);
    // 3) La rama elegida es la que se pincho. demo_branching pone confianza a 0, le suma 1
    // antes del @choice y 5 mas solo en rama_reintentar, que es la opcion 0: confianza 6
    // solo se puede alcanzar por ahi. Si el clic hubiera caido en otra opcion (o en ninguna,
    // y el @choice se hubiera resuelto por otra via) este numero seria distinto.
    bool found_confianza_6 = false;
    for (u32 i = 0; i < k_max_vars; ++i) {
        if (s.state.vars[i] == 6) {
            found_confianza_6 = true;
        }
    }
    CHECK(found_confianza_6);
    // 4) Los Say se confirmaron de verdad (el backlog solo crece al confirmar una linea).
    CHECK(g_backlog.count > 0);
    // 5) El arrastre del slider bajo el volumen del bus Master.
    CHECK(s.state.bus_volume[0] < 1.0f);
    CHECK(s.state.bus_volume[0] > 0.0f);
    // 6) Guardar escribio el archivo del hueco 0.
    std::FILE* slot_file = std::fopen(save_slot_path(0), "rb");
    CHECK(slot_file != nullptr);
    if (slot_file != nullptr) {
        std::fclose(slot_file);
    }

    arena_destroy(&arena);
    g_config = saved_config;
    catalog_clear();
    // El .vnrec NO se borra a proposito: queda en el directorio de build para poder
    // reproducir la MISMA sesion en el juego real con
    // `sz_runtime --replay-input mouse_session.vnrec`, que es donde se comprueba que jugar con
    // el raton no asigna heap dentro del frame (aqui no hay bucle de frame que medir).
}

TEST_CASE("raton: el letterbox se deshace, y un clic en la barra negra no acierta nada") {
    // Una ventana de 1280x800 (aspecto 1.6) es mas alta que 16:9, asi que el contenido se
    // presenta con barras arriba y abajo: 1280x720 centrado, con 40 px de barra.
    f32 vx = 0.0f, vy = 0.0f;
    render_window_to_virtual(1280, 800, 640.0f, 400.0f, &vx, &vy);
    CHECK(vx == doctest::Approx(960.0f));
    CHECK(vy == doctest::Approx(540.0f));

    // Esquina superior izquierda del CONTENIDO, no de la ventana.
    render_window_to_virtual(1280, 800, 0.0f, 40.0f, &vx, &vy);
    CHECK(vx == doctest::Approx(0.0f));
    CHECK(vy == doctest::Approx(0.0f));

    // Un clic en la barra negra de arriba cae fuera de la pantalla virtual, y por tanto
    // fuera de cualquier rectangulo de UI: no hace falta un "esta dentro" aparte.
    render_window_to_virtual(1280, 800, 640.0f, 5.0f, &vx, &vy);
    CHECK(vy < 0.0f);
    InputState on_bar{};
    on_bar.mouse_x = vx;
    on_bar.mouse_y = vy;
    CHECK_FALSE(ui_hover(vn_button_rect(VnButton::Menu), on_bar));

    // Pillarbox: ventana de 2560x1080 (mas ancha que 16:9), contenido de 1920x1080 centrado
    // con 320 px de barra a cada lado.
    render_window_to_virtual(2560, 1080, 1280.0f, 540.0f, &vx, &vy);
    CHECK(vx == doctest::Approx(960.0f));
    CHECK(vy == doctest::Approx(540.0f));
    render_window_to_virtual(2560, 1080, 100.0f, 540.0f, &vx, &vy);
    CHECK(vx < 0.0f);
}

TEST_CASE("raton: la botonera y las opciones no se solapan con nada que las tape") {
    // Los rectangulos de update() y render() son los mismos por construccion (una sola
    // funcion), pero eso no impide que dos elementos DISTINTOS se pisen: si un boton cayera
    // dentro del cuadro de dialogo, un clic abriria un panel y avanzaria el dialogo a la vez
    // (o al contrario, segun el orden). Esto lo comprueba de verdad en vez de confiarlo.
    UiRect box = vn_dialogue_box_rect();
    for (u32 i = 0; i < k_vn_button_count; ++i) {
        UiRect b = vn_button_rect(static_cast<VnButton>(i));
        INFO("boton " << vn_button_label(static_cast<VnButton>(i)));
        CHECK(b.y + b.h <= box.y);            // por encima del cuadro
        CHECK(b.x >= 0.0f);
        CHECK(b.x + b.w <= static_cast<f32>(k_virtual_width));
        for (u32 j = i + 1; j < k_vn_button_count; ++j) {
            UiRect o = vn_button_rect(static_cast<VnButton>(j));
            bool   overlap =
                b.x < o.x + o.w && o.x < b.x + b.w && b.y < o.y + o.h && o.y < b.y + b.h;
            CHECK_FALSE(overlap);
        }
    }

    // Las opciones de un @choice tampoco se solapan entre si, para cualquier cantidad
    // admitida, y caben en pantalla.
    for (u32 count = 1; count <= k_max_choice_options; ++count) {
        for (u32 i = 0; i < count; ++i) {
            UiRect r = vn_choice_rect(i, count);
            INFO("count=" << count << " i=" << i);
            CHECK(r.y >= 0.0f);
            CHECK(r.y + r.h <= static_cast<f32>(k_virtual_height));
            if (i + 1 < count) {
                UiRect next = vn_choice_rect(i + 1, count);
                CHECK(r.y + r.h <= next.y);
            }
        }
    }
}

TEST_CASE("raton: el arte de UI que la interfaz pide existe de verdad en el atlas") {
    // ui_draw_nine_slice cae a un rectangulo solido cuando el sprite no esta en el atlas, y
    // lo hace EN SILENCIO (a proposito: SPEC.md #4, nunca fatal). Eso significa que "arte de
    // UI real" podria ser falso sin que nada lo dijera. Esto lo comprueba.
    // El runner de tests no carga el registro del atlas (no lo necesita casi ningun test),
    // asi que se carga aqui, igual que hace test_atlas.cpp.
    REQUIRE(atlas_load(&g_arena_perm));
    REQUIRE(atlas_sprite_count() > 0);
    const char* needed[] = {"button_rectangle_depth_flat", "button_rectangle_border",
                            "slide_horizontal_grey"};
    for (const char* name : needed) {
        AtlasSprite r{};
        INFO("sprite de UI: " << std::string(name));
        CHECK(atlas_find(name, &r));
        CHECK(r.w > 0);
        CHECK(r.h > 0);
        // El nine-slice usa esquinas de 16 px, asi que un sprite mas pequeno que 32x32 en
        // cualquier eje se dibujaria sin centro. slide_horizontal_grey es de 96x16 y por eso
        // usa esquinas de 6 (ver ui_draw_slider).
        MESSAGE(std::string(name) << ": " << r.w << "x" << r.h);
    }
}
