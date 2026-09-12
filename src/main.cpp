#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "assets/assets.h"
#include "assets/hot_reload.h"
#include "vfs/pak.h"
#include "audio/audio.h"
#include "core/arena.h"
#include "core/heap_guard.h"
#include "core/log.h"
#if defined(SZ_EDITOR)
#include "editor/editor.h"
#endif
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
#include "render/render.h"
#include "render/texture.h"
#include "platform/clock.h"
#include "platform/input.h"
#include "platform/input_record.h"
#include "platform/window.h"
#include "text/font.h"
#include "text/glyph_cache.h"
#include "text/layout.h"
#include "lua/lua_bindings.h"
#include "vm/backlog.h"
#include "vm/rollback.h"
#include "vm/save.h"
#include "vm/script_load.h"
#include "vm/symbols_load.h"
#include "vm/vm.h"

// M1: renderizado 2D. Ademas del bucle base de M0, dibuja un stress test de 5000 sprites
// de un unico atlas para verificar el criterio de aceptacion de M1 (SPEC.md #12): deben
// resolverse en una sola draw call a mas de 300 fps.
//
// M2: texto. Ademas, monta un cuadro de dialogo de prueba con marcado inline y furigana,
// avanzando con el efecto de maquina de escribir (visible_glyphs), para demostrar que
// text_layout no se vuelve a llamar por frame (SPEC.md #12).
//
// M3: VM y DSL. Carga assets_baked/demo.vnc (compilado por sz_bake desde
// assets_src/scripts/demo.vns) y lo avanza con vm_update() cada frame. Con
// --autoplay-script <ruta> corre un guion entero via vm_skip_current() sin abrir ventana,
// a maxima velocidad (skill vne-build-verify), y sale con codigo 0/1.


constexpr usize k_perm_arena_size  = 64ull * 1024 * 1024;
constexpr usize k_scene_arena_size = 256ull * 1024 * 1024;
constexpr usize k_frame_arena_size = 8ull * 1024 * 1024;

constexpr u32 k_frame_history_len   = 240;
constexpr f64 k_report_interval_s   = 2.0;


static f32 frame_history_p99_ms(const f32* history, u32 count) {
    f32 sorted[k_frame_history_len];
    for (u32 i = 0; i < count; ++i) {
        sorted[i] = history[i];
    }
    for (u32 i = 1; i < count; ++i) {
        f32 key = sorted[i];
        i32 j   = static_cast<i32>(i) - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j -= 1;
        }
        sorted[j + 1] = key;
    }
    if (count == 0) {
        return 0.0f;
    }
    u32 p99_index = (count * 99) / 100;
    if (p99_index >= count) {
        p99_index = count - 1;
    }
    return sorted[p99_index] * 1000.0f;
}

// El lector del manifiesto del atlas vivia aqui suelto desde M1 ("no hay todavia un modulo
// assets/ formal: esto es una lectura minima, solo para el stress test"). M13 lo saca a
// render/atlas.{h,cpp} porque ahora tiene consumidores de verdad —validacion de @show/@bg al
// compilar y dibujado de fondos y actores— y necesita busqueda por nombre, no solo por
// indice.

// Ship monta game.pak (SPEC.md #11: "paquete .pak ... para builds de release"); Debug/Dev
// montan el directorio suelto, que ademas es lo que hace posible el hot reload de M8/M11
// (leer un archivo recien modificado sin volver a hornear el .pak entero). Un unico sitio
// para esta decision: main() y run_autoplay() la comparten.
static void mount_assets_backend() {
#if defined(SZ_SHIPPING)
    pak_mount("game.pak");
#else
    pak_mount(".");  // assets_baked/ y assets_src/ttf|ogg del propio build dir
#endif
}

