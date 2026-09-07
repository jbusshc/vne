#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "audio/audio.h"
#include "base/arena.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "game/backlog_mode.h"
#include "game/menu_mode.h"
#include "game/mode.h"
#include "game/save_load_mode.h"
#include "game/vn_mode.h"
#include "gfx/gfx.h"
#include "gfx/texture.h"
#include "platform/clock.h"
#include "platform/input.h"
#include "platform/window.h"
#include "text/font.h"
#include "text/glyph_cache.h"
#include "text/layout.h"
#include "script/lua_bindings.h"
#include "vm/backlog.h"
#include "vm/rollback.h"
#include "vm/save.h"
#include "vm/script_load.h"
#include "vm/vm.h"

// M1: renderizado 2D. Ademas del bucle base de M0, dibuja un stress test de 5000 sprites
// de un unico atlas para verificar el criterio de aceptacion de M1 (SPEC.md #12): deben
// resolverse en una sola draw call a mas de 300 fps.
//
// M2: texto. Ademas, monta un cuadro de dialogo de prueba con marcado inline y furigana,
// avanzando con el efecto de maquina de escribir (visible_glyphs), para demostrar que
// text_layout no se vuelve a llamar por frame (SPEC.md #12).
//
// M3: VM y DSL. Carga assets_baked/demo.vnc (compilado por vne_bake desde
// assets_src/scripts/demo.vns) y lo avanza con vm_update() cada frame. Con
// --autoplay-script <ruta> corre un guion entero via vm_skip_current() sin abrir ventana,
// a maxima velocidad (skill vne-build-verify), y sale con codigo 0/1.

constexpr f32 k_demo_typewriter_glyphs_per_second = 18.0f;

constexpr usize k_perm_arena_size  = 64ull * 1024 * 1024;
constexpr usize k_scene_arena_size = 256ull * 1024 * 1024;
constexpr usize k_frame_arena_size = 8ull * 1024 * 1024;

constexpr u32 k_frame_history_len   = 240;
constexpr f64 k_report_interval_s   = 2.0;
constexpr u32 k_stress_sprite_count = 5000;
constexpr u32 k_stress_grid_cols    = 100;
constexpr u32 k_stress_grid_rows    = 50;  // 100*50 = 5000

constexpr u32 k_atlas_bin_magic = 0x54414E56u;  // 'VNAT', ver tools/bake/main.cpp

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

// Rectangulo de un sprite dentro del atlas (ADR-0025): mismo layout binario que
// SpriteRect en tools/bake/main.cpp, sin compartir header porque uno es runtime y el
// otro una herramienta offline.
struct AtlasSpriteRect {
    u16 x, y, w, h;
};

// Lee el manifiesto del atlas horneado por vne_bake (ADR-0025: empaquetador real sobre
// assets_src/png/, o la rejilla procedural de respaldo si ese directorio esta vacio — en
// ambos casos, mismo formato de sprites). No hay todavia un modulo assets/ formal: esto
// es una lectura minima, solo para el stress test de M1. Devuelve nullptr si no se pudo
// cargar; el llamante debe seguir funcionando igual (placeholder magenta).
static AtlasSpriteRect* read_atlas_manifest(u32* out_count) {
    *out_count = 0;

    std::FILE* bin = std::fopen("assets_baked/atlas_00.bin", "rb");
    if (bin == nullptr) {
        log_error("No se encontro assets_baked/atlas_00.bin; ejecuta vne_bake primero.");
        return nullptr;
    }
    u32 header[5];
    if (std::fread(header, sizeof(header), 1, bin) != 1 || header[0] != k_atlas_bin_magic ||
        header[1] != 2u) {
        log_error("assets_baked/atlas_00.bin invalido o de una version anterior");
        std::fclose(bin);
        return nullptr;
    }
    u32 count = header[4];
    if (count == 0) {
        std::fclose(bin);
        return nullptr;
    }

    AtlasSpriteRect* sprites = arena_alloc_n<AtlasSpriteRect>(&g_arena_perm, count);
    if (sprites == nullptr ||
        std::fread(sprites, sizeof(AtlasSpriteRect), count, bin) != count) {
        log_error("assets_baked/atlas_00.bin truncado");
        std::fclose(bin);
        return nullptr;
    }
    std::fclose(bin);
    *out_count = count;
    return sprites;
}

