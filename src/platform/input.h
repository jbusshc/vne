#pragma once
#include <type_traits>

#include "core/types.h"

constexpr u32 k_max_scancodes = 512;

constexpr u32 k_max_mouse_buttons = 3;  // izquierdo, medio, derecho (indices 0,1,2)

// Snapshot de entrada llenado por platform_poll_events() cada frame. Nada de esto
// sobrevive al frame: si un Mode necesita recordar estado de input, lo copia a GameState.
//
// El raton se rastrea en PIXELES DE VENTANA, que es lo que el editor necesita (ImGui y
// sokol_imgui trabajan en pixeles de ventana directamente). La UI de novela visual, en
// cambio, vive en el espacio virtual 1920x1080 y no conoce el tamano de la ventana, asi que
// main.cpp convierte una COPIA de este estado con render_window_to_virtual y es esa copia la
// que reciben los Mode (M15, ADR-0070). Es tambien la copia que se graba en un .vnrec: lo
// que se guarda es lo que vio la logica de juego, para que una sesion se pueda reproducir en
// una ventana de otro tamano —o sin ventana ninguna, dentro de la suite de tests—.
// Relleno EXPLICITO (M15, misma leccion que ADR-0028): un InputState se escribe tal cual a
// un archivo .vnrec al grabar una sesion, y 1025 bool seguidos de un f32 dejan huecos de
// alineacion que el compilador no inicializa. Sin declararlos, dos estados logicamente
// identicos podrian diferir en bytes, y entonces ni la compresion por repeticion ni la
// comparacion de una reproduccion contra su grabacion serian fiables.
struct InputState {
    bool quit_requested               = false;
    bool key_down[k_max_scancodes]    = {};
    bool key_pressed[k_max_scancodes] = {};  // flanco de bajada durante este frame
    u8   _pad0[3]                      = {};

    f32  mouse_x = 0.0f, mouse_y = 0.0f;  // pixeles de ventana, origen arriba-izquierda
    f32  mouse_wheel_y                     = 0.0f;
    bool mouse_down[k_max_mouse_buttons]    = {};
    bool mouse_pressed[k_max_mouse_buttons] = {};
    u8   _pad1[2]                            = {};
};
static_assert(sizeof(InputState) == 1048);
static_assert(std::is_trivially_copyable_v<InputState>);

void platform_poll_events(InputState* state);
