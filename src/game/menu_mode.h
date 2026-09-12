#pragma once
#include "core/arena.h"
#include "core/handle.h"
#include "game/mode.h"
#include "game/ui.h"
#include "text/layout.h"
#include "formats/state.h"

// Menu de configuracion (SPEC.md #10): volumenes de bus (M6) con sliders, mas cambio de
// idioma en caliente (M10). Se apila sobre VnMode.
//
// M15: se maneja tambien con el raton — los sliders se arrastran y la fila de idioma se
// pincha. Hasta aqui era solo teclado (flechas), que es lo que ADR-0040 dejo anotado.

constexpr i32 k_menu_row_count = 5;  // 4 buses + idioma

// Rectangulos en coordenadas virtuales, compartidos por update() y render() para que lo
// que se dibuja y lo que se puede pulsar no puedan discrepar. Expuestos para que un test
// pinche un slider concreto sin adivinar coordenadas.
UiRect menu_row_rect(i32 row);
UiRect menu_slider_rect(i32 bus);
UiRect menu_close_rect();
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

    // Si false, cambiar una preferencia NO escribe config.ini (M15). Lo pone a false quien
    // reproduce una sesion grabada: una reproduccion no debe pisarle la configuracion a
    // quien este jugando, ni depender de la que hubiera.
    bool persist_config = true;

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

    // Raton (M15). dragging_bus es el bus cuyo slider se esta arrastrando, o -1: hace falta
    // recordarlo entre frames por dos razones. Una, para que arrastrar siga funcionando
    // aunque el cursor se salga un poco del carril. Y otra mas concreta: config.ini se
    // escribe al SOLTAR, no en cada frame del arrastre, que serian sesenta escrituras de
    // archivo por segundo.
    i32        dragging_bus = -1;
    i32        hovered_row   = -1;
    bool       hovered_close = false;
    TextLayout close_label;
    bool       close_label_built = false;

    void update(const InputState& input, f32 dt) override;
    void render() override;
    bool blocks_render_below() const override { return false; }
};
