#pragma once
#include "base/types.h"

constexpr u32 k_max_scancodes = 512;

// Snapshot de entrada llenado por platform_poll_events() cada frame. Nada de esto
// sobrevive al frame: si un Mode necesita recordar estado de input, lo copia a GameState.
struct InputState {
    bool quit_requested               = false;
    bool key_down[k_max_scancodes]    = {};
    bool key_pressed[k_max_scancodes] = {};  // flanco de bajada durante este frame
};

void platform_poll_events(InputState* state);
