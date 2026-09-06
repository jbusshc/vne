#include <SDL3/SDL.h>

#include "base/arena.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "platform/clock.h"
#include "platform/input.h"
#include "platform/window.h"

// M0: esqueleto del motor. Abre una ventana, la limpia a un color y demuestra que el
// bucle de frame no toca el heap (SPEC.md #12, hito M0).

constexpr usize k_perm_arena_size  = 64ull * 1024 * 1024;
constexpr usize k_scene_arena_size = 256ull * 1024 * 1024;
constexpr usize k_frame_arena_size = 8ull * 1024 * 1024;

constexpr u32 k_frame_history_len = 240;
constexpr f64 k_report_interval_s = 2.0;

// Las tres arenas del motor (skill vne-memory-model). Nunca se crean mas de una vez.
static Arena g_arena_perm;
static Arena g_arena_scene;
static Arena g_arena_frame;

static f32 frame_history_p99_ms(const f32* history, u32 count) {
    f32 sorted[k_frame_history_len];
    for (u32 i = 0; i < count; ++i) {
        sorted[i] = history[i];
    }
    for (u32 i = 1; i < count; ++i) {
        f32 key = sorted[i];
        i32 j   = static_cast<i32>(i) - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j -= 1;
        }
        sorted[j + 1] = key;
    }
    if (count == 0) {
        return 0.0f;
    }
    u32 p99_index = (count * 99) / 100;
    if (p99_index >= count) {
        p99_index = count - 1;
    }
    return sorted[p99_index] * 1000.0f;
}

int main(int, char**) {
    g_arena_perm  = arena_create(k_perm_arena_size, "perm");
    g_arena_scene = arena_create(k_scene_arena_size, "scene");
    g_arena_frame = arena_create(k_frame_arena_size, "frame");

    PlatformWindow window{};
    if (!platform_window_create(&window, "vne \xe2\x80\x94 M0", 1280, 720)) {
        return 1;
    }

    InputState input{};
    Clock      clock = clock_create();

    f32* frame_times      = arena_alloc_n<f32>(&g_arena_perm, k_frame_history_len);
    u32  frame_index      = 0;
    u32  frames_recorded  = 0;
    f64  report_timer     = 0.0;
    u64  max_frame_allocs = 0;

    while (!input.quit_requested) {
        arena_reset(&g_arena_frame);
        heap_guard_reset_frame();

        platform_poll_events(&input);
        if (input.key_pressed[SDL_SCANCODE_ESCAPE]) {
            input.quit_requested = true;
        }

        f32 dt = clock_tick(&clock);

        frame_times[frame_index % k_frame_history_len] = dt;
        frame_index += 1;
        frames_recorded =
            frame_index < k_frame_history_len ? frame_index : k_frame_history_len;

        platform_window_clear(&window, 20, 24, 32);

        // Justo antes de presentar: aqui es donde M1 llamara a gfx_present. La regla de
        // cero asignaciones se comprueba en este punto exacto del frame.
        heap_guard_check_frame();
        if (g_frame_alloc_count > max_frame_allocs) {
            max_frame_allocs = g_frame_alloc_count;
        }

        platform_window_present(&window);

        report_timer += dt;
        if (report_timer >= k_report_interval_s) {
            report_timer  = 0.0;
            f32 p99_ms    = frame_history_p99_ms(frame_times, frames_recorded);
            f32 fps       = dt > 0.0f ? 1.0f / dt : 0.0f;
            log_info("fps~%.1f frame_p99=%.2fms heap_allocs_frame_max=%llu", fps, p99_ms,
                      static_cast<unsigned long long>(max_frame_allocs));
        }
    }

    platform_window_destroy(&window);
    arena_destroy(&g_arena_frame);
    arena_destroy(&g_arena_scene);
    arena_destroy(&g_arena_perm);
    return 0;
}
