#pragma once
#include "core/arena.h"
#include "core/handle.h"
#include "game/mode.h"
#include "text/layout.h"
#include "vm/backlog.h"
#include "vm/vm.h"
#include "game/ui.h"

constexpr u32 k_backlog_visible_lines = 8;

// Rectangulo del boton de cerrar, en coordenadas virtuales. Compartido por update() y
// render() para que no puedan discrepar.
UiRect backlog_close_rect();

// Historial de dialogo (SPEC.md #10): lista desplazable sobre g_backlog (M4). Se apila
// sobre VnMode sin destruirlo (blocks_render_below=false: VnMode se sigue viendo detras).
struct BacklogMode : Mode {
    const CompiledScript* script = nullptr;  // para resolver text_id a texto
    FontHandle             font;
    Arena*                 scratch_arena = nullptr;  // g_arena_scene: layouts de esta pantalla
    i32                    scroll        = 0;         // indice de la entrada mas antigua visible
    bool                   wants_close   = false;

    // Cache de layouts (skill vne-rendering: "text_layout no se llama por frame"): solo
    // se reconstruyen cuando `scroll` cambia, no en cada render().
    TextLayout cached_layouts[k_backlog_visible_lines];
    u32        cached_count  = 0;
    i32        cached_scroll = -1;
    // catalog_generation() con la que se construyo el cache (M14). Sin esto, cambiar de
    // idioma con el backlog ya abierto dejaria las lineas en el idioma anterior: el cache
    // solo se reconstruia al hacer scroll. Mismo mecanismo que usa VnMode desde M10.
    u32        cached_locale_gen = 0xFFFFFFFFu;

    // Raton (M15): boton de cerrar y rueda para desplazarse.
    bool       hovered_close      = false;
    TextLayout close_label;
    bool       close_label_built = false;

    void update(const InputState& input, f32 dt) override;
    void render() override;
    bool blocks_render_below() const override { return false; }
};