// Corre un guion entero sin ventana ni GPU, a maxima velocidad, via vm_skip_current()
// (skill vne-build-verify: "--autoplay-script ... a maxima velocidad ... sale con codigo
// 0 o distinto de 0"). No es una demostracion visual: es la base de la verificacion
// automatizada de guiones completos.
static int run_autoplay(const char* script_path) {
    g_arena_perm  = arena_create(k_perm_arena_size, "perm");
    g_arena_scene = arena_create(k_scene_arena_size, "scene");
    g_arena_frame = arena_create(k_frame_arena_size, "frame");
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
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--autoplay-script") == 0) {
            return run_autoplay(argv[i + 1]);
        }
    }

    g_arena_perm  = arena_create(k_perm_arena_size, "perm");
    g_arena_scene = arena_create(k_scene_arena_size, "scene");
    g_arena_frame = arena_create(k_frame_arena_size, "frame");
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    lua_init();
    audio_init();

    PlatformWindow window{};
    if (!platform_window_create(&window, "vne \xe2\x80\x94 M2", 1280, 720)) {
        return 1;
    }

    if (!gfx_init(&window)) {
        log_error("gfx_init fallo");
        return 1;
    }

    TextureHandle atlas{};
    if (texture_load("assets_baked/atlas_00.qoi", &atlas) != TextureLoadResult::Ok) {
        log_error("No se pudo cargar el atlas de prueba; se usara el placeholder magenta.");
    }

    u32              atlas_sprite_count = 0;
    AtlasSpriteRect* atlas_sprites      = read_atlas_manifest(&atlas_sprite_count);

    FontHandle demo_font = text_load_font("assets_src/ttf/NotoSansJP.ttf", 28);
    if (!demo_font.valid()) {
        log_error("No se pudo cargar la fuente de prueba NotoSansJP.ttf");
    }
    const char* demo_text =
        "Hola {b}mundo{/b}. {color=#ff5040}Texto en rojo{/color}. "
        "{ruby=\xE3\x81\x8B\xE3\x82\x93\xE3\x81\x98}\xE6\xBC\xA2\xE5\xAD\x97{/ruby} "
        "con furigana.";
    // La arena de escena (no la de frame) porque el layout debe sobrevivir entre frames:
    // el efecto de maquina de escribir solo cambia visible_glyphs, nunca relayoutea
    // (regla del skill vne-rendering). Arriba de la pantalla para no pelear visualmente
    // con el cuadro de dialogo real de M7 (VnMode), que vive en la parte de abajo.
    TextLayout demo_layout = text_layout(demo_font, demo_text, 1700.0f, &g_arena_scene);
    f32        visible_glyphs_f = 0.0f;

    GameState     demo_state{};
    CompiledScript demo_script{};
    if (script_load("assets_baked/demo.vnc", &g_arena_scene, &demo_script) !=
        ScriptLoadResult::Ok) {
        log_error("No se pudo cargar assets_baked/demo.vnc; ejecuta vne_bake primero.");
    }

    // Pila de modos (SPEC.md #10, M7): VnMode dirige la VM y el cuadro de dialogo real
    // (cierra ADR-0023); Backlog/Menu/SaveLoad se apilan encima sin destruirlo.
    VnMode vn_mode{};
    vn_mode.state        = &demo_state;
    vn_mode.script        = demo_script;
    vn_mode.font          = demo_font;
    vn_mode.layout_arena = &g_arena_scene;

    BacklogMode backlog_mode{};
    backlog_mode.script        = &vn_mode.script;
    backlog_mode.font          = demo_font;
    backlog_mode.scratch_arena = &g_arena_scene;

    MenuMode menu_mode{};
    menu_mode.state         = &demo_state;
    menu_mode.font          = demo_font;
    menu_mode.scratch_arena = &g_arena_scene;

    SaveLoadMode save_load_mode{};
    save_load_mode.state         = &demo_state;
    save_load_mode.backlog       = &g_backlog;
    save_load_mode.font          = demo_font;
    save_load_mode.scratch_arena = &g_arena_scene;

    ModeStack mode_stack{};
    mode_stack_push(&mode_stack, &vn_mode);

    InputState input{};
    Clock      clock = clock_create();

    f32* frame_times      = arena_alloc_n<f32>(&g_arena_perm, k_frame_history_len);
    u32  frame_index      = 0;
    u32  frames_recorded  = 0;
    f64  report_timer     = 0.0;
    u64  max_frame_allocs = 0;

    const f32 cell_dst_w = static_cast<f32>(k_virtual_width) / static_cast<f32>(k_stress_grid_cols);
    const f32 cell_dst_h = static_cast<f32>(k_virtual_height) / static_cast<f32>(k_stress_grid_rows);

    while (!input.quit_requested) {
        arena_reset(&g_arena_frame);
        heap_guard_reset_frame();
        gfx_begin_frame();
        glyph_cache_begin_frame();

        platform_poll_events(&input);

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

        f32 dt = clock_tick(&clock);
        audio_update(dt, &demo_state.bgm_position);

        mode_stack_update(&mode_stack, input, dt);
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

        for (u32 i = 0; i < k_stress_sprite_count; ++i) {
            AtlasSpriteRect rect =
                atlas_sprite_count > 0 ? atlas_sprites[i % atlas_sprite_count]
                                       : AtlasSpriteRect{0, 0, 1, 1};

            Sprite s{};
            s.tex   = atlas;
            s.src_x = static_cast<f32>(rect.x);
            s.src_y = static_cast<f32>(rect.y);
            s.src_w = static_cast<f32>(rect.w);
            s.src_h = static_cast<f32>(rect.h);

            u32 col = i % k_stress_grid_cols;
            u32 row = i / k_stress_grid_cols;
            s.dst_x = static_cast<f32>(col) * cell_dst_w;
            s.dst_y = static_cast<f32>(row) * cell_dst_h;
            s.dst_w = cell_dst_w - 1.0f;
            s.dst_h = cell_dst_h - 1.0f;
            s.layer = static_cast<u16>(GfxLayer::Actors);

            gfx_draw_sprite(s);
        }

        // VnMode ya avanzo la VM dentro de mode_stack_update() de mas arriba (SPEC.md
        // #10): un unico router de modos, no una llamada aparte a vm_update aqui.
        mode_stack_render(&mode_stack);

        visible_glyphs_f += k_demo_typewriter_glyphs_per_second * dt;
        if (visible_glyphs_f > static_cast<f32>(demo_layout.count) * 1.5f) {
            visible_glyphs_f = 0.0f;  // reinicia el efecto para que la demo haga bucle
        }
        u32 visible_glyphs = static_cast<u32>(visible_glyphs_f);
        text_draw(demo_layout, 80.0f, 60.0f, visible_glyphs);

        // Una sola vez por frame, despues de todos los text_draw/text_layout del frame y
        // antes de gfx_flush(): sg_update_image solo admite una subida por pagina y por
        // frame (ver docs/DECISIONS.md, hito M2).
        glyph_cache_flush_dirty_pages();

        gfx_flush();

        i32 window_w = 0, window_h = 0;
        platform_window_size_px(&window, &window_w, &window_h);

        // Justo antes de presentar: la regla de cero asignaciones se comprueba en este
        // punto exacto del frame (skill vne-memory-model).
        heap_guard_check_frame();
        if (g_frame_alloc_count > max_frame_allocs) {
            max_frame_allocs = g_frame_alloc_count;
        }

        gfx_present(window_w, window_h);

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
                fps, p99_ms, g_gfx_draw_call_count, k_stress_sprite_count,
                static_cast<unsigned long long>(max_frame_allocs), g_text_layout_call_count,
                demo_state.vm.pc, demo_script.cmd_count);
        }
    }

    audio_shutdown();
    gfx_shutdown();
    platform_window_destroy(&window);
    arena_destroy(&g_arena_frame);
    arena_destroy(&g_arena_scene);
    arena_destroy(&g_arena_perm);
    return 0;
}
