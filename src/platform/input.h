#pragma once
#include "base/types.h"

constexpr u32 k_max_scancodes = 512;

constexpr u32 k_max_mouse_buttons = 3;  // izquierdo, medio, derecho (indices 0,1,2)

// Snapshot de entrada llenado por platform_poll_events() cada frame. Nada de esto
// sobrevive al frame: si un Mode necesita recordar estado de input, lo copia a GameState.
//
// El raton (M8, editor) se rastrea en pixeles de ventana reales, no en el espacio
// virtual 1920x1080 (SPEC.md #6.4 solo exige esa conversion para contenido de juego;
// ImGui/sokol_imgui ya trabaja en pixeles de ventana directamente, y la UI de VN no lo
// necesita todavia — sigue siendo solo teclado, ADR-0040).
struct InputState {
    bool quit_requested               = false;
    bool key_down[k_max_scancodes]    = {};
    bool key_pressed[k_max_scancodes] = {};  // flanco de bajada durante este frame

    f32  mouse_x = 0.0f, mouse_y = 0.0f;  // pixeles de ventana, origen arriba-izquierda
    f32  mouse_wheel_y                                = 0.0f;
    bool mouse_down[k_max_mouse_buttons]    = {};
    bool mouse_pressed[k_max_mouse_buttons] = {};
};

void platform_poll_events(InputState* state);
