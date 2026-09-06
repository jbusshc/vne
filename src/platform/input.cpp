#include "platform/input.h"

#include <SDL3/SDL.h>

#include <cstring>

void platform_poll_events(InputState* state) {
    std::memset(state->key_pressed, 0, sizeof(state->key_pressed));

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
        }
    }
}
