#include "platform/input.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace {
u32 mouse_button_index(u8 sdl_button) {
    switch (sdl_button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_MIDDLE: return 1;
        case SDL_BUTTON_RIGHT: return 2;
        default: return k_max_mouse_buttons;  // fuera de rango: se ignora
    }
}
}  // namespace

void platform_poll_events(InputState* state) {
    std::memset(state->key_pressed, 0, sizeof(state->key_pressed));
    std::memset(state->mouse_pressed, 0, sizeof(state->mouse_pressed));
    state->mouse_wheel_y = 0.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            state->quit_requested = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            u32 sc = static_cast<u32>(event.key.scancode);
            if (sc < k_max_scancodes) {
                if (!state->key_down[sc]) {
                    state->key_pressed[sc] = true;
                }
                state->key_down[sc] = true;
            }
        } else if (event.type == SDL_EVENT_KEY_UP) {
            u32 sc = static_cast<u32>(event.key.scancode);
            if (sc < k_max_scancodes) {
                state->key_down[sc] = false;
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            state->mouse_x = event.motion.x;
            state->mouse_y = event.motion.y;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            u32 b = mouse_button_index(event.button.button);
            if (b < k_max_mouse_buttons) {
                if (!state->mouse_down[b]) {
                    state->mouse_pressed[b] = true;
                }
                state->mouse_down[b] = true;
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            u32 b = mouse_button_index(event.button.button);
            if (b < k_max_mouse_buttons) {
                state->mouse_down[b] = false;
            }
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            state->mouse_wheel_y += event.wheel.y;
        }
    }
}
