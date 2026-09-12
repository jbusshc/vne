#pragma once
#include "core/arena.h"
#include "game/mode.h"
#include "render/texture.h"
#include "text/layout.h"
#include "formats/state.h"
#include "vm/vm.h"
#include "game/ui.h"

// Modo principal de novela visual (SPEC.md #10): dirige la VM, dibuja el cuadro de
// dialogo real y por fin resuelve el Say esperando input de verdad (cierra ADR-0023).
// Skip y auto viven aqui tambien (no como modos aparte de la pila): son variantes de como
// VnMode avanza el guion, no estados independientes con su propio render.

// Peticion que VnMode le hace a la pila que lo contiene (M15): un Mode no puede apilar ni
// desapilar por su cuenta, igual que MapMode expresa "quiero apilar este guion" con
// pending_trigger_script y los overlays de M7 dicen "cierrame" con wants_close. Existe
// porque la botonera de raton tiene que poder abrir el historial o el menu, y esas dos
// acciones las decide quien es dueno de la pila (main.cpp), no VnMode.
//
// Quien la consume la pone a None; VnMode nunca la limpia por si mismo.
enum class VnUiRequest : u8 {
    None = 0,
    Backlog,
    Menu,
    Save,
    Load,
    RollbackBack,
    RollbackForward,
};

// Botonera de raton del cuadro de dialogo (M15). Es lo que hace que el criterio de SPEC.md
// #12 "una partida completa se juega de principio a fin solo con el raton" sea alcanzable:
// abrir el historial o el menu requeria B/M, que son teclas y nada mas.
enum class VnButton : u8 {
    Backlog = 0,
    Save,
    Load,
    Menu,
    Auto,
    Skip,
    RollbackBack,
    RollbackForward,
    Count,
};
constexpr u32 k_vn_button_count = static_cast<u32>(VnButton::Count);

const char* vn_button_label(VnButton b);

// Rectangulos en coordenadas virtuales. Funciones y no constantes porque update() y
// render() tienen que estar de acuerdo byte a byte sobre donde esta cada cosa: si cada uno
// calculara las suyas, un boton se dibujaria donde no se puede pulsar. Expuestas tambien
// para que un test pueda pinchar un boton concreto sin adivinar coordenadas.
UiRect vn_button_rect(VnButton b);
UiRect vn_choice_rect(u32 option_index, u32 option_count);
UiRect vn_dialogue_box_rect();

struct VnMode : Mode {
    GameState*      state  = nullptr;
    CompiledScript  script;
    FontHandle      font;
    TextureHandle   atlas_tex;  // atlas de sprites (M13): fondos y actores salen de aqui
    FontHandle      bold_font;  // variante sintetica para {b} (M12); invalida = {b} sin efecto
    Arena*          layout_arena = nullptr;  // g_arena_scene: el layout sobrevive entre frames

    TextLayout current_layout;
    u32        layout_pc         = 0xFFFFFFFFu;  // pc para el que se construyo current_layout
    u32        layout_locale_gen = 0xFFFFFFFFu;  // catalog_generation() de ese momento (M10)
    f32        visible_glyphs_f = 0.0f;
    // Temporizacion de {w=n}/{speed=n} (M12). next_event es el indice del siguiente
    // TypewriterEvent de current_layout que queda por aplicar; pause_timer es lo que
    // queda de la pausa en curso.
    u32        next_event         = 0;
    f32        pause_timer        = 0.0f;
    f32        typewriter_speed  = 1.0f;

    bool skip_mode = false;
    bool auto_mode = false;
    f32  auto_hold_timer = 0.0f;

    bool finished = false;

    // Ratón (M15). Las coordenadas que llegan en InputState::mouse_x/y son VIRTUALES
    // (1920x1080): main.cpp ya deshizo el letterbox, ver platform/input.h.
    VnUiRequest ui_request = VnUiRequest::None;

    // Opciones del @choice en curso (M15). Hasta aqui NADIE dibujaba las opciones de un
    // @choice: vm_select_choice existia desde M5 y solo lo llamaban los tests, asi que un
    // guion con ramas era injugable en el juego real —el mismo tipo de hueco que M13
    // encontro con los actores—. Cache con las mismas reglas que current_layout: se
    // reconstruye al cambiar el pc o el idioma, nunca por frame.
    TextLayout choice_layouts[k_max_choice_options];
    u32        choice_count          = 0;
    u32        choice_layout_pc      = 0xFFFFFFFFu;
    u32        choice_layout_locale = 0xFFFFFFFFu;
    i32        choice_selected       = 0;

    // Etiquetas de la botonera. Son texto de interfaz fijo, igual que los nombres de bus
    // de MenuMode: no pasan por el catalogo de localizacion (ver "Pendientes observados").
    TextLayout button_labels[k_vn_button_count];
    bool       button_labels_built = false;
    // Boton bajo el cursor, o -1. Lo calcula update() y lo consume render(): render() no
    // recibe InputState (Mode::render no tiene parametros) y darselo solo para pintar un
    // resaltado seria ensanchar la interfaz de todos los modos por un color.
    i32 hovered_button = -1;

    void update(const InputState& input, f32 dt) override;
    void render() override;
};

constexpr f32 k_typewriter_glyphs_per_second = 40.0f;

// Modo auto (M12): el tiempo de espera tras terminar de escribir una linea es proporcional
// a su longitud, no fijo. Con 1.2 s fijos una linea de tres palabras se quedaba una
// eternidad en pantalla y un parrafo largo desaparecia antes de poder leerlo.
//
// El reparto base + por glifo es el mismo que usa cualquier lector automatico: un minimo
// para registrar que la linea cambio, mas tiempo de lectura real. 0.04 s/glifo son unos
// 25 caracteres por segundo, ritmo de lectura comodo y algo por debajo de la velocidad de
// escritura (40 glifos/s), para que la pausa no se sienta mas corta que el tecleo.
constexpr f32 k_auto_hold_base_seconds      = 0.5f;
constexpr f32 k_auto_hold_seconds_per_glyph = 0.04f;

// Tope para que una linea larguisima (o un layout roto) no deje el juego colgado.
constexpr f32 k_auto_hold_max_seconds = 8.0f;

// Expuesta (y no escondida en el .cpp) para poder verificar el criterio directamente en un
// test, sin tener que conducir un VnMode entero con reloj.
inline f32 vn_auto_hold_seconds(u32 glyph_count) {
    f32 seconds = k_auto_hold_base_seconds +
                  k_auto_hold_seconds_per_glyph * static_cast<f32>(glyph_count);
    return seconds < k_auto_hold_max_seconds ? seconds : k_auto_hold_max_seconds;
}
// Guarda contra un bucle de comandos instantaneos sin Say/Choice/End en el medio (SPEC.md
// #12: el modo skip debe recorrer 1000 comandos en menos de 1 segundo, no colgarse).
constexpr u32 k_skip_steps_per_frame = 64;
