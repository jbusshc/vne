#include "editor/editor.h"

#if defined(VN_EDITOR)

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

#include <sokol_gfx.h>

#define SOKOL_IMGUI_IMPL
#define SOKOL_IMGUI_NO_SOKOL_APP
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996 4127 4189)
#endif
#include <util/sokol_imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "base/arena.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "platform/input.h"
#include "vm/script_load.h"
#include "vm/state.h"
#include "vm/vm.h"

namespace {

bool g_active = false;
bool g_setup  = false;

// Recarga de scripts (SPEC.md #7.4: watcher de mtime cada 500ms). vne_bake se invoca
// como subproceso en vez de linkar vne_script_tools en vne_game: el compilador del DSL
// se queda confinado a herramientas offline igual que en cualquier otra configuracion
// (skill vne-script-dsl, "cero parsing en release"); aqui simplemente se dispara desde
// dentro del editor, no se le da al juego la capacidad de parsear.
struct ScriptWatcher {
    char  vns_path[256] = {};
    char  vnc_path[256] = {};
    i64   last_mtime     = 0;
    f32   check_timer     = 0.0f;
};

ScriptWatcher g_watcher;

i64 file_mtime(const char* path) {
    struct stat st{};
    if (stat(path, &st) != 0) {
        return 0;
    }
    return static_cast<i64>(st.st_mtime);
}

void check_hot_reload(GameState* state, CompiledScript* script, f32 dt) {
    if (g_watcher.vns_path[0] == '\0') {
        return;
    }
    g_watcher.check_timer += dt;
    if (g_watcher.check_timer < 0.5f) {
        return;
    }
    g_watcher.check_timer = 0.0f;

    i64 mtime = file_mtime(g_watcher.vns_path);
    if (mtime == 0 || mtime == g_watcher.last_mtime) {
        return;
    }
    g_watcher.last_mtime = mtime;

    char cmd[600];
    // "./vne_bake" en vez de "vne_bake" a secas: cmd.exe /c (lo que system() invoca por
    // debajo en Windows) no siempre resuelve el ejecutable del propio directorio de
    // trabajo sin el prefijo, aunque vne_bake viva justo al lado de vne_game.
#if defined(_WIN32)
    std::snprintf(cmd, sizeof(cmd), ".\\vne_bake.exe script \"%s\" \"%s\"", g_watcher.vns_path,
                  g_watcher.vnc_path);
#else
    std::snprintf(cmd, sizeof(cmd), "./vne_bake script \"%s\" \"%s\"", g_watcher.vns_path,
                  g_watcher.vnc_path);
#endif
    // system() asigna heap (fork/exec del proceso hijo): aceptable aqui porque la
    // recarga es una accion de desarrollo disparada por un archivo modificado, no algo
    // que ocurra en un frame normal de juego (analogo a la excepcion ya aceptada para
    // Lua/audio, ADR-0032/ADR-0035, pero mas alla de dudarlo: esto ni siquiera existe en
    // Ship, VN_EDITOR nunca esta definido ahi).
    heap_guard_suspend();
    int result = std::system(cmd);
    heap_guard_resume();

    if (result != 0) {
        log_error("editor: vne_bake fallo recompilando '%s'", g_watcher.vns_path);
        return;
    }

    CompiledScript reloaded{};
    if (script_load(g_watcher.vnc_path, &g_arena_scene, &reloaded) == ScriptLoadResult::Ok) {
        *script     = reloaded;
        state->vm.pc         = 0;
        state->vm.cmd_phase = 0;
        log_info("editor: '%s' recargado sin reiniciar (SPEC.md #12)", g_watcher.vns_path);
    }
}

void draw_gamestate_panel(GameState* state) {
    if (!ImGui::CollapsingHeader("GameState", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Text("vm.pc = %u", state->vm.pc);
    ImGui::Text("vm.call_depth = %u", state->vm.call_depth);
    ImGui::Text("bg_id = %u", state->bg_id);
    ImGui::Text("bgm_track_id = %u  bgm_position = %.2f", state->bgm_track_id,
                static_cast<double>(state->bgm_position));
    ImGui::Text("rng_state = %u", state->rng_state);

    if (ImGui::TreeNode("actors[]")) {
        for (u32 i = 0; i < k_max_actor_slots; ++i) {
            const ActorSlot& a = state->actors[i];
            if (a.actor_id != 0) {
                ImGui::Text("slot %u: actor=%u pose=%u alpha=%.2f", i, a.actor_id, a.pose_id,
                            static_cast<double>(a.alpha));
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("vars[0..15] (editable)")) {
        for (u32 i = 0; i < 16; ++i) {
            char label[16];
            std::snprintf(label, sizeof(label), "vars[%u]", i);
            ImGui::InputInt(label, &state->vars[i]);
        }
        ImGui::TreePop();
    }
}

void draw_jump_panel(GameState* state, const CompiledScript& script) {
    if (!ImGui::CollapsingHeader("Saltar a comando")) {
        return;
    }
    static int target_pc = 0;
    ImGui::InputInt("pc destino", &target_pc);
    if (ImGui::Button("Saltar") && script.cmd_count > 0) {
        if (target_pc < 0) target_pc = 0;
        if (static_cast<u32>(target_pc) >= script.cmd_count) {
            target_pc = static_cast<int>(script.cmd_count) - 1;
        }
        state->vm.pc         = static_cast<u32>(target_pc);
        state->vm.cmd_phase = 0;
    }
    ImGui::Text("cmd_count = %u", script.cmd_count);
}

void draw_diagnostics_panel(const EditorDiagnostics& diag) {
    if (!ImGui::CollapsingHeader("Diagnostico", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::Text("heap_allocs_frame_max = %llu",
                static_cast<unsigned long long>(diag.max_frame_allocs));
    ImGui::Text("atlas_sprite_count = %u", diag.atlas_sprite_count);
    if (diag.frame_time_count > 0) {
        ImGui::PlotLines("frame time (s)", diag.frame_times,
                          static_cast<int>(diag.frame_time_count), 0, nullptr, 0.0f, 0.05f,
                          ImVec2(0, 80));
    }
}

void draw_reload_panel() {
    if (!ImGui::CollapsingHeader("Recarga de scripts")) {
        return;
    }
    ImGui::Text("Vigilando: %s",
                g_watcher.vns_path[0] != '\0' ? g_watcher.vns_path : "(ninguno)");
    ImGui::TextWrapped(
        "Guarda el .vns con cualquier editor de texto: se recompila y recarga solo, sin "
        "reiniciar el juego (comprobacion cada 0.5s, SPEC.md #7.4).");
}

}  // namespace

void editor_init() {
    // Ruta absoluta (VNE_SOURCE_DIR, ver CMakeLists.txt): assets_src/scripts/ nunca se
    // copia al directorio de build (a diferencia de png/ogg/ttf), asi que una ruta
    // relativa al CWD de vne_game no lo encontraria.
    std::snprintf(g_watcher.vns_path, sizeof(g_watcher.vns_path), "%s",
                  VNE_SOURCE_DIR "/assets_src/scripts/demo.vns");
    std::snprintf(g_watcher.vnc_path, sizeof(g_watcher.vnc_path),
                  "%s", "assets_baked/demo.vnc");
    g_watcher.last_mtime = file_mtime(g_watcher.vns_path);

    // Mismo formato que el swapchain al que gfx_present() ya dibuja (gfx_backend.h): el
    // editor se renderiza dentro de esa misma pasada, despues del blit de letterbox
    // (SPEC.md #6.5).
    simgui_desc_t desc{};
    desc.color_format = SG_PIXELFORMAT_RGBA8;
    desc.depth_format = SG_PIXELFORMAT_NONE;
    desc.sample_count  = 1;
    simgui_setup(&desc);
    g_setup = true;
}

void editor_shutdown() {
    if (g_setup) {
        simgui_shutdown();
        g_setup = false;
    }
}

void editor_toggle() {
    g_active = !g_active;
}

bool editor_active() {
    return g_active;
}

void editor_update(const InputState& input, GameState* state, CompiledScript* script,
                    const char* script_path, const EditorDiagnostics& diag, i32 window_w,
                    i32 window_h, f32 dt) {
    // script_path solo se usa para mostrarlo en el panel (draw_reload_panel): la ruta
    // que de verdad se vigila la fija editor_init() (VNE_SOURCE_DIR, absoluta, ver
    // comentario ahi) y no cambia mientras el proceso vive.
    (void)script_path;
    check_hot_reload(state, script, dt);

    if (!g_active) {
        return;
    }

    simgui_add_mouse_pos_event(input.mouse_x, input.mouse_y);
    for (u32 i = 0; i < k_max_mouse_buttons; ++i) {
        if (input.mouse_pressed[i]) {
            simgui_add_mouse_button_event(static_cast<int>(i), true);
        } else if (!input.mouse_down[i]) {
            simgui_add_mouse_button_event(static_cast<int>(i), false);
        }
    }
    if (input.mouse_wheel_y != 0.0f) {
        simgui_add_mouse_wheel_event(0.0f, input.mouse_wheel_y);
    }

    simgui_frame_desc_t frame_desc{};
    frame_desc.width      = window_w;
    frame_desc.height     = window_h;
    frame_desc.delta_time = static_cast<double>(dt);
    frame_desc.dpi_scale  = 1.0f;
    simgui_new_frame(&frame_desc);

    ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("vne editor (F1)", nullptr);
    draw_gamestate_panel(state);
    draw_jump_panel(state, *script);
    draw_diagnostics_panel(diag);
    draw_reload_panel();
    ImGui::End();
}

void editor_render() {
    if (!g_active) {
        return;
    }
    simgui_render();
}

#endif  // VN_EDITOR
