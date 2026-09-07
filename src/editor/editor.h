#pragma once
#include "base/types.h"

// Editor de desarrollo (SPEC.md #10 hito M8): Dear ImGui, activable con F1. SPEC.md #6
// dice literalmente "en Ship, todo el codigo de src/editor/ queda excluido del build":
// CMakeLists.txt ni siquiera añade este .cpp (ni ImGui) al target en una configuracion
// Ship, asi que estas declaraciones existen siempre (para que main.cpp compile en
// cualquier config) pero SOLO tienen una definicion real cuando VN_EDITOR esta definido
// (Dev). Los llamantes deben envolver sus usos en `#if defined(VN_EDITOR)` de todas
// formas, porque en Ship el .cpp no existe y el enlazador fallaria si algo las llama de
// verdad ahi.

struct GameState;
struct CompiledScript;
struct InputState;

void editor_init();
void editor_shutdown();
void editor_toggle();
bool editor_active();

// Datos de diagnostico que ya existian en main.cpp (historial de frame time, contador de
// allocs, tamano del atlas): el editor los muestra, no los posee.
struct EditorDiagnostics {
    const f32* frame_times      = nullptr;
    u32        frame_time_count = 0;
    u64        max_frame_allocs = 0;
    u32        atlas_sprite_count = 0;
};

// Alimenta ImGui con el frame actual y dibuja los paneles (sin emitir nada a la GPU
// todavia: eso lo hace editor_render(), SPEC.md #6.5 "editor_render() -> solo si
// VN_EDITOR", despues de gfx_flush() y antes de gfx_present()). script_path es el .vns
// fuente que el panel de recarga vigila (mtime cada 500ms, SPEC.md #7.4); *out_script se
// recarga en la misma arena de escena si cambio.
void editor_update(const InputState& input, GameState* state, CompiledScript* script,
                    const char* script_path, const EditorDiagnostics& diag, i32 window_w,
                    i32 window_h, f32 dt);
void editor_render();
