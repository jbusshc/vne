#pragma once
#include <SDL3/SDL.h>

#include "base/types.h"

// Sin timestep fijo (SPEC.md #6.5): el VN no necesita determinismo porque el rollback usa
// instantaneas completas, no reejecucion.

struct Clock {
    u64 last_ticks = 0;
    f64 freq_hz    = 0.0;
};

inline Clock clock_create() {
    Clock c;
    c.last_ticks = SDL_GetPerformanceCounter();
    c.freq_hz    = static_cast<f64>(SDL_GetPerformanceFrequency());
    return c;
}

inline f32 clock_tick(Clock* c) {
    u64 now      = SDL_GetPerformanceCounter();
    f64 dt       = static_cast<f64>(now - c->last_ticks) / c->freq_hz;
    c->last_ticks = now;
    if (dt > 0.1) {
        dt = 0.1;
    }
    return static_cast<f32>(dt);
}

// Marca de tiempo en microsegundos, sin relacion con Clock/clock_tick: para medir el
// coste de una operacion puntual (skill vne-build-verify), no el dt del bucle de frame.
inline u64 clock_now_microseconds() {
    u64 counter = SDL_GetPerformanceCounter();
    f64 freq_hz = static_cast<f64>(SDL_GetPerformanceFrequency());
    return static_cast<u64>((static_cast<f64>(counter) / freq_hz) * 1000000.0);
}
