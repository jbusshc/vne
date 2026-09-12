#include <doctest/doctest.h>

#include <ostream>

#include "render/render.h"
#include "render/texture.h"

// Criterio de aceptación de M1 (SPEC.md §12): **5000 sprites de un mismo atlas se resuelven en
// una sola draw call.** Se verificaba dibujando 5000 sprites dentro del bucle de frame del
// juego y leyendo el contador en el HUD; ese banco de pruebas se quedó ahí **cuatro hitos
// después de servir para algo**, dibujándose en cada frame del juego distribuido (los logs
// decían `sprites=5158` con el juego jugándose).
//
// P0 del rediseño lo borra de `main.cpp` y lo trae aquí, que es donde un criterio de
// aceptación pertenece: se verifica igual, en las tres configuraciones, sin coste en el juego.
TEST_CASE("batching: 5000 sprites del mismo atlas caben en una sola draw call") {
    // Necesita contexto grafico real (lo monta test_main.cpp con una ventana oculta). Sin él,
    // render_flush no puede emitir nada y el test no mediría nada: mejor decirlo que pasar.
    TextureHandle tex = render_white_texture();
    REQUIRE(tex.valid());

    constexpr u32 k_count = 5000;
    constexpr u32 k_cols  = 100;
    const f32     cell_w  = static_cast<f32>(k_virtual_width) / static_cast<f32>(k_cols);
    const f32     cell_h  = static_cast<f32>(k_virtual_height) / static_cast<f32>(k_count / k_cols);

    render_begin_frame();
    for (u32 i = 0; i < k_count; ++i) {
        Sprite s{};
        s.tex   = tex;
        s.src_w = 1.0f;
        s.src_h = 1.0f;
        s.dst_x = static_cast<f32>(i % k_cols) * cell_w;
        s.dst_y = static_cast<f32>(i / k_cols) * cell_h;
        s.dst_w = cell_w - 1.0f;
        s.dst_h = cell_h - 1.0f;
        // Reparte por capas a proposito: el radix sort tiene que dejarlos contiguos igual,
        // porque la textura es la misma. Si la clave de ordenación estuviera mal construida,
        // esto saldría en varias draw calls.
        s.layer = static_cast<u16>(i % 8);
        s.order = static_cast<u16>(i % 64);
        render_draw_sprite(s);
    }
    render_flush();

    // Se copia a una local ANTES de cualquier macro de doctest: las macros asignan y ejecutan
    // código, y leer un contador global dentro de un CHECK mide el instrumento (lección de M12).
    u32 draw_calls = g_render_draw_call_count;

    // g_render_sprite_count_last_frame publica lo encolado en el frame ANTERIOR (ver
    // render.h), así que hace falta abrir otro frame para poder leerlo. Se comprueba porque
    // un "draw_calls == 1" con cero sprites encolados también daría 1, y eso no probaría nada.
    render_begin_frame();
    u32 sprites = g_render_sprite_count_last_frame;

    MESSAGE("draw_calls=" << draw_calls << " sprites encolados=" << sprites);
    CHECK(draw_calls == 1);
    CHECK(sprites == k_count);
}
