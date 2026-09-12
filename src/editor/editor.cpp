#include "editor/editor.h"

#if defined(SZ_EDITOR)

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

#include "core/arena.h"
#include "core/heap_guard.h"
#include "core/heap_guard_hooks.h"
#include "core/log.h"
#include "render/atlas.h"
#include "render/texture.h"
#include "render/texture_internal.h"
#include "platform/files.h"
#include "platform/input.h"
#include "vm/script_load.h"
#include "formats/state.h"
#include "vm/vm.h"

namespace {

// Firma compartida por ImGui::SetAllocatorFunctions y simgui_allocator_t: las dos piden
// exactamente void*(size_t, void*) y void(void*, void*).
void* imgui_alloc_counted(size_t size, void* /*user*/) {
    return heap_guard_malloc(size, HeapSource::ImGui);
}

void imgui_free_counted(void* p, void* /*user*/) {
    heap_guard_free(p);
}

bool g_active = false;
bool g_setup  = false;

// Recarga de scripts (SPEC.md #7.4: watcher de mtime cada 500ms). sz_bake se invoca
// como subproceso en vez de linkar sz_content en sz_runtime: el compilador del DSL
// se queda confinado a herramientas offline igual que en cualquier otra configuracion
// (skill vne-script-dsl, "cero parsing en release"); aqui simplemente se dispara desde
// dentro del editor, no se le da al juego la capacidad de parsear.
struct ScriptWatcher {
    char  vns_path[256] = {};
    char  vnc_path[256] = {};  // ruta de archivo real: la usa sz_bake como argumento
    // Nombre logico (M11, ver vfs/pak.h): lo que script_load() espera, no una ruta de
    // archivo. Distinto de vnc_path porque sz_bake si necesita una ruta real -- son el
    // mismo destino visto por dos consumidores distintos (herramienta offline vs backend
    // de assets en runtime).
    char  vnc_logical_name[256] = {};
    i64   last_mtime     = 0;
    f32   check_timer     = 0.0f;
};

ScriptWatcher g_watcher;

// Guiones que el panel de recarga ofrece para vigilar (M15: hasta ahora vigilaba demo.vns y
// solo demo.vns, fijo en editor_init). Se escanea UNA vez en editor_init y no por frame:
// dir_list_by_extension pasa por SDL, que asigna, y el editor corre dentro del bucle de
// frame — escanear ahi romperia la regla de cero heap por un desplegable.
constexpr u32 k_max_watchable_scripts = 32;
struct WatchableScripts {
    char names[k_max_watchable_scripts][64] = {};  // sin extension
    u32  count                              = 0;
};
WatchableScripts g_watchable;

void collect_watchable(void* userdata, const char* name_no_ext, const char* /*full_name*/) {
    WatchableScripts* list = static_cast<WatchableScripts*>(userdata);
    if (list->count >= k_max_watchable_scripts) {
        return;
    }
    std::snprintf(list->names[list->count], sizeof(list->names[0]), "%s", name_no_ext);
    list->count += 1;
}


i64 file_mtime(const char* path) {
    struct stat st{};
    if (stat(path, &st) != 0) {
        return 0;
    }
    return static_cast<i64>(st.st_mtime);
}

// Apunta el watcher a <name>.vns / <name>.vnc. Un solo sitio para las tres rutas, que
// tienen que describir el mismo guion o la recarga recompilaria una cosa y cargaria otra.
void watch_script(const char* name_no_ext) {
    std::snprintf(g_watcher.vns_path, sizeof(g_watcher.vns_path),
                  "%s/assets_src/scripts/%s.vns", SZ_SOURCE_DIR, name_no_ext);
    std::snprintf(g_watcher.vnc_path, sizeof(g_watcher.vnc_path), "assets_baked/%s.vnc",
                  name_no_ext);
    std::snprintf(g_watcher.vnc_logical_name, sizeof(g_watcher.vnc_logical_name), "%s.vnc",
                  name_no_ext);
    g_watcher.last_mtime  = file_mtime(g_watcher.vns_path);
    g_watcher.check_timer = 0.0f;
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
    // "./sz_bake" en vez de "sz_bake" a secas: cmd.exe /c (lo que system() invoca por
    // debajo en Windows) no siempre resuelve el ejecutable del propio directorio de
    // trabajo sin el prefijo, aunque sz_bake viva justo al lado de sz_runtime.
#if defined(_WIN32)
    std::snprintf(cmd, sizeof(cmd), ".\\sz_bake.exe script \"%s\" \"%s\"", g_watcher.vns_path,
                  g_watcher.vnc_path);
#else
    std::snprintf(cmd, sizeof(cmd), "./sz_bake script \"%s\" \"%s\"", g_watcher.vns_path,
                  g_watcher.vnc_path);
#endif
    // system() asigna heap (fork/exec del proceso hijo): aceptable aqui porque la
    // recarga es una accion de desarrollo disparada por un archivo modificado, no algo
    // que ocurra en un frame normal de juego (analogo a la excepcion ya aceptada para
    // Lua/audio, ADR-0032/ADR-0035, pero mas alla de dudarlo: esto ni siquiera existe en
    // Ship, SZ_EDITOR nunca esta definido ahi).
    heap_guard_suspend();
    int result = std::system(cmd);
    heap_guard_resume();

    if (result != 0) {
        log_error("editor: sz_bake fallo recompilando '%s'", g_watcher.vns_path);
        return;
    }

    CompiledScript reloaded{};
    if (script_load(g_watcher.vnc_logical_name, &g_arena_scene, &reloaded) ==
        ScriptLoadResult::Ok) {
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

// Visor de atlas (criterio 3 de M15 en SPEC.md #12: "el visor de atlas muestra la textura
// real, no un recuento de sprites"). Era un criterio de M8 que se cerro con una sola linea
// de texto — "atlas_sprite_count = N" — que no ensena nada de lo que hay dentro.
void draw_atlas_panel(const EditorDiagnostics& diag) {
    if (!ImGui::CollapsingHeader("Visor de atlas")) {
        return;
    }
    u32 count = atlas_sprite_count();
    ImGui::Text("%u sprites en atlas_00.bin", count);
    if (!diag.atlas_texture.valid()) {
        ImGui::TextUnformatted("No hay textura de atlas cargada.");
        return;
    }

    i32 tex_w = 0, tex_h = 0;
    texture_size(diag.atlas_texture, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) {
        ImGui::TextUnformatted("La textura del atlas tiene tamano cero.");
        return;
    }
    ImTextureID tex_id = static_cast<ImTextureID>(simgui_imtextureid_with_sampler(
        texture_gpu_image(diag.atlas_texture), texture_gpu_sampler(diag.atlas_texture)));

    static f32 zoom     = 0.5f;
    static int selected = -1;
    ImGui::SliderFloat("zoom", &zoom, 0.1f, 2.0f, "%.2fx");

    // Lista de nombres a la izquierda. Es el otro sentido de la tabla de nombres del atlas
    // (M13 solo permitia nombre -> rectangulo, ver atlas_name_at).
    ImGui::BeginChild("atlas_names", ImVec2(260.0f, 220.0f), true);
    for (u32 i = 0; i < count; ++i) {
        AtlasSprite r{};
        atlas_sprite_at(i, &r);
        char label[160];
        std::snprintf(label, sizeof(label), "%s  %ux%u", atlas_name_at(i), r.w, r.h);
        if (ImGui::Selectable(label, selected == static_cast<int>(i))) {
            selected = static_cast<int>(i);
        }
    }
    ImGui::EndChild();

    // El sprite elegido, recortado del atlas con coordenadas UV: si el recorte fuera mal, se
    // veria el sprite equivocado, que es exactamente el fallo que un visor existe para
    // cazar.
    if (selected >= 0 && static_cast<u32>(selected) < count) {
        AtlasSprite r{};
        atlas_sprite_at(static_cast<u32>(selected), &r);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", atlas_name_at(static_cast<u32>(selected)));
        ImGui::Text("x=%u y=%u w=%u h=%u", r.x, r.y, r.w, r.h);
        ImVec2 uv0(static_cast<f32>(r.x) / static_cast<f32>(tex_w),
                   static_cast<f32>(r.y) / static_cast<f32>(tex_h));
        ImVec2 uv1(static_cast<f32>(r.x + r.w) / static_cast<f32>(tex_w),
                   static_cast<f32>(r.y + r.h) / static_cast<f32>(tex_h));
        ImGui::Image(tex_id, ImVec2(static_cast<f32>(r.w) * 2.0f, static_cast<f32>(r.h) * 2.0f),
                     uv0, uv1);
        ImGui::EndGroup();
    }

    // Y el atlas entero, que es lo que el criterio pide de verdad: ver la textura.
    ImGui::Text("atlas_00.qoi  %dx%d", tex_w, tex_h);
    ImGui::Image(tex_id, ImVec2(static_cast<f32>(tex_w) * zoom, static_cast<f32>(tex_h) * zoom));
}

void draw_reload_panel() {
    if (!ImGui::CollapsingHeader("Recarga de scripts")) {
        return;
    }
    ImGui::Text("Vigilando: %s",
                g_watcher.vns_path[0] != '\0' ? g_watcher.vns_path : "(ninguno)");

    // Elegir el guion vigilado (M15). Hasta aqui era demo.vns fijo, puesto a mano en
    // editor_init: para trabajar en demo_branching.vns o demo_transitions.vns con recarga en
    // caliente habia que recompilar el juego.
    for (u32 i = 0; i < g_watchable.count; ++i) {
        if (i % 3 != 0) {
            ImGui::SameLine();
        }
        bool is_current =
            std::strstr(g_watcher.vnc_logical_name, g_watchable.names[i]) != nullptr;
        if (ImGui::RadioButton(g_watchable.names[i], is_current)) {
            watch_script(g_watchable.names[i]);
            log_info("editor: vigilando ahora '%s'", g_watcher.vns_path);
        }
    }
    if (g_watchable.count == 0) {
        ImGui::TextUnformatted("No se encontro ningun .vns en assets_src/scripts/.");
    }

    ImGui::TextWrapped(
        "Guarda el .vns con cualquier editor de texto: se recompila y recarga solo, sin "
        "reiniciar el juego (comprobacion cada 0.5s, SPEC.md #7.4).");
}

}  // namespace

void editor_init() {
    // Ruta absoluta (SZ_SOURCE_DIR, ver CMakeLists.txt): assets_src/scripts/ nunca se
    // copia al directorio de build (a diferencia de png/ogg/ttf), asi que una ruta
    // relativa al CWD de sz_runtime no lo encontraria.
    watch_script("demo");

    // Lista de guiones vigilables, escaneada una sola vez aqui (fuera del bucle de frame:
    // dir_list_by_extension pasa por SDL y asigna, ver WatchableScripts).
    dir_list_by_extension(SZ_SOURCE_DIR "/assets_src/scripts", ".vns", collect_watchable,
                           &g_watchable);
    log_info("editor: %u guiones vigilables en assets_src/scripts/", g_watchable.count);

    // Mismo formato que el swapchain al que render_present() ya dibuja (rhi/rhi.h): el
    // editor se renderiza dentro de esa misma pasada, despues del blit de letterbox
    // (SPEC.md #6.5).
    // ImGui y sokol_imgui asignan con malloc, invisible para operator new. El editor corre
    // en TODOS los frames mientras esta abierto, asi que sin estos hooks la regla de cero
    // heap (SPEC.md #4) no diria nada sobre el unico camino que mas asigna de todo el juego
    // en Dev. Ver ADR-0058.
    ImGui::SetAllocatorFunctions(imgui_alloc_counted, imgui_free_counted, nullptr);

    simgui_desc_t desc{};
    desc.color_format = SG_PIXELFORMAT_RGBA8;
    desc.depth_format = SG_PIXELFORMAT_NONE;
    desc.sample_count  = 1;
    desc.allocator.alloc_fn = imgui_alloc_counted;
    desc.allocator.free_fn  = imgui_free_counted;
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
    // que de verdad se vigila la fija editor_init() (SZ_SOURCE_DIR, absoluta, ver
    // comentario ahi) y no cambia mientras el proceso vive.
    (void)script_path;
    check_hot_reload(state, script, dt);

    if (!g_active) {
        return;
    }

    // Excepcion a la regla de cero heap (SPEC.md #4), de la misma familia que
    // hot_reload_update y el subproceso `sz_bake`: el editor es herramienta de desarrollo
    // y no existe en Ship (ADR-0041 lo excluye del build entero a nivel de CMake), asi que
    // esto no puede degradar el juego que se distribuye.
    //
    // Medido en M12 forzando el editor abierto (no se puede pulsar F1 en este entorno):
    // 54 asignaciones en su primer frame —contexto y atlas de fuentes de ImGui— y 1 en
    // cuatro frames mas mientras ImGui hace crecer sus draw lists; a partir de ahi 0, que
    // es el comportamiento esperado de ImGui (reutiliza sus buffers una vez dimensionados).
    // Acotar en vez de arreglar es lo correcto aqui: esos buffers son de ImGui y no van a
    // salir de una arena nuestra.
    heap_guard_suspend();
    struct GuardResume {
        ~GuardResume() { heap_guard_resume(); }
    } guard_resume;

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
    draw_atlas_panel(diag);
    draw_reload_panel();
    ImGui::End();
}

void editor_render() {
    if (!g_active) {
        return;
    }
    // Misma excepcion que en editor_update: simgui_render sube los draw lists de ImGui a
    // la GPU y puede hacer crecer sus buffers.
    heap_guard_suspend();
    simgui_render();
    heap_guard_resume();
}

#endif  // SZ_EDITOR
