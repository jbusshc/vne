#pragma once
#include "base/arena.h"
#include "game/mode.h"
#include "text/layout.h"
#include "vm/state.h"
#include "vm/vm.h"

// Modo principal de novela visual (SPEC.md #10): dirige la VM, dibuja el cuadro de
// dialogo real y por fin resuelve el Say esperando input de verdad (cierra ADR-0023).
// Skip y auto viven aqui tambien (no como modos aparte de la pila): son variantes de como
// VnMode avanza el guion, no estados independientes con su propio render.

struct VnMode : Mode {
    GameState*      state  = nullptr;
    CompiledScript  script;
    FontHandle      font;
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

    void update(const InputState& input, f32 dt) override;
    void render() override;
};

constexpr f32 k_typewriter_glyphs_per_second = 40.0f;
constexpr f32 k_auto_hold_seconds            = 1.2f;
// Guarda contra un bucle de comandos instantaneos sin Say/Choice/End en el medio (SPEC.md
// #12: el modo skip debe recorrer 1000 comandos en menos de 1 segundo, no colgarse).
constexpr u32 k_skip_steps_per_frame = 64;
