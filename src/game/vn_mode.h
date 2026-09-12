#pragma once
#include "base/arena.h"
#include "game/mode.h"
#include "gfx/texture.h"
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
