#pragma once
#include "base/arena.h"
#include "base/handle.h"
#include "game/mode.h"
#include "text/layout.h"
#include "vm/state.h"

// Menu de configuracion (SPEC.md #10): volumenes de bus (M6) con sliders discretos
// (flechas izq/der, sin mouse: platform/input.h no rastrea el raton todavia, ver
// docs/DECISIONS.md), mas cambio de idioma en caliente (M10). Se apila sobre VnMode.
struct MenuMode : Mode {
    GameState* state = nullptr;
    FontHandle font;
    Arena*     scratch_arena = nullptr;
    i32        selected      = 0;  // indice de fila: 0..3 buses, 4 idioma
    bool       wants_close   = false;

    // Idioma (M10): dialogue_font_slot es la FontHandle que VnMode usa para dibujar
    // dialogo — este modo la reasigna al cambiar de idioma (latin_font para español,
    // cjk_font para japones, "CJK bajo demanda" ya existe desde M2 en el cacheo de
    // glifos; aqui solo hace falta cargar la fuente correcta). locale_vnl_paths[i] es
    // nullptr para el idioma base (sin catalogo, cae al texto del propio guion).
    FontHandle* dialogue_font_slot = nullptr;
    FontHandle  latin_font;
    FontHandle  cjk_font;
    // M12: la variante en negrita tiene que seguir al idioma igual que la normal, o {b}
    // dibujaria glifos latinos engordados sobre texto japones.
    FontHandle* bold_font_slot = nullptr;
    FontHandle  latin_bold_font;
    FontHandle  cjk_bold_font;
    static constexpr u32 k_locale_count               = 2;
    const char*           locale_names[k_locale_count] = {"Espanol", "Nihongo (placeholder)"};
    const char*           locale_vnl_paths[k_locale_count] = {nullptr, "ja.vnl"};
    i32                   locale_index                     = 0;

    // Cache de layouts (skill vne-rendering): solo se reconstruye cuando cambia algo
    // visible (seleccion o un volumen), no en cada render().
    TextLayout cached_lines[5];
    i32        cached_selected      = -1;
    f32        cached_volumes[4]    = {-1.0f, -1.0f, -1.0f, -1.0f};
    i32        cached_locale        = -1;

    void update(const InputState& input, f32 dt) override;
    void render() override;
    bool blocks_render_below() const override { return false; }
};
