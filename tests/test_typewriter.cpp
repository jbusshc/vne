#include <doctest/doctest.h>

#include "base/arena.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("maquina de escribir: text_draw con visible_glyphs creciente nunca relayoutea") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout l = text_layout(g_test_font_latin, "Hola, mundo. Esto es una prueba.", 10000.0f, &a);
    REQUIRE(l.count > 0);

    u32 calls_before = g_text_layout_call_count;
    for (u32 visible = 0; visible <= l.count; ++visible) {
        text_draw(l, 0.0f, 0.0f, visible);
    }
    CHECK(g_text_layout_call_count == calls_before);

    arena_destroy(&a);
}
