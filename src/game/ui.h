#pragma once
#include "core/types.h"
#include "render/render.h"
#include "platform/input.h"

// Hit-testing de la UI de novela visual (M15). Cuatro funciones inline sobre un rectangulo
// en coordenadas virtuales 1920x1080; sin estado, sin asignaciones y sin virtual, asi que
// se puede llamar por elemento y por frame sin pensarlo.
//
// **Las coordenadas de raton que llegan aqui son virtuales, no pixeles de ventana.** Los
// modos no conocen el tamano de la ventana (skill vne-rendering); la conversion la hace una
// sola vez main.cpp con render_window_to_virtual y los modos reciben el InputState ya
// convertido. Un raton sobre las barras del letterbox cae fuera de la pantalla virtual y
// por tanto fuera de cualquier rectangulo, que es el comportamiento correcto sin ningun
// caso especial.

struct UiRect {
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

inline bool ui_contains(const UiRect& r, f32 px, f32 py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

inline bool ui_hover(const UiRect& r, const InputState& input) {
    return ui_contains(r, input.mouse_x, input.mouse_y);
}

// Clic IZQUIERDO que empieza dentro del rectangulo. mouse_pressed es el flanco (solo el
// frame en que se pulsa, ver platform/input.cpp), no el estado sostenido: sin eso un boton
// se activaria 60 veces por segundo mientras se mantiene el boton.
inline bool ui_clicked(const UiRect& r, const InputState& input) {
    return input.mouse_pressed[0] && ui_hover(r, input);
}

// Arrastre: boton izquierdo mantenido con el cursor dentro. Lo usan los sliders de volumen,
// donde arrastrar tiene que seguir funcionando mientras el boton siga pulsado.
inline bool ui_dragging(const UiRect& r, const InputState& input) {
    return input.mouse_down[0] && ui_hover(r, input);
}

// Rectangulo relleno con la textura blanca de 1x1. Sigue siendo lo correcto para lo que de
// verdad es un color plano: los velos a pantalla completa de los overlays y el relleno de un
// slider. Lo que en M15 deja de ser un rectangulo solido son los elementos con forma
// (botones, paneles, carriles), que pasan a ui_draw_nine_slice.
void ui_draw_rect(const UiRect& r, u32 color, RenderLayer layer = RenderLayer::UI, u16 order = 0);

// Textura del atlas de sprites, de donde sale el arte de UI. La pone main.cpp una vez tras
// cargar el atlas; si nunca se pone (o el sprite pedido no esta en el atlas),
// ui_draw_nine_slice cae al rectangulo solido, que es como se veia la UI hasta M15 y nunca
// es un fallo fatal (SPEC.md #4).
void ui_set_atlas(TextureHandle atlas);

// Dibuja `atlas_name` estirado a `r` en nueve trozos: las cuatro esquinas a tamano original,
// los cuatro lados estirados en un eje y el centro en los dos. Es lo que permite usar un
// boton de 192x64 como cuadro de dialogo de 1800x220 sin que las esquinas redondeadas se
// deformen. Nueve sprites de la MISMA textura, asi que siguen cabiendo en el mismo lote y no
// suben el numero de draw calls (skill vne-rendering).
void ui_draw_nine_slice(const UiRect& r, const char* atlas_name, u32 tint,
                         RenderLayer layer = RenderLayer::UI, u16 order = 0, f32 corner = 16.0f);

// Atajos con el arte ya elegido, para que el aspecto de un boton se decida en un sitio y no
// en cada modo. Arte de Kenney UI Pack (CC0), ya en assets_src/png/ desde M3.
void ui_draw_button(const UiRect& r, u32 tint, RenderLayer layer = RenderLayer::UI, u16 order = 0);
void ui_draw_panel(const UiRect& r, u32 tint, RenderLayer layer = RenderLayer::UI, u16 order = 0);

// Carril y relleno de un slider de volumen, con `value` en 0..1.
void ui_draw_slider(const UiRect& track, f32 value);

// Colores de los estados de un elemento pinchable. Premultiplicados, como pide render.h.
constexpr u32 k_ui_button_idle    = 0xC0303030u;
constexpr u32 k_ui_button_hover   = 0xE0505050u;
constexpr u32 k_ui_button_active  = 0xF0707070u;
constexpr u32 k_ui_row_selected   = 0xA0404060u;

inline u32 ui_button_color(const UiRect& r, const InputState& input) {
    if (!ui_hover(r, input)) {
        return k_ui_button_idle;
    }
    return input.mouse_down[0] ? k_ui_button_active : k_ui_button_hover;
}
