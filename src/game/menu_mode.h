#pragma once
#include "base/arena.h"
#include "base/handle.h"
#include "game/mode.h"
#include "text/layout.h"
#include "vm/state.h"

// Menu de configuracion (SPEC.md #10): volumenes de bus (M6) con sliders discretos
// (flechas izq/der, sin mouse: platform/input.h no rastrea el raton todavia, ver
// docs/DECISIONS.md). Se apila sobre VnMode.
struct MenuMode : Mode {
    GameState* state = nullptr;
    FontHandle font;
    Arena*     scratch_arena = nullptr;
    i32        selected      = 0;  // indice de bus (0..3)
    bool       wants_close   = false;

    // Cache de layouts (skill vne-rendering): solo se reconstruye cuando cambia algo
    // visible (seleccion o un volumen), no en cada render().
    TextLayout cached_lines[4];
    i32        cached_selected      = -1;
    f32        cached_volumes[4]    = {-1.0f, -1.0f, -1.0f, -1.0f};

    void update(const InputState& input, f32 dt) override;
    void render() override;
    bool blocks_render_below() const override { return false; }
};