// Corre un guion entero sin ventana ni GPU, a maxima velocidad, via vm_skip_current()
// (skill vne-build-verify: "--autoplay-script ... a maxima velocidad ... sale con codigo
// 0 o distinto de 0"). No es una demostracion visual: es la base de la verificacion
// automatizada de guiones completos.
static int run_autoplay(const char* script_path) {
    g_arena_perm  = arena_create(k_perm_arena_size, "perm");
    g_arena_scene = arena_create(k_scene_arena_size, "scene");
    g_arena_frame = arena_create(k_frame_arena_size, "frame");
    mount_assets_backend();
    // La tabla de simbolos del proyecto hace falta AQUI tambien, y faltaba: sin ella
    // `vn.get_var/set_var` no puede resolver un nombre a un id y todo `@lua` de un guion
    // reportaba "la variable no existe". --autoplay-script seguia saliendo con 0, asi que la
    // verificacion de guiones daba por bueno un Lua que no funcionaba. Encontrado en P0.
    symbols_load(&g_arena_perm);
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    lua_init();
    audio_init();

    CompiledScript script{};
    if (script_load(script_path, &g_arena_scene, &script) != ScriptLoadResult::Ok) {
        log_error("--autoplay-script: no se pudo cargar '%s'", script_path);
        return 1;
    }

    GameState state{};
    bool      finished = false;
    u32       steps    = 0;
    constexpr u32 k_max_autoplay_steps = 1000000;
    while (!finished && steps < k_max_autoplay_steps) {
        CmdKind kind = script.cmds[state.vm.pc].kind;
        vm_skip_current(&state.vm, &state, script);
        steps += 1;
        if (kind == CmdKind::End) {
            finished = true;
        }
    }

    if (!finished) {
        log_error("--autoplay-script: '%s' no termino tras %u pasos (posible bucle)",
                  script_path, k_max_autoplay_steps);
        return 1;
    }
    log_info("--autoplay-script: '%s' completo en %u comandos", script_path, steps);
    return 0;
}

int main(int argc, char** argv) {
    // --script <nombre logico> elige que guion carga la ventana (por defecto demo.vnc).
    // Anadido en M12 para poder observar demo_transitions.vnc sin recompilar: el criterio
    // de las transiciones es visual y no se puede llegar a el con demo.vnc, cuyos Say
    // bloquean esperando un input que en este entorno no se puede inyectar.
    const char* script_name     = "demo.vnc";
    bool        script_override = false;
    // M15: --record-input graba las pulsaciones a un .vnrec y --replay-input las vuelve a
    // meter como si alguien las estuviera tecleando. Es lo que por fin permite verificar
    // F5/F9, el rollback, los modos y el cambio de idioma con teclas de verdad y no solo por
    // su logica interna (la limitacion que se venia declarando desde M4).
    const char* record_path = nullptr;
    const char* replay_path = nullptr;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--autoplay-script") == 0) {
            return run_autoplay(argv[i + 1]);
        }
        if (std::strcmp(argv[i], "--script") == 0) {
            script_name     = argv[i + 1];
            script_override = true;
        }
        if (std::strcmp(argv[i], "--record-input") == 0) {
            record_path = argv[i + 1];
        }
        if (std::strcmp(argv[i], "--replay-input") == 0) {
            replay_path = argv[i + 1];
        }
    }

    g_arena_perm  = arena_create(k_perm_arena_size, "perm");
    g_arena_scene = arena_create(k_scene_arena_size, "scene");
    g_arena_frame = arena_create(k_frame_arena_size, "frame");
    mount_assets_backend();
    // Antes que nada lo que dependa de una preferencia: el idioma y los volumenes salen de
    // aqui, no de valores por defecto (M14).
    // Una reproduccion NO lee las preferencias del jugador (M15): si las leyera, la misma
    // sesion daria resultados distintos segun el config.ini que hubiera en el disco, y una
    // grabacion existe precisamente para ser reproducible. De paso evita que reproducir una
    // sesion le pise la configuracion a quien este jugando.
    if (replay_path == nullptr) {
        config_load();
    }
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    lua_init();
    audio_init();

    PlatformWindow window{};
    if (!platform_window_create(&window, "vne \xe2\x80\x94 M2", 1280, 720)) {
        return 1;
    }

    if (!render_init(&window)) {
        log_error("render_init fallo");
        return 1;
    }
    // Despues de render_init (su primera pagina de atlas usa texture_create_dynamic, que
    // necesita el sistema de texturas ya en pie) y desde aqui, no desde render_init: gfx no
    // depende de text en ninguna otra parte y meterle este include invertiria las capas
    // (text/ ya depende de render/). Es la misma capa que ya llama a
    // glyph_cache_begin_frame() cada frame.
    glyph_cache_init();
    // Despues del sistema de texturas (assets_texture reserva el handle placeholder) y con
    // el backend ya montado: arranca el hilo de IO (SPEC.md #7.4).
    assets_init();

