#include "platform/input.h"

#include <SDL3/SDL.h>

#include <cstring>

#include "base/heap_guard.h"
#include "base/heap_guard_hooks.h"

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

// SDL asigna de forma diferida dentro de su subsistema de eventos la primera vez que
// maneja ciertos tipos de evento (foco de ventana, entrada del raton, cambio de pantalla).
// Esos eventos los manda el SO cuando quiere: medido en M13, llegan en un frame variable
// (36, 84...), asi que vaciar la cola al crear la ventana —que es lo que hacia M12— no los
// atrapa. Son 9 asignaciones, UNA vez en toda la vida del proceso.
//
// En vez de suspender el guard aqui para siempre —que cegaria un camino que corre en TODOS
// los frames, justo lo que ADR-0058 dice que no se haga— se le da un presupuesto de por
// vida. Mientras SDL no lo agote, sus asignaciones no cuentan; en cuanto lo agote, cuentan
// y el assert salta. Una fuga de verdad dentro del manejo de input reventaria el
// presupuesto en unos pocos frames y se veria igual.
constexpr u64 k_sdl_lazy_init_budget = 64;

// Solo lo que SDL asigna DENTRO de poll_events. No vale mirar su contador de por vida: SDL
// asigna a espuertas durante SDL_Init y la creacion de la ventana, asi que cualquier
// presupuesto razonable ya estaria agotado antes del primer frame (me paso al escribirlo, y
// el efecto fue que el guard no se suspendia nunca).
u64 g_sdl_allocs_in_poll = 0;

void platform_poll_events(InputState* state) {
    bool within_budget = g_sdl_allocs_in_poll < k_sdl_lazy_init_budget;
    u64  sdl_before     = heap_guard_lifetime_count(HeapSource::Sdl);
    if (within_budget) {
        heap_guard_suspend();
    }
    struct GuardResume {
        bool active;
        u64  before;
        ~GuardResume() {
            if (active) {
                heap_guard_resume();
            }
            // El contador de por vida sube este el guard suspendido o no, asi que el
            // presupuesto se consume igual y acaba cerrandose solo.
            g_sdl_allocs_in_poll += heap_guard_lifetime_count(HeapSource::Sdl) - before;
        }
    } guard_resume{within_budget, sdl_before};

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
