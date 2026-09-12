#pragma once
#include "core/arena.h"
#include "core/handle.h"
#include "game/mode.h"
#include "game/ui.h"
#include "text/layout.h"
#include "vm/backlog.h"
#include "vm/save.h"
#include "formats/state.h"

constexpr u32 k_save_slot_count = 4;

// Pantalla de guardado/carga con miniaturas (SPEC.md #10, criterio de M7). Un unico modo
// sirve para ambos casos (is_save decide la accion de ENTER), en vez de dos modos casi
// identicos.
struct SaveLoadMode : Mode {
    GameState* state   = nullptr;
    Backlog*   backlog = nullptr;
    FontHandle font;
    Arena*     scratch_arena = nullptr;
    bool       is_save       = true;
    i32        selected      = 0;
    bool       wants_close   = false;

    TextureHandle slot_thumbnail[k_save_slot_count];
    bool          slot_has_data[k_save_slot_count] = {};
    TextLayout    slot_label[k_save_slot_count];
    bool          labels_built = false;

    // Raton (M15).
    i32        hovered_slot      = -1;
    bool       hovered_close     = false;
    TextLayout close_label;
    bool       close_label_built = false;

    void on_enter() override;
    void update(const InputState& input, f32 dt) override;
    void render() override;
    bool blocks_render_below() const override { return false; }
};

const char* save_slot_path(u32 slot_index);

// Rectangulos en coordenadas virtuales, compartidos por update() y render(). Expuestos para
// que un test pinche un hueco concreto sin adivinar coordenadas.
UiRect save_slot_rect(u32 slot_index);
UiRect save_close_rect();