#if defined(SZ_EDITOR)
    editor_init();
    g_editor_render_hook = editor_render;
#endif

    // assets_texture y no texture_load: devuelve ya un handle dibujable (placeholder
    // magenta) y el hilo de IO trae el atlas de verdad por detras, que aparece solo en
    // cuanto assets_process_completed_loads() lo integre, sobre este mismo handle
    // (SPEC.md #7.4, criterio de M11). Los primeros frames dibujan magenta a proposito.
    TextureHandle atlas = assets_texture("atlas_00.qoi");

    // El registro de sprites del atlas (M13, render/atlas.h). Un fallo no es fatal: el juego
    // sigue con el placeholder magenta, igual que antes.
    atlas_load(&g_arena_perm);
    // De donde saca la UI su arte (M15). Una sola vez: los modos no reciben la textura, la
    // piden a game/ui.h, que es tambien quien decide que sprite usa cada elemento.
    ui_set_atlas(atlas);

    FontHandle demo_font = text_load_font("ttf/NotoSansJP-subset.ttf", 28);
    if (!demo_font.valid()) {
        log_error("No se pudo cargar la fuente de prueba NotoSansJP-subset.ttf");
    }
    // Fuente latina aparte para el dialogo en español (M10, "fuentes CJK bajo demanda":
    // la fuente CJK completa solo se necesita de verdad cuando el idioma activo la usa;
    // la rasterizacion de glifos bajo demanda en si ya existe desde M2 en glyph_cache).
    FontHandle latin_dialogue_font = text_load_font("ttf/NotoSans-subset.ttf", 28);
    if (!latin_dialogue_font.valid()) {
        log_error("No se pudo cargar la fuente de prueba NotoSans-subset.ttf");
    }
    // Variantes en negrita para {b} (M12): la misma cara marcada para engordar el contorno
    // al rasterizar, porque no hay ningun TTF en negrita entre los assets. Una por idioma,
    // igual que las normales.
    FontHandle latin_bold_font = text_load_font("ttf/NotoSans-subset.ttf", 28, /*bold=*/true);
    FontHandle cjk_bold_font   = text_load_font("ttf/NotoSansJP-subset.ttf", 28, /*bold=*/true);

    // Recarga en caliente (SPEC.md #7.4, M11): vigila los .ttf de estas dos fuentes y los
    // .png del atlas. Los .vns los vigila el editor aparte, porque recargarlos toca la VM
    // (ver el comentario de reparto en assets/hot_reload.h). Vacio en Ship.
    hot_reload_init();
    hot_reload_watch_font(demo_font, "ttf/NotoSansJP-subset.ttf", 28);
    hot_reload_watch_font(latin_dialogue_font, "ttf/NotoSans-subset.ttf", 28);

    GameState demo_state{};
    // Posicion inicial dentro de assets_src/maps/demo_map.tmx (M9): el centro de la sala
    // abierta, no (0,0) (que cae en la pared del borde y dejaria al jugador atascado si
    // esta es una partida nueva, no una cargada).
    //
    // M13: map_id sale del catalogo, no de un 1 puesto a mano. Antes guardar y cargar
    // funcionaba solo porque siempre se cargaba el mismo mapa pasara lo que pasara.
    // Tabla de simbolos del proyecto (M14, ADR-0067): de ella salen los nombres de actor,
    // pose y fondo que necesita el dibujado, y los ids de variable y bandera que usa Lua.
    symbols_load(&g_arena_perm);
    map_catalog_init();
    demo_state.map_id   = map_catalog_id_of("demo_map");
    demo_state.player_x = 4.0f * 64.0f + 32.0f;
    demo_state.player_y = 3.0f * 64.0f + 32.0f;

    CompiledScript demo_script{};
    if (script_load(script_name, &g_arena_scene, &demo_script) != ScriptLoadResult::Ok) {
        log_error("No se pudo cargar '%s'; ejecuta sz_bake primero.", script_name);
    }

    // Pila de modos (SPEC.md #10, M7): VnMode dirige la VM y el cuadro de dialogo real
    // (cierra ADR-0023); Backlog/Menu/SaveLoad se apilan encima sin destruirlo.
    VnMode vn_mode{};
    vn_mode.state        = &demo_state;
    vn_mode.script        = demo_script;
    vn_mode.font          = latin_dialogue_font;  // idioma base: espanol (M10)
    vn_mode.bold_font     = latin_bold_font;      // M12: {b}
    vn_mode.atlas_tex     = atlas;                // M13: fondos y actores
    vn_mode.layout_arena = &g_arena_scene;

    BacklogMode backlog_mode{};
    backlog_mode.script        = &vn_mode.script;
    backlog_mode.font          = demo_font;
    backlog_mode.scratch_arena = &g_arena_scene;

    MenuMode menu_mode{};
    menu_mode.state              = &demo_state;
    menu_mode.font                = demo_font;
    menu_mode.scratch_arena      = &g_arena_scene;
    menu_mode.dialogue_font_slot = &vn_mode.font;  // M10: cambia el idioma de VnMode
    menu_mode.latin_font          = latin_dialogue_font;
    menu_mode.cjk_font            = demo_font;
    menu_mode.bold_font_slot      = &vn_mode.bold_font;  // M12: {b} sigue al idioma
    menu_mode.latin_bold_font     = latin_bold_font;
    menu_mode.cjk_bold_font       = cjk_bold_font;

    // Preferencias guardadas (M14): el idioma elegido en una sesion anterior se aplica ahora,
    // que es el criterio de SPEC.md #12 ("cambiar el idioma, cerrar el proceso y volver a
    // abrirlo mantiene el idioma elegido"). Va DESPUES de cablear las fuentes: apply_locale
    // las reasigna segun el idioma.
    // Grabacion/reproduccion de input (M15). Va justo antes del bucle: todo lo que se
    // inicializa arriba ya esta en pie, asi que la sesion empieza con el juego en un estado
    // conocido.
    menu_mode.persist_config = (replay_path == nullptr);
    if (record_path != nullptr) {
        input_record_begin(record_path);
    } else if (replay_path != nullptr) {
        input_replay_begin(replay_path);
    }

    menu_mode.catalog_arena = &g_arena_perm;  // el catalogo sobrevive a un cambio de escena
    menu_mode.locale_index  = static_cast<i32>(g_config.locale_index);
    menu_mode.apply_locale();
    for (u32 i = 0; i < 4; ++i) {
        demo_state.bus_volume[i] = g_config.bus_volume[i];
        audio_set_bus_volume(static_cast<Bus>(i), g_config.bus_volume[i]);
    }

    SaveLoadMode save_load_mode{};
    save_load_mode.state         = &demo_state;
    save_load_mode.backlog       = &g_backlog;
    save_load_mode.font          = demo_font;
    save_load_mode.scratch_arena = &g_arena_scene;

    // MapMode (M9, SPEC.md #10): la base de la pila ahora es el mapa, no la escena de VN
    // directamente — "caminar por un mapa, pisar un trigger, jugar una escena de VN,
    // volver al mapa" es el criterio de aceptacion. WASD mueve al jugador (las flechas
    // ya las usa el rollback de M4 en la base de la pila).
    MapMode map_mode{};
    map_mode.state = &demo_state;
    // El mapa que se carga sale de GameState.map_id via el catalogo (M13), asi que una
    // partida cargada abre el mapa que tuviera guardado y no uno fijo.
    const char* map_name = map_catalog_resolve(demo_state.map_id);
    bool        have_map = map_name != nullptr && map_mode.load(map_name, &g_arena_scene);
    if (!have_map) {
        log_error("No se pudo cargar el mapa con map_id=%u (%u mapas en el catalogo); "
                  "ejecuta sz_bake map primero.",
                  demo_state.map_id, map_catalog_count());
    }

    // Con --script explicito se arranca directamente en VnMode: quien pide un guion
    // concreto quiere ver ESE guion, no el mapa. Sin el, la base sigue siendo MapMode
    // ("caminar por un mapa" es el criterio de M9) y VnMode se apila al pisar un trigger.
    ModeStack mode_stack{};
    if (have_map && !script_override) {
        mode_stack_push(&mode_stack, &map_mode);
    } else {
        mode_stack_push(&mode_stack, &vn_mode);
    }

    InputState input{};  // tal y como lo entrega la plataforma: raton en pixeles de ventana
    // Lo que ve la logica de juego: el mismo estado con el raton ya convertido a
    // coordenadas virtuales 1920x1080 (M15, ADR-0070). Son dos valores y no uno porque el
    // editor necesita el primero (ImGui dibuja en pixeles de ventana) y los Mode el
    // segundo (ningun modo conoce el tamano de la ventana, skill vne-rendering).
    InputState ui_input{};
    i32        window_w = 0, window_h = 0;
    Clock      clock    = clock_create();

    f32* frame_times      = arena_alloc_n<f32>(&g_arena_perm, k_frame_history_len);
    u32  frame_index      = 0;
    u32  frames_recorded  = 0;
    f64  report_timer     = 0.0;
    u64  max_frame_allocs = 0;

    while (!input.quit_requested) {
        arena_reset(&g_arena_frame);
        heap_guard_reset_frame();
        render_begin_frame();
        glyph_cache_begin_frame();
        // Unico punto donde un asset que trajo el hilo de IO entra en el juego (SPEC.md
        // #7.4). Acotado por dentro para no reventar el presupuesto del frame.
        assets_process_completed_loads();

        // En reproduccion el input NO se lee del sistema: sale de la sesion grabada. Cuando
        // se acaba, el juego se cierra solo — asi una sesion reproducida termina de forma
        // determinista en vez de quedarse en una ventana esperando a nadie.
        platform_window_size_px(&window, &window_w, &window_h);
        if (input_replay_active()) {
            // Una grabacion guarda ya el estado CONVERTIDO, asi que reproducirla no vuelve
            // a convertir nada: por eso una sesion se reproduce igual en una ventana de
            // otro tamano, y dentro de la suite de tests, donde no hay ventana ninguna.
            if (!input_replay_next(&ui_input)) {
                break;
            }
            input = ui_input;
        } else {
            platform_poll_events(&input);
            ui_input = input;
            render_window_to_virtual(window_w, window_h, input.mouse_x, input.mouse_y,
                                   &ui_input.mouse_x, &ui_input.mouse_y);
        }
        input_record_frame(ui_input);

#if defined(SZ_EDITOR)
        if (input.key_pressed[SDL_SCANCODE_F1]) {
            editor_toggle();
        }
#endif

        // Router de modos de nivel superior (SPEC.md #10, M7): B/M/F5/F9 abren un
        // overlay sobre VnMode solo cuando no hay ya uno abierto; ESC en la base cierra
        // el juego, ESC dentro de un overlay lo cierra a el (cada Mode marca su propio
        // wants_close, comprobado despues de mode_stack_update mas abajo).
        if (mode_stack.count == 1) {
            if (input.key_pressed[SDL_SCANCODE_ESCAPE]) {
                input.quit_requested = true;
            }
            if (input.key_pressed[SDL_SCANCODE_B]) {
                mode_stack_push(&mode_stack, &backlog_mode);
            } else if (input.key_pressed[SDL_SCANCODE_M]) {
                mode_stack_push(&mode_stack, &menu_mode);
            } else if (input.key_pressed[SDL_SCANCODE_F5]) {
                save_load_mode.is_save = true;
                mode_stack_push(&mode_stack, &save_load_mode);
            } else if (input.key_pressed[SDL_SCANCODE_F9]) {
                save_load_mode.is_save = false;
                mode_stack_push(&mode_stack, &save_load_mode);
            }
        }
        // Izquierda/derecha para rollback (SPEC.md #12) solo en la base: dentro de un
        // overlay esas mismas teclas navegan su propia UI (M7).
        if (mode_stack.count == 1 && input.key_pressed[SDL_SCANCODE_LEFT]) {
            rollback_back(&g_rollback, &demo_state);
            vm_resync_after_state_change(&demo_state);
        }
        if (mode_stack.count == 1 && input.key_pressed[SDL_SCANCODE_RIGHT]) {
            rollback_forward(&g_rollback, &demo_state);
            vm_resync_after_state_change(&demo_state);
        }

        // Paso fijo mientras se graba o se reproduce (M15): una sesion grabada existe para ser
        // reproducible, y con dt variable la reproduccion divergiria del original en cuanto un
        // temporizador cayera en otro frame.
        f32 dt = clock_tick(&clock);
        if (input_replay_active() || input_record_active()) {
            dt = k_input_record_dt;
        }
        audio_update(dt, &demo_state.bgm_position);
        // Vacio en Ship; en Debug/Dev comprueba mtimes como mucho cada 500 ms.
        hot_reload_update(dt);

        mode_stack_update(&mode_stack, ui_input, dt);
        if (backlog_mode.wants_close) {
            backlog_mode.wants_close = false;
            mode_stack_pop(&mode_stack);
        }
        if (menu_mode.wants_close) {
            menu_mode.wants_close = false;
            mode_stack_pop(&mode_stack);
        }
        if (save_load_mode.wants_close) {
            save_load_mode.wants_close = false;
            mode_stack_pop(&mode_stack);
        }

        // Lo mismo que el router de teclas de arriba, pero pedido con el raton desde la
        // botonera de VnMode (M15). Comparte el mismo `mode_stack.count == 1`: un boton no
        // puede abrir un overlay estando ya dentro de otro, igual que no lo puede la tecla.
        if (mode_stack.count == 1 && vn_mode.ui_request != VnUiRequest::None) {
            switch (vn_mode.ui_request) {
                case VnUiRequest::Backlog:
                    mode_stack_push(&mode_stack, &backlog_mode);
                    break;
                case VnUiRequest::Menu:
                    mode_stack_push(&mode_stack, &menu_mode);
                    break;
                case VnUiRequest::Save:
                    save_load_mode.is_save = true;
                    mode_stack_push(&mode_stack, &save_load_mode);
                    break;
                case VnUiRequest::Load:
                    save_load_mode.is_save = false;
                    mode_stack_push(&mode_stack, &save_load_mode);
                    break;
                case VnUiRequest::RollbackBack:
                    rollback_back(&g_rollback, &demo_state);
                    vm_resync_after_state_change(&demo_state);
                    break;
                case VnUiRequest::RollbackForward:
                    rollback_forward(&g_rollback, &demo_state);
                    vm_resync_after_state_change(&demo_state);
                    break;
                case VnUiRequest::None:
                    break;
            }
        }
        // Se limpia SIEMPRE, incluso si no se atendio (por ejemplo por haber un overlay
        // abierto): una peticion es de este frame, no una cola.
        vn_mode.ui_request = VnUiRequest::None;

        // MapMode <-> VnMode (M9, SPEC.md #10): pisar un trigger apila una VnMode nueva
        // con el guion de ese trigger; cuando esa escena termina, se vuelve al mapa con
        // la posicion correcta (ya vive en GameState, no hay que restaurar nada aparte).
        if (have_map && map_mode.pending_trigger_script != nullptr) {
            CompiledScript triggered_script{};
            if (script_load(map_mode.pending_trigger_script, &g_arena_scene,
                             &triggered_script) == ScriptLoadResult::Ok) {
                vn_mode.script    = triggered_script;
                vn_mode.finished  = false;
                vn_mode.layout_pc = 0xFFFFFFFFu;
                demo_state.vm.pc         = 0;
                demo_state.vm.cmd_phase = 0;
                mode_stack_push(&mode_stack, &vn_mode);
            } else {
                log_error("MapMode: no se pudo cargar el guion del trigger '%s'",
                          map_mode.pending_trigger_script);
            }
            map_mode.pending_trigger_script = nullptr;
        }
        if (have_map && mode_stack.count == 2 && mode_stack.modes[1] == &vn_mode &&
            vn_mode.finished) {
            mode_stack_pop(&mode_stack);
        }

        // VnMode ya avanzo la VM dentro de mode_stack_update() de mas arriba (SPEC.md
        // #10): un unico router de modos, no una llamada aparte a vm_update aqui.
        mode_stack_render(&mode_stack);

        // Una sola vez por frame, despues de todos los text_draw/text_layout del frame y
        // antes de render_flush(): sg_update_image solo admite una subida por pagina y por
        // frame (ver docs/DECISIONS.md, hito M2).
        glyph_cache_flush_dirty_pages();

        render_flush();


#if defined(SZ_EDITOR)
        EditorDiagnostics editor_diag{};
        editor_diag.frame_times        = frame_times;
        editor_diag.frame_time_count   = frames_recorded;
        editor_diag.max_frame_allocs   = max_frame_allocs;
        editor_diag.atlas_sprite_count = atlas_sprite_count();
        editor_diag.atlas_texture       = atlas;
        editor_update(input, &demo_state, &demo_script, "assets_src/scripts/demo.vns",
                      editor_diag, window_w, window_h, dt);
#endif

        // Justo antes de presentar: la regla de cero asignaciones se comprueba en este
        // punto exacto del frame (skill vne-memory-model).
        heap_guard_check_frame();
        if (g_frame_alloc_count > max_frame_allocs) {
            max_frame_allocs = g_frame_alloc_count;
        }

        render_present(window_w, window_h);

        frame_times[frame_index % k_frame_history_len] = dt;
        frame_index += 1;
        frames_recorded =
            frame_index < k_frame_history_len ? frame_index : k_frame_history_len;

        report_timer += dt;
        if (report_timer >= k_report_interval_s) {
            report_timer  = 0.0;
            f32 p99_ms    = frame_history_p99_ms(frame_times, frames_recorded);
            f32 fps       = dt > 0.0f ? 1.0f / dt : 0.0f;
            log_info(
                "fps~%.1f frame_p99=%.2fms draw_calls=%u sprites=%u heap_allocs_frame_max=%llu "
                "text_layout_calls=%u vm_pc=%u/%u",
                // El denominador es el guion que la VM esta ejecutando AHORA, no demo.vnc: al
                // pisar un trigger VnMode cambia de guion (M9) y el HUD seguia dividiendo
                // por los 185 comandos de demo.vnc, asi que "vm_pc=40/185" describia dos
                // guiones distintos a la vez.
                fps, p99_ms, g_render_draw_call_count, g_render_sprite_count_last_frame,
                static_cast<unsigned long long>(max_frame_allocs), g_text_layout_call_count,
                demo_state.vm.pc, vn_mode.script.cmd_count);
        }
    }

    input_record_end();  // escribe el .vnrec si se estaba grabando (M15)
#if defined(SZ_EDITOR)
    editor_shutdown();
#endif
    audio_shutdown();
    assets_shutdown();       // para el hilo de IO antes de tirar nada que pueda estar usando
    glyph_cache_shutdown();  // antes de render_shutdown, simetrico con el init de arriba
    render_shutdown();
    platform_window_destroy(&window);
    arena_destroy(&g_arena_frame);
    arena_destroy(&g_arena_scene);
    arena_destroy(&g_arena_perm);
    return 0;
}
