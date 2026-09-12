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
    // glifos; aqui solo hace falta cargar la fuente correcta). Que idiomas hay, con que
    // catalogo y que fuente, sale de la tabla de game/locales.h (M14).
    FontHandle* dialogue_font_slot = nullptr;
    FontHandle  latin_font;
    FontHandle  cjk_font;
    // M12: la variante en negrita tiene que seguir al idioma igual que la normal, o {b}
    // dibujaria glifos latinos engordados sobre texto japones.
    FontHandle* bold_font_slot = nullptr;
    FontHandle  latin_bold_font;
    FontHandle  cjk_bold_font;
    i32 locale_index = 0;  // indice en k_locales (game/locales.h)

    // Arena donde vive el CATALOGO cargado. Distinta de scratch_arena a proposito (M14): el
    // catalogo dura lo que dura el idioma elegido, mientras que los layouts de este menu son
    // de usar y tirar. Usar una sola para las dos cosas hacia que resetear la arena de
    // escena —cuyo proposito documentado es justo eso al cambiar de capitulo— dejara el
    // catalogo global apuntando a memoria liberada. No pasaba hoy porque nadie la resetea
    // todavia, pero era una mina: en un test lo fue, con SIGSEGV.
    Arena* catalog_arena = nullptr;

    // Aplica el idioma actual: carga su catalogo y pone las fuentes que le tocan. Publica
    // porque main.cpp la llama al arrancar para aplicar lo que venia en config.ini, sin
    // tener que simular una pulsacion de tecla.
    void apply_locale();

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
